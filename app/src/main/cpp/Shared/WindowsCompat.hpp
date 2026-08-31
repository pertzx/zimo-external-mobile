#pragma once

// Compatibilidade Windows -> Android
// Define os tipos e macros que o codigo PC usa mas que nao existem no Android

#ifdef __ANDROID__

#include <cstdint>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

// Tipos Windows comuns
using HANDLE = void*;
using DWORD = uint32_t;
using WORD = uint16_t;
using BYTE = uint8_t;
using BOOL = int;
using NTSTATUS = int;
using LONG = int32_t;
using LONGLONG = int64_t;
using ULONG = uint32_t;
using USHORT = uint16_t;
using UCHAR = uint8_t;
using PVOID = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPDWORD = DWORD*;
using LPBYTE = BYTE*;
using SIZE_T = size_t;

// Macros Windows
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define WINAPI
#define CALLBACK
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define EXCEPTION_EXECUTE_HANDLER 1

// Time functions inline
inline LONGLONG GetTickCount64() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (LONGLONG)(ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}

// VirtualAlloc/Free stubs
inline PVOID VirtualAlloc(PVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) {
    (void)flAllocationType; (void)flProtect;
    if (lpAddress) return lpAddress;
    return malloc(dwSize);
}
inline BOOL VirtualFree(PVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType) {
    (void)dwSize; (void)dwFreeType;
    free(lpAddress);
    return TRUE;
}
inline void SecureZeroMemory(PVOID ptr, SIZE_T cnt) {
    volatile unsigned char* p = (volatile unsigned char*)ptr;
    while (cnt--) *p++ = 0;
}
inline void ZeroMemory(PVOID Destination, SIZE_T Length) {
    memset(Destination, 0, Length);
}
inline void RtlZeroMemory(PVOID Destination, SIZE_T Length) {
    memset(Destination, 0, Length);
}
inline void RtlCopyMemory(PVOID Destination, LPCVOID Source, SIZE_T Length) {
    memcpy(Destination, Source, Length);
}
inline PVOID RtlSecureZeroMemory(PVOID ptr, SIZE_T cnt) {
    SecureZeroMemory(ptr, cnt);
    return ptr;
}

// Interlocked functions
inline LONG InterlockedExchange(volatile LONG* Target, LONG Value) {
    return __sync_lock_test_and_set(Target, Value);
}
inline LONG InterlockedCompareExchange(volatile LONG* Destination, LONG Exchange, LONG Comperand) {
    return __sync_val_compare_and_swap(Destination, Comperand, Exchange);
}
inline LONGLONG InterlockedCompareExchange64(volatile LONGLONG* Destination, LONGLONG Exchange, LONGLONG Comperand) {
    return __sync_val_compare_and_swap(Destination, Comperand, Exchange);
}
inline LONG InterlockedIncrement(volatile LONG* Addend) {
    return __sync_add_and_fetch(Addend, 1);
}
inline LONG InterlockedDecrement(volatile LONG* Addend) {
    return __sync_sub_and_fetch(Addend, 1);
}
inline LONG InterlockedExchangeAdd(volatile LONG* Addend, LONG Value) {
    return __sync_fetch_and_add(Addend, Value);
}

// MemoryBarrier
#define MemoryBarrier() __sync_synchronize()

// Sleep
inline void Sleep(DWORD dwMilliseconds) {
    usleep(dwMilliseconds * 1000);
}

// String functions
inline int strcpy_s(char* dest, size_t destsz, const char* src) {
    if (!dest || !src || destsz == 0) return -1;
    size_t i;
    for (i = 0; i < destsz - 1 && src[i]; i++) dest[i] = src[i];
    dest[i] = 0;
    return 0;
}
inline int strcat_s(char* dest, size_t destsz, const char* src) {
    if (!dest || !src || destsz == 0) return -1;
    size_t len = strlen(dest);
    size_t i;
    for (i = 0; len + i < destsz - 1 && src[i]; i++) dest[len + i] = src[i];
    dest[len + i] = 0;
    return 0;
}
inline int strncpy_s(char* dest, size_t destsz, const char* src, size_t count) {
    if (!dest || !src || destsz == 0) return -1;
    size_t i;
    for (i = 0; i < destsz - 1 && i < count && src[i]; i++) dest[i] = src[i];
    dest[i] = 0;
    return 0;
}
inline int memcpy_s(void* dest, size_t destsz, const void* src, size_t count) {
    if (!dest || !src || count > destsz) return -1;
    memcpy(dest, src, count);
    return 0;
}
inline int memmove_s(void* dest, size_t destsz, const void* src, size_t count) {
    if (!dest || !src || count > destsz) return -1;
    memmove(dest, src, count);
    return 0;
}

// Thread creation wrapper
inline DWORD CreateThreadWrapper(void* (*start_routine)(void*), void* arg) {
    pthread_t tid;
    pthread_create(&tid, nullptr, start_routine, arg);
    pthread_detach(tid);
    return (DWORD)(uintptr_t)tid;
}

// Suspend/Resume thread stubs
inline DWORD SuspendThread(pthread_t hThread) { (void)hThread; return 0; }
inline DWORD ResumeThread(pthread_t hThread) { (void)hThread; return 0; }
inline BOOL TerminateThread(pthread_t hThread, DWORD dwExitCode) {
    (void)dwExitCode;
    pthread_cancel(hThread);
    return TRUE;
}

// Critical section -> pthread_mutex
using CRITICAL_SECTION = pthread_mutex_t;
inline void InitializeCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_init(cs, nullptr); }
inline void EnterCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_lock(cs); }
inline void LeaveCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_unlock(cs); }
inline void DeleteCriticalSection(CRITICAL_SECTION* cs) { pthread_mutex_destroy(cs); }

// GetLastError stub
inline DWORD GetLastError() { return 0; }
inline void SetLastError(DWORD dwErrCode) { (void)dwErrCode; }

// Event stubs
inline HANDLE CreateEventW(void* lpEventAttributes, BOOL bManualReset, BOOL bInitialState, const wchar_t* lpName) {
    (void)lpEventAttributes; (void)bManualReset; (void)bInitialState; (void)lpName;
    return nullptr;
}
inline BOOL SetEvent(HANDLE hEvent) { (void)hEvent; return TRUE; }
inline BOOL ResetEvent(HANDLE hEvent) { (void)hEvent; return TRUE; }
inline BOOL WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    (void)hHandle; (void)dwMilliseconds;
    usleep(dwMilliseconds * 1000);
    return 0;
}
inline BOOL CloseHandle(HANDLE hObject) { (void)hObject; return TRUE; }

// File/Path
inline DWORD GetTempPathW(DWORD nBufferLength, wchar_t* lpBuffer) {
    (void)nBufferLength;
    *lpBuffer = L'\0';
    return 0;
}
inline BOOL DeleteFileW(const wchar_t* lpFileName) { (void)lpFileName; return TRUE; }

// Process
inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }
inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }

// Misc
inline void OutputDebugStringA(const char* lpOutputString) {
    __android_log_print(ANDROID_LOG_DEBUG, "StormDebug", "%s", lpOutputString);
}

#else
#include <Windows.h>
#endif
