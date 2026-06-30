// hook_common.h
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace DFHack {
namespace DFCH {
namespace Hooks {

    /* Common hook utility macros */
    #define HOOK_FUNC(fn_name) fn_name##_hook
    #define ORIG_FUNC(fn_name) fn_name##_orig

    #define SETUP_ORIG_FUNC_OFFSET(fn_name, offset) fn_name ORIG_FUNC(fn_name) = (fn_name)((UINT64)GetModuleHandle(NULL) + offset)
    #define SETUP_ORIG_FUNC_FNAME(fn_name, module_name) fn_name ORIG_FUNC(fn_name) = (fn_name)(GetProcAddress(GetModuleHandle(TEXT(#module_name)), #fn_name))
    #define GET_ADDR(offset) (PVOID)((UINT64)GetModuleHandle(NULL) + offset)

    #define DECLARE_ORIG_FUNC(fn_type, fn_name) extern fn_type ORIG_FUNC(fn_name);
    #define DEFINE_ORIG_FUNC(fn_type, fn_name) fn_type ORIG_FUNC(fn_name) = nullptr;
    #define INIT_ORIG_FUNC(fn_type, fn_name, module_handle) ORIG_FUNC(fn_name) = (fn_type)(GetProcAddress(module_handle, #fn_name));

} // namespace Hooks
} // namespace DFCH
} // namespace DFHack
