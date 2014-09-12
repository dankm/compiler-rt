//===-- asan_win_uar_thunk.cc ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This file defines things that need to be present in the application modules
// to interact with the ASan DLL runtime correctly and can't be implemented
// using the default "import library" generated when linking the DLL RTL.
//
// This includes:
//  - forwarding the detect_stack_use_after_return runtime option
//  - FIXME: installing a custom SEH handler (PR20918)
//
//===----------------------------------------------------------------------===//

// Only compile this code when buidling asan_dynamic_runtime_thunk.lib
// Using #ifdef rather than relying on Makefiles etc.
// simplifies the build procedure.
#ifdef ASAN_DYNAMIC_RUNTIME_THUNK
extern "C" {
__declspec(dllimport) int __asan_should_detect_stack_use_after_return();

// Define a copy of __asan_option_detect_stack_use_after_return that should be
// used when linking an MD runtime with a set of object files on Windows.
//
// The ASan MD runtime dllexports '__asan_option_detect_stack_use_after_return',
// so normally we would just dllimport it.  Unfortunately, the dllimport
// attribute adds __imp_ prefix to the symbol name of a variable.
// Since in general we don't know if a given TU is going to be used
// with a MT or MD runtime and we don't want to use ugly __imp_ names on Windows
// just to work around this issue, let's clone the a variable that is
// constant after initialization anyways.
int __asan_option_detect_stack_use_after_return =
    __asan_should_detect_stack_use_after_return();
}
#endif // ASAN_DYNAMIC_RUNTIME_THUNK