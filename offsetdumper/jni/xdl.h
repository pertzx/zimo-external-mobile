// xdl.h - Mini xDL implementation (bypasses Android namespace isolation)
// Compatible with the xDL API used in zygisk painel/AnekoCheat
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// xdl_open: opens a lib bypassing namespace isolation
// Returns a handle (or NULL on failure) that can be used with xdl_sym
void* xdl_open(const char* name, int flags);

// xdl_sym: finds a symbol in the handle returned by xdl_open
// Returns the function pointer (or NULL)
void* xdl_sym(void* handle, const char* name, size_t* cache);

// xdl_close: closes the handle (no-op in our impl since we use mmap)
void xdl_close(void* handle);

#ifdef __cplusplus
}
#endif