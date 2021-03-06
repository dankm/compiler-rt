//===-- tsan_libdispatch_mac.cc -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Mac-specific libdispatch (GCD) support.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_MAC

#include "sanitizer_common/sanitizer_common.h"
#include "interception/interception.h"
#include "tsan_interceptors.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"

#include <Block.h>
#include <dispatch/dispatch.h>
#include <pthread.h>

namespace __tsan {

typedef struct {
  dispatch_queue_t queue;
  void *orig_context;
  dispatch_function_t orig_work;
} tsan_block_context_t;

static tsan_block_context_t *AllocContext(ThreadState *thr, uptr pc,
                                          dispatch_queue_t queue,
                                          void *orig_context,
                                          dispatch_function_t orig_work) {
  tsan_block_context_t *new_context =
      (tsan_block_context_t *)user_alloc(thr, pc, sizeof(tsan_block_context_t));
  new_context->queue = queue;
  new_context->orig_context = orig_context;
  new_context->orig_work = orig_work;
  return new_context;
}

static void dispatch_callback_wrap_acquire(void *param) {
  SCOPED_INTERCEPTOR_RAW(dispatch_async_f_callback_wrap);
  tsan_block_context_t *context = (tsan_block_context_t *)param;
  Acquire(thr, pc, (uptr)context);
  context->orig_work(context->orig_context);
  user_free(thr, pc, context);
}

static void invoke_and_release_block(void *param) {
  dispatch_block_t block = (dispatch_block_t)param;
  block();
  Block_release(block);
}

#define DISPATCH_INTERCEPT_B(name)                                           \
  TSAN_INTERCEPTOR(void, name, dispatch_queue_t q, dispatch_block_t block) { \
    SCOPED_TSAN_INTERCEPTOR(name, q, block);                                 \
    dispatch_block_t heap_block = Block_copy(block);                         \
    tsan_block_context_t *new_context =                                      \
        AllocContext(thr, pc, q, heap_block, &invoke_and_release_block);     \
    Release(thr, pc, (uptr)new_context);                                     \
    REAL(name##_f)(q, new_context, dispatch_callback_wrap_acquire);          \
  }

#define DISPATCH_INTERCEPT_F(name)                                \
  TSAN_INTERCEPTOR(void, name, dispatch_queue_t q, void *context, \
                   dispatch_function_t work) {                    \
    SCOPED_TSAN_INTERCEPTOR(name, q, context, work);              \
    tsan_block_context_t *new_context =                           \
        AllocContext(thr, pc, q, context, work);                  \
    Release(thr, pc, (uptr)new_context);                          \
    REAL(name)(q, new_context, dispatch_callback_wrap_acquire);   \
  }

// We wrap dispatch_async, dispatch_sync and friends where we allocate a new
// context, which is used to synchronize (we release the context before
// submitting, and the callback acquires it before executing the original
// callback).
DISPATCH_INTERCEPT_B(dispatch_async)
DISPATCH_INTERCEPT_B(dispatch_barrier_async)
DISPATCH_INTERCEPT_F(dispatch_async_f)
DISPATCH_INTERCEPT_F(dispatch_barrier_async_f)
DISPATCH_INTERCEPT_B(dispatch_sync)
DISPATCH_INTERCEPT_B(dispatch_barrier_sync)
DISPATCH_INTERCEPT_F(dispatch_sync_f)
DISPATCH_INTERCEPT_F(dispatch_barrier_sync_f)

// GCD's dispatch_once implementation has a fast path that contains a racy read
// and it's inlined into user's code. Furthermore, this fast path doesn't
// establish a proper happens-before relations between the initialization and
// code following the call to dispatch_once. We could deal with this in
// instrumented code, but there's not much we can do about it in system
// libraries. Let's disable the fast path (by never storing the value ~0 to
// predicate), so the interceptor is always called, and let's add proper release
// and acquire semantics. Since TSan does not see its own atomic stores, the
// race on predicate won't be reported - the only accesses to it that TSan sees
// are the loads on the fast path. Loads don't race. Secondly, dispatch_once is
// both a macro and a real function, we want to intercept the function, so we
// need to undefine the macro.
#undef dispatch_once
TSAN_INTERCEPTOR(void, dispatch_once, dispatch_once_t *predicate,
                 dispatch_block_t block) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_once, predicate, block);
  atomic_uint32_t *a = reinterpret_cast<atomic_uint32_t *>(predicate);
  u32 v = atomic_load(a, memory_order_acquire);
  if (v == 0 &&
      atomic_compare_exchange_strong(a, &v, 1, memory_order_relaxed)) {
    block();
    Release(thr, pc, (uptr)a);
    atomic_store(a, 2, memory_order_release);
  } else {
    while (v != 2) {
      internal_sched_yield();
      v = atomic_load(a, memory_order_acquire);
    }
    Acquire(thr, pc, (uptr)a);
  }
}

#undef dispatch_once_f
TSAN_INTERCEPTOR(void, dispatch_once_f, dispatch_once_t *predicate,
                 void *context, dispatch_function_t function) {
  SCOPED_TSAN_INTERCEPTOR(dispatch_once_f, predicate, context, function);
  WRAP(dispatch_once)(predicate, ^(void) {
    function(context);
  });
}

}  // namespace __tsan

#endif  // SANITIZER_MAC
