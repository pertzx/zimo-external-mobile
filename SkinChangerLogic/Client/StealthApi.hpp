#pragma once
// Resolve dinâmico de APIs hot-scan (some do IAT). Não usar ForcedInclude.
// Requer Windows.h já incluído (ou inclui aqui).

#ifndef STEALTH_API_HPP
#define STEALTH_API_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstring>
#include "XorStr.hpp"

namespace StealthApi {

    inline void CopyXorName(char* dst, size_t dstSize, const char* src) {
        if (!dst || dstSize == 0) return;
        if (!src) { dst[0] = 0; return; }
        ::strncpy_s(dst, dstSize, src, _TRUNCATE);
    }

    inline HMODULE Mod(const wchar_t* wideName) {
        return ::GetModuleHandleW(wideName);
    }

    inline HMODULE LoadMod(const wchar_t* wideName) {
        HMODULE m = ::GetModuleHandleW(wideName);
        if (!m) m = ::LoadLibraryW(wideName);
        return m;
    }

    template <typename Fn>
    inline Fn Resolve(HMODULE mod, const char* apiName) {
        if (!mod || !apiName || !apiName[0]) return nullptr;
        return reinterpret_cast<Fn>(::GetProcAddress(mod, apiName));
    }

    template <typename Fn>
    inline Fn ResolveXor(HMODULE mod, const char* xorDecryptedName) {
        char name[128]{};
        CopyXorName(name, sizeof(name), xorDecryptedName);
        return Resolve<Fn>(mod, name);
    }

    inline HMODULE Kernel32() {
        static HMODULE m = []() -> HMODULE {
            wchar_t dll[32]{};
            ::wcsncpy_s(dll, XorStr(L"kernel32"), _TRUNCATE); // sem .dll
            return Mod(dll);
        }();
        return m;
    }

    inline HMODULE User32() {
        static HMODULE m = []() -> HMODULE {
            wchar_t dll[32]{};
            ::wcsncpy_s(dll, XorStr(L"user32"), _TRUNCATE);
            return Mod(dll);
        }();
        return m;
    }

    inline HMODULE Advapi32() {
        static HMODULE m = []() -> HMODULE {
            wchar_t dll[32]{};
            ::wcsncpy_s(dll, XorStr(L"advapi32"), _TRUNCATE);
            return LoadMod(dll);
        }();
        return m;
    }

    inline HMODULE WinHttp() {
        static HMODULE m = []() -> HMODULE {
            wchar_t dll[32]{};
            ::wcsncpy_s(dll, XorStr(L"winhttp"), _TRUNCATE);
            return LoadMod(dll);
        }();
        return m;
    }

    inline HMODULE WinINet() {
        static HMODULE m = []() -> HMODULE {
            wchar_t dll[32]{};
            ::wcsncpy_s(dll, XorStr(L"wininet"), _TRUNCATE);
            return LoadMod(dll);
        }();
        return m;
    }

    // ─── Kernel32 (memory / process) ─────────────────────────────────────────
    using Fn_RPM = BOOL(WINAPI*)(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T*);
    using Fn_WPM = BOOL(WINAPI*)(HANDLE, LPVOID, LPCVOID, SIZE_T, SIZE_T*);
    using Fn_OpenProcess = HANDLE(WINAPI*)(DWORD, BOOL, DWORD);
    using Fn_VirtualAllocEx = LPVOID(WINAPI*)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
    using Fn_VirtualFreeEx = BOOL(WINAPI*)(HANDLE, LPVOID, SIZE_T, DWORD);
    using Fn_VirtualQueryEx = SIZE_T(WINAPI*)(HANDLE, LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
    using Fn_VirtualProtect = BOOL(WINAPI*)(LPVOID, SIZE_T, DWORD, PDWORD);
    using Fn_CreateRemoteThread = HANDLE(WINAPI*)(HANDLE, LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
    using Fn_DuplicateHandle = BOOL(WINAPI*)(HANDLE, HANDLE, HANDLE, LPHANDLE, DWORD, BOOL, DWORD);

    inline Fn_RPM FnReadProcessMemory() {
        static Fn_RPM f = ResolveXor<Fn_RPM>(Kernel32(), XorStr("ReadProcessMemory"));
        return f;
    }
    inline Fn_WPM FnWriteProcessMemory() {
        static Fn_WPM f = ResolveXor<Fn_WPM>(Kernel32(), XorStr("WriteProcessMemory"));
        return f;
    }
    inline Fn_OpenProcess FnOpenProcess() {
        static Fn_OpenProcess f = ResolveXor<Fn_OpenProcess>(Kernel32(), XorStr("OpenProcess"));
        return f;
    }
    inline Fn_VirtualAllocEx FnVirtualAllocEx() {
        static Fn_VirtualAllocEx f = ResolveXor<Fn_VirtualAllocEx>(Kernel32(), XorStr("VirtualAllocEx"));
        return f;
    }
    inline Fn_VirtualFreeEx FnVirtualFreeEx() {
        static Fn_VirtualFreeEx f = ResolveXor<Fn_VirtualFreeEx>(Kernel32(), XorStr("VirtualFreeEx"));
        return f;
    }
    inline Fn_VirtualQueryEx FnVirtualQueryEx() {
        static Fn_VirtualQueryEx f = ResolveXor<Fn_VirtualQueryEx>(Kernel32(), XorStr("VirtualQueryEx"));
        return f;
    }
    inline Fn_VirtualProtect FnVirtualProtect() {
        static Fn_VirtualProtect f = ResolveXor<Fn_VirtualProtect>(Kernel32(), XorStr("VirtualProtect"));
        return f;
    }
    inline Fn_CreateRemoteThread FnCreateRemoteThread() {
        static Fn_CreateRemoteThread f = ResolveXor<Fn_CreateRemoteThread>(Kernel32(), XorStr("CreateRemoteThread"));
        return f;
    }
    inline Fn_DuplicateHandle FnDuplicateHandle() {
        static Fn_DuplicateHandle f = ResolveXor<Fn_DuplicateHandle>(Kernel32(), XorStr("DuplicateHandle"));
        return f;
    }

    inline BOOL WINAPI ReadProcessMemory(HANDLE h, LPCVOID a, LPVOID b, SIZE_T n, SIZE_T* r) {
        auto f = FnReadProcessMemory();
        return f ? f(h, a, b, n, r) : FALSE;
    }
    inline BOOL WINAPI WriteProcessMemory(HANDLE h, LPVOID a, LPCVOID b, SIZE_T n, SIZE_T* w) {
        auto f = FnWriteProcessMemory();
        return f ? f(h, a, b, n, w) : FALSE;
    }
    inline HANDLE WINAPI OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
        auto f = FnOpenProcess();
        return f ? f(access, inherit, pid) : nullptr;
    }
    inline LPVOID WINAPI VirtualAllocEx(HANDLE h, LPVOID a, SIZE_T s, DWORD t, DWORD p) {
        auto f = FnVirtualAllocEx();
        return f ? f(h, a, s, t, p) : nullptr;
    }
    inline BOOL WINAPI VirtualFreeEx(HANDLE h, LPVOID a, SIZE_T s, DWORD t) {
        auto f = FnVirtualFreeEx();
        return f ? f(h, a, s, t) : FALSE;
    }
    inline SIZE_T WINAPI VirtualQueryEx(HANDLE h, LPCVOID a, PMEMORY_BASIC_INFORMATION m, SIZE_T l) {
        auto f = FnVirtualQueryEx();
        return f ? f(h, a, m, l) : 0;
    }
    inline BOOL WINAPI VirtualProtect(LPVOID a, SIZE_T s, DWORD n, PDWORD o) {
        auto f = FnVirtualProtect();
        return f ? f(a, s, n, o) : FALSE;
    }
    inline HANDLE WINAPI CreateRemoteThread(HANDLE h, LPSECURITY_ATTRIBUTES sa, SIZE_T stack,
        LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags, LPDWORD tid) {
        auto f = FnCreateRemoteThread();
        return f ? f(h, sa, stack, start, param, flags, tid) : nullptr;
    }
    inline BOOL WINAPI DuplicateHandle(HANDLE srcProc, HANDLE src, HANDLE dstProc, LPHANDLE dst,
        DWORD access, BOOL inherit, DWORD options) {
        auto f = FnDuplicateHandle();
        return f ? f(srcProc, src, dstProc, dst, access, inherit, options) : FALSE;
    }

    // ─── User32 ──────────────────────────────────────────────────────────────
    using Fn_GetAsyncKeyState = SHORT(WINAPI*)(int);
    using Fn_SendInput = UINT(WINAPI*)(UINT, LPINPUT, int);
    using Fn_SetWindowDisplayAffinity = BOOL(WINAPI*)(HWND, DWORD);
    using Fn_GetWindowDisplayAffinity = BOOL(WINAPI*)(HWND, DWORD*);

    inline SHORT WINAPI GetAsyncKeyState(int vk) {
        static auto f = ResolveXor<Fn_GetAsyncKeyState>(User32(), XorStr("GetAsyncKeyState"));
        return f ? f(vk) : (SHORT)0;
    }
    inline UINT WINAPI SendInput(UINT n, LPINPUT i, int cb) {
        static auto f = ResolveXor<Fn_SendInput>(User32(), XorStr("SendInput"));
        return f ? f(n, i, cb) : 0;
    }
    inline BOOL WINAPI SetWindowDisplayAffinity(HWND h, DWORD d) {
        static auto f = ResolveXor<Fn_SetWindowDisplayAffinity>(User32(), XorStr("SetWindowDisplayAffinity"));
        return f ? f(h, d) : FALSE;
    }
    inline BOOL WINAPI GetWindowDisplayAffinity(HWND h, DWORD* d) {
        static auto f = ResolveXor<Fn_GetWindowDisplayAffinity>(User32(), XorStr("GetWindowDisplayAffinity"));
        return f ? f(h, d) : FALSE;
    }

    // ─── Advapi32 ────────────────────────────────────────────────────────────
    using Fn_OpenProcessToken = BOOL(WINAPI*)(HANDLE, DWORD, PHANDLE);
    using Fn_LookupPrivilegeValueA = BOOL(WINAPI*)(LPCSTR, LPCSTR, PLUID);
    using Fn_AdjustTokenPrivileges = BOOL(WINAPI*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);

    inline BOOL WINAPI OpenProcessToken(HANDLE p, DWORD a, PHANDLE t) {
        static auto f = ResolveXor<Fn_OpenProcessToken>(Advapi32(), XorStr("OpenProcessToken"));
        return f ? f(p, a, t) : FALSE;
    }
    inline BOOL WINAPI LookupPrivilegeValueA(LPCSTR s, LPCSTR n, PLUID l) {
        static auto f = ResolveXor<Fn_LookupPrivilegeValueA>(Advapi32(), XorStr("LookupPrivilegeValueA"));
        return f ? f(s, n, l) : FALSE;
    }
    inline BOOL WINAPI AdjustTokenPrivileges(HANDLE t, BOOL d, PTOKEN_PRIVILEGES n, DWORD bl, PTOKEN_PRIVILEGES p, PDWORD rl) {
        static auto f = ResolveXor<Fn_AdjustTokenPrivileges>(Advapi32(), XorStr("AdjustTokenPrivileges"));
        return f ? f(t, d, n, bl, p, rl) : FALSE;
    }

    // ─── WinHTTP (tipos em winhttp.h — wrappers usam void* equivalente via includes do caller) ──
    // Tipagens aqui usam PVOID/HINTERNET-like void* para não forçar winhttp.h neste header.

    using HINTERNET_T = LPVOID;
    using Fn_WinHttpOpen = HINTERNET_T(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
    using Fn_WinHttpConnect = HINTERNET_T(WINAPI*)(HINTERNET_T, LPCWSTR, WORD, DWORD);
    using Fn_WinHttpOpenRequest = HINTERNET_T(WINAPI*)(HINTERNET_T, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
    using Fn_WinHttpAddRequestHeaders = BOOL(WINAPI*)(HINTERNET_T, LPCWSTR, DWORD, DWORD);
    using Fn_WinHttpSendRequest = BOOL(WINAPI*)(HINTERNET_T, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
    using Fn_WinHttpReceiveResponse = BOOL(WINAPI*)(HINTERNET_T, LPVOID);
    using Fn_WinHttpQueryDataAvailable = BOOL(WINAPI*)(HINTERNET_T, LPDWORD);
    using Fn_WinHttpReadData = BOOL(WINAPI*)(HINTERNET_T, LPVOID, DWORD, LPDWORD);
    using Fn_WinHttpCloseHandle = BOOL(WINAPI*)(HINTERNET_T);
    using Fn_WinHttpSetOption = BOOL(WINAPI*)(HINTERNET_T, DWORD, LPVOID, DWORD);

    inline HINTERNET_T WINAPI WinHttpOpen(LPCWSTR a, DWORD t, LPCWSTR p, LPCWSTR b, DWORD f) {
        static auto fn = ResolveXor<Fn_WinHttpOpen>(WinHttp(), XorStr("WinHttpOpen"));
        return fn ? fn(a, t, p, b, f) : nullptr;
    }
    inline HINTERNET_T WINAPI WinHttpConnect(HINTERNET_T s, LPCWSTR host, WORD port, DWORD reserved) {
        static auto fn = ResolveXor<Fn_WinHttpConnect>(WinHttp(), XorStr("WinHttpConnect"));
        return fn ? fn(s, host, port, reserved) : nullptr;
    }
    inline HINTERNET_T WINAPI WinHttpOpenRequest(HINTERNET_T c, LPCWSTR verb, LPCWSTR obj, LPCWSTR ver, LPCWSTR ref, LPCWSTR* accept, DWORD flags) {
        static auto fn = ResolveXor<Fn_WinHttpOpenRequest>(WinHttp(), XorStr("WinHttpOpenRequest"));
        return fn ? fn(c, verb, obj, ver, ref, accept, flags) : nullptr;
    }
    inline BOOL WINAPI WinHttpAddRequestHeaders(HINTERNET_T r, LPCWSTR headers, DWORD len, DWORD modifiers) {
        static auto fn = ResolveXor<Fn_WinHttpAddRequestHeaders>(WinHttp(), XorStr("WinHttpAddRequestHeaders"));
        return fn ? fn(r, headers, len, modifiers) : FALSE;
    }
    inline BOOL WINAPI WinHttpSendRequest(HINTERNET_T r, LPCWSTR headers, DWORD headersLen, LPVOID optional, DWORD optionalLen, DWORD total, DWORD_PTR ctx) {
        static auto fn = ResolveXor<Fn_WinHttpSendRequest>(WinHttp(), XorStr("WinHttpSendRequest"));
        return fn ? fn(r, headers, headersLen, optional, optionalLen, total, ctx) : FALSE;
    }
    inline BOOL WINAPI WinHttpReceiveResponse(HINTERNET_T r, LPVOID reserved) {
        static auto fn = ResolveXor<Fn_WinHttpReceiveResponse>(WinHttp(), XorStr("WinHttpReceiveResponse"));
        return fn ? fn(r, reserved) : FALSE;
    }
    inline BOOL WINAPI WinHttpQueryDataAvailable(HINTERNET_T r, LPDWORD available) {
        static auto fn = ResolveXor<Fn_WinHttpQueryDataAvailable>(WinHttp(), XorStr("WinHttpQueryDataAvailable"));
        return fn ? fn(r, available) : FALSE;
    }
    inline BOOL WINAPI WinHttpReadData(HINTERNET_T r, LPVOID buf, DWORD size, LPDWORD read) {
        static auto fn = ResolveXor<Fn_WinHttpReadData>(WinHttp(), XorStr("WinHttpReadData"));
        return fn ? fn(r, buf, size, read) : FALSE;
    }
    inline BOOL WINAPI WinHttpCloseHandle(HINTERNET_T h) {
        static auto fn = ResolveXor<Fn_WinHttpCloseHandle>(WinHttp(), XorStr("WinHttpCloseHandle"));
        return fn ? fn(h) : FALSE;
    }
    inline BOOL WINAPI WinHttpSetOption(HINTERNET_T h, DWORD opt, LPVOID buf, DWORD len) {
        static auto fn = ResolveXor<Fn_WinHttpSetOption>(WinHttp(), XorStr("WinHttpSetOption"));
        return fn ? fn(h, opt, buf, len) : FALSE;
    }

    // ─── WinINet ─────────────────────────────────────────────────────────────
    using Fn_InternetOpenA = HINTERNET_T(WINAPI*)(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
    using Fn_InternetOpenUrlA = HINTERNET_T(WINAPI*)(HINTERNET_T, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
    using Fn_InternetReadFile = BOOL(WINAPI*)(HINTERNET_T, LPVOID, DWORD, LPDWORD);
    using Fn_InternetCloseHandle = BOOL(WINAPI*)(HINTERNET_T);

    inline HINTERNET_T WINAPI InternetOpenA(LPCSTR agent, DWORD access, LPCSTR proxy, LPCSTR bypass, DWORD flags) {
        static auto f = ResolveXor<Fn_InternetOpenA>(WinINet(), XorStr("InternetOpenA"));
        return f ? f(agent, access, proxy, bypass, flags) : nullptr;
    }
    inline HINTERNET_T WINAPI InternetOpenUrlA(HINTERNET_T h, LPCSTR url, LPCSTR headers, DWORD headersLen, DWORD flags, DWORD_PTR ctx) {
        static auto f = ResolveXor<Fn_InternetOpenUrlA>(WinINet(), XorStr("InternetOpenUrlA"));
        return f ? f(h, url, headers, headersLen, flags, ctx) : nullptr;
    }
    inline BOOL WINAPI InternetReadFile(HINTERNET_T h, LPVOID buf, DWORD size, LPDWORD read) {
        static auto f = ResolveXor<Fn_InternetReadFile>(WinINet(), XorStr("InternetReadFile"));
        return f ? f(h, buf, size, read) : FALSE;
    }
    inline BOOL WINAPI InternetCloseHandle(HINTERNET_T h) {
        static auto f = ResolveXor<Fn_InternetCloseHandle>(WinINet(), XorStr("InternetCloseHandle"));
        return f ? f(h) : FALSE;
    }
}

// Redireciona símbolos Win32 → wrappers (some do IAT neste TU).
#ifndef STEALTH_API_NO_MACROS
#define ReadProcessMemory          StealthApi::ReadProcessMemory
#define WriteProcessMemory         StealthApi::WriteProcessMemory
#define OpenProcess                StealthApi::OpenProcess
#define VirtualAllocEx             StealthApi::VirtualAllocEx
#define VirtualFreeEx              StealthApi::VirtualFreeEx
#define VirtualQueryEx             StealthApi::VirtualQueryEx
#define VirtualProtect             StealthApi::VirtualProtect
#define CreateRemoteThread         StealthApi::CreateRemoteThread
#define DuplicateHandle            StealthApi::DuplicateHandle
#define GetAsyncKeyState           StealthApi::GetAsyncKeyState
#define SendInput                  StealthApi::SendInput
#define SetWindowDisplayAffinity   StealthApi::SetWindowDisplayAffinity
#define GetWindowDisplayAffinity   StealthApi::GetWindowDisplayAffinity
#define OpenProcessToken           StealthApi::OpenProcessToken
#define LookupPrivilegeValueA      StealthApi::LookupPrivilegeValueA
#define AdjustTokenPrivileges      StealthApi::AdjustTokenPrivileges
#if defined(__WINHTTP_H__)
#define WinHttpOpen                StealthApi::WinHttpOpen
#define WinHttpConnect             StealthApi::WinHttpConnect
#define WinHttpOpenRequest         StealthApi::WinHttpOpenRequest
#define WinHttpAddRequestHeaders   StealthApi::WinHttpAddRequestHeaders
#define WinHttpSendRequest         StealthApi::WinHttpSendRequest
#define WinHttpReceiveResponse     StealthApi::WinHttpReceiveResponse
#define WinHttpQueryDataAvailable  StealthApi::WinHttpQueryDataAvailable
#define WinHttpReadData            StealthApi::WinHttpReadData
#define WinHttpCloseHandle         StealthApi::WinHttpCloseHandle
#define WinHttpSetOption           StealthApi::WinHttpSetOption
#endif
#if defined(_WININET_)
#define InternetOpenA              StealthApi::InternetOpenA
#define InternetOpenUrlA           StealthApi::InternetOpenUrlA
#define InternetReadFile           StealthApi::InternetReadFile
#define InternetCloseHandle        StealthApi::InternetCloseHandle
#endif
#endif

#endif // STEALTH_API_HPP
