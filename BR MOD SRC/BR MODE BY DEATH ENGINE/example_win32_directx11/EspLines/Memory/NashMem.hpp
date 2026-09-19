#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>
#include <vector>
#include <string>
#include <iostream>
#include <TlHelp32.h>
#include <tchar.h>
#include <winternl.h>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <algorithm>
#include <mutex>

std::string MemoryLogs;

typedef NTSTATUS(NTAPI* _NtReadVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    SIZE_T NumberOfBytesToRead,
    PSIZE_T NumberOfBytesRead
    );

typedef NTSTATUS(NTAPI* _NtWriteVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    SIZE_T NumberOfBytesToWrite,
    PSIZE_T NumberOfBytesWritten
    );

typedef NTSTATUS(NTAPI* _NtProtectVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID* BaseAddress,
    PSIZE_T NumberOfBytesToProtect,
    ULONG NewAccessProtection,
    PULONG OldAccessProtection
    );

class MemoryNash
{
private:
    _NtReadVirtualMemory pNtReadVirtualMemory = nullptr;
    _NtWriteVirtualMemory pNtWriteVirtualMemory = nullptr;
    _NtProtectVirtualMemory pNtProtectVirtualMemory = nullptr;

    std::unordered_map<std::wstring, DWORD> processCache;
    std::unordered_map<std::string, std::vector<DWORD_PTR>> patternCache;
    std::atomic<bool> isScanning{ false };
    std::mutex cacheMutex;

public:
    MemoryNash() {
        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        if (hNtdll)
        {
            pNtReadVirtualMemory = (_NtReadVirtualMemory)GetProcAddress(hNtdll, "NtReadVirtualMemory");
            pNtWriteVirtualMemory = (_NtWriteVirtualMemory)GetProcAddress(hNtdll, "NtWriteVirtualMemory");
            pNtProtectVirtualMemory = (_NtProtectVirtualMemory)GetProcAddress(hNtdll, "NtProtectVirtualMemory");
        }
    }

    DWORD ProcessId = 0;
    HANDLE ProcessHandle = nullptr;

    struct MEMORY_REGION
    {
        DWORD_PTR dwBaseAddr;
        DWORD_PTR dwMemorySize;
        DWORD protection;
    };

    int GetPid(const wchar_t* procname)
    {
        if (procname == nullptr) return 0;

        std::wstring procStr(procname);
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = processCache.find(procStr);
            if (it != processCache.end() && it->second != 0) {
                return it->second;
            }
        }

        DWORD pid = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, procname) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &pe));
        }

        CloseHandle(hSnap);

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            processCache[procStr] = pid;
        }

        return pid;
    }

    const char* GetEmulatorRunning()
    {
        static const std::vector<std::wstring> emulators = {
            L"HD-Player.exe",
            L"LdVBoxHeadless.exe",
            L"MEmuHeadless.exe",
            L"AndroidProcess.exe",
            L"aow_exe.exe",
            L"NoxVMHandle.exe"
        };

        for (const auto& emulator : emulators) {
            if (GetPid(emulator.c_str()) != 0) {
                static char result[256];
                size_t converted;
                wcstombs_s(&converted, result, emulator.c_str(), sizeof(result));
                return result;
            }
        }
        return nullptr;
    }

    BOOL AttackProcess(const char* procname)
    {
        if (ProcessHandle != nullptr) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(ProcessHandle, &exitCode) && exitCode == STILL_ACTIVE) {
                return TRUE;
            }
            CloseHandle(ProcessHandle);
            ProcessHandle = nullptr;
            ProcessId = 0;
        }

        if (!procname) return FALSE;

        int size = MultiByteToWideChar(CP_UTF8, 0, procname, -1, NULL, 0);
        if (size == 0) return FALSE;

        std::vector<wchar_t> wideProcName(size);
        MultiByteToWideChar(CP_UTF8, 0, procname, -1, wideProcName.data(), size);

        DWORD ProcId = GetPid(wideProcName.data());
        if (ProcId == 0) return FALSE;

        ProcessId = ProcId;
        ProcessHandle = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, 0, ProcessId);

        if (ProcessHandle == nullptr || ProcessHandle == INVALID_HANDLE_VALUE) {
            ProcessHandle = nullptr;
            ProcessId = 0;
            return FALSE;
        }

        return TRUE;
    }

    bool FastFindPattern(DWORD_PTR StartRange, DWORD_PTR EndRange,
        const std::vector<BYTE>& pattern,
        std::vector<DWORD_PTR>& results)
    {
        if (isScanning.load() || !ProcessHandle) return false;
        isScanning.store(true);

        std::vector<MEMORY_REGION> regions;
        GetMemoryRegions(StartRange, EndRange, regions);

        if (regions.empty()) {
            isScanning.store(false);
            return false;
        }

        std::vector<DWORD_PTR> tempResults;

        for (const auto& region : regions) {
            ScanRegion(region, pattern, tempResults);
        }

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            results = std::move(tempResults);
        }

        isScanning.store(false);
        return !results.empty();
    }

    void GetMemoryRegions(DWORD_PTR StartRange, DWORD_PTR EndRange,
        std::vector<MEMORY_REGION>& regions)
    {
        if (!ProcessHandle) return;

        MEMORY_BASIC_INFORMATION mbi;
        DWORD_PTR currentAddress = StartRange;

        while (currentAddress < EndRange &&
            VirtualQueryEx(ProcessHandle, (LPCVOID)currentAddress, &mbi, sizeof(mbi)))
        {
            if (mbi.State == MEM_COMMIT &&
                !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_READONLY)))
            {
                regions.push_back({
                    (DWORD_PTR)mbi.BaseAddress,
                    mbi.RegionSize,
                    mbi.Protect
                    });
            }

            if (mbi.RegionSize == 0) break;
            currentAddress = (DWORD_PTR)mbi.BaseAddress + mbi.RegionSize;

            if (currentAddress < (DWORD_PTR)mbi.BaseAddress) break; // Overflow protection
        }
    }

    void ScanRegion(const MEMORY_REGION& region, const std::vector<BYTE>& pattern,
        std::vector<DWORD_PTR>& results)
    {
        if (!ProcessHandle || pattern.empty() || region.dwMemorySize < pattern.size()) return;
        if (region.dwMemorySize > 0x10000000) return;

        std::vector<BYTE> buffer(region.dwMemorySize);
        SIZE_T bytesRead = 0;

        if (!ReadProcessMemory(ProcessHandle, (LPCVOID)region.dwBaseAddr,
            buffer.data(), region.dwMemorySize, &bytesRead) || bytesRead < pattern.size()) {
            return;
        }

        for (DWORD_PTR i = 0; i <= bytesRead - pattern.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < pattern.size(); ++j) {
                if (pattern[j] != 0xCC && buffer[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                results.push_back(region.dwBaseAddr + i);
            }
        }
    }

    bool ReplacePattern(DWORD_PTR dwStartRange, DWORD_PTR dwEndRange,
        const std::vector<BYTE>& searchPattern,
        const std::vector<BYTE>& replacePattern)
    {
        if (!ProcessHandle || searchPattern.size() != replacePattern.size() || searchPattern.empty()) {
            return false;
        }

        std::string patternKey(reinterpret_cast<const char*>(searchPattern.data()), searchPattern.size());
        std::vector<DWORD_PTR> addresses;

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            patternCache.erase(patternKey);
        }

        if (!FastFindPattern(dwStartRange, dwEndRange, searchPattern, addresses)) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            patternCache[patternKey] = addresses;
        }

        DWORD_PTR address = addresses[0];
        DWORD oldProtect;
        VirtualProtectEx(ProcessHandle, (LPVOID)address, replacePattern.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
        BOOL result = WriteProcessMemory(ProcessHandle, (LPVOID)address, replacePattern.data(), replacePattern.size(), 0);

        return result != 0;
    }

    void ReWrite(const std::string& type, DWORD_PTR dwStartRange, DWORD_PTR dwEndRange,
        const std::vector<BYTE>& search, const std::vector<BYTE>& replace)
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator)) {
            MemoryLogs = type + ": Process not found";
            return;
        }

        MemoryLogs = "Applying - " + type;
        bool Status = ReplacePattern(dwStartRange, dwEndRange, search, replace);

        MemoryLogs = Status ? (type + " - Enabled!") : (type + " : Failed to Enable!");
    }

    bool PatchBytesAtOffsetsForPattern(DWORD_PTR dwStartRange, DWORD_PTR dwEndRange,
        const std::vector<BYTE>& searchPattern,
        const std::pair<size_t, BYTE>* patches, size_t patchCount)
    {
        if (!ProcessHandle || searchPattern.empty() || !patches || patchCount == 0)
            return false;

        std::vector<DWORD_PTR> addresses;
        if (!FastFindPattern(dwStartRange, dwEndRange, searchPattern, addresses) || addresses.empty())
            return false;

        DWORD_PTR base = addresses[0];
        for (size_t i = 0; i < patchCount; ++i) {
            size_t off = patches[i].first;
            BYTE b = patches[i].second;
            DWORD OldProtect = 0;
            VirtualProtectEx(ProcessHandle, (LPVOID)(base + off), 1, PAGE_EXECUTE_READWRITE, &OldProtect);
            WriteProcessMemory(ProcessHandle, (LPVOID)(base + off), &b, 1, nullptr);
        }
        return true;
    }

    void ClearCache() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        patternCache.clear();
        processCache.clear();
    }

    ~MemoryNash() {
        if (ProcessHandle) {
            CloseHandle(ProcessHandle);
            ProcessHandle = nullptr;
        }
    }

    void CameraLeft()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "CameraLeft: Emulator not found";
            Beep(330, 200);
            return;
        }
        // Padr�es corrigidos para terem o mesmo tamanho
        std::vector<BYTE> search = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };
        std::vector<BYTE> replace = {
            0x00, 0x00, 0x00, 0x00, 0x22, 0x9e, 0x93, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };

        MemoryLogs = "Applying CameraLeft...";
        bool success = ReplacePattern(0x00000000, 0x7FFFFFFF, search, replace);
        if (success)
        {
            MemoryLogs = "CameraLeft - Enabled!";
            Beep(530, 150);
            Sleep(50);
            Beep(630, 150);
        }
        else
        {
            MemoryLogs = "CameraLeft: Pattern not found";
            Beep(330, 300);
        }
    }

    void CameraLeftDisable()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "CameraLeft Disable: Emulator not found";
            Beep(330, 200);
            return;
        }
        // Padr�es corrigidos para terem o mesmo tamanho
        std::vector<BYTE> replace = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };
        std::vector<BYTE> search = {
            0x00, 0x00, 0x00, 0x00, 0x22, 0x9e, 0x93, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };

        MemoryLogs = "Disabling CameraLeft...";
        bool success = ReplacePattern(0x00000000, 0x7FFFFFFF, search, replace);
        if (success)
        {
            MemoryLogs = "CameraLeft - Disabled!";
            Beep(430, 150);
            Sleep(50);
            Beep(330, 150);
        }
        else
        {
            MemoryLogs = "CameraLeft Disable: Pattern not found";
            Beep(330, 300);
        }
    }

    void CameraRigth()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "CameraRigth: Emulator not found";
            Beep(330, 200);
            return;
        }
        // Padr�es corrigidos para terem o mesmo tamanho
        std::vector<BYTE> search = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };
        std::vector<BYTE> replace = {
            0x00, 0x00, 0x00, 0x00, 0x22, 0x9e, 0x93, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };

        MemoryLogs = "Applying CameraRigth...";
        bool success = ReplacePattern(0x00000000, 0x7FFFFFFF, search, replace);
        if (success)
        {
            MemoryLogs = "CameraRigth - Enabled!";
            Beep(530, 150);
            Sleep(50);
            Beep(630, 150);
        }
        else
        {
            MemoryLogs = "CameraRigth: Pattern not found";
            Beep(330, 300);
        }
    }

    void CameraRigthDisable()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "CameraRigth Disable: Emulator not found";
            Beep(330, 200);
            return;
        }
        // Padr�es corrigidos para terem o mesmo tamanho
        std::vector<BYTE> replace = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };
        std::vector<BYTE> search = {
            0x00, 0x00, 0x00, 0x00, 0x22, 0x9e, 0x93, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0xff
        };

        MemoryLogs = "Disabling CameraRigth...";
        bool success = ReplacePattern(0x00000000, 0x7FFFFFFF, search, replace);
        if (success)
        {
            MemoryLogs = "CameraRigth - Disabled!";
            Beep(430, 150);
            Sleep(50);
            Beep(330, 150);
        }
        else
        {
            MemoryLogs = "CameraRigth Disable: Pattern not found";
            Beep(330, 300);
        }
    }

    void SpeedHack()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "SpeedHack: Emulator not found";
            Beep(330, 200);
            return;
        }
        // Padr�es corrigidos para terem o mesmo tamanho
        std::vector<BYTE> search = {
            0x01, 0x00, 0x00, 0x00, 0x02, 0x2b, 0x07, 0x3d
        };
        std::vector<BYTE> replace = {
            0x01, 0x00, 0x00, 0x00, 0x92, 0xe4, 0x70, 0x3d
        };

        MemoryLogs = "Applying SpeedHack...";
        bool success = ReplacePattern(0x00000000, 0x7FFFFFFF, search, replace);
        if (success)
        {
            MemoryLogs = "SpeedHack - Enabled!";
            Beep(530, 150);
            Sleep(50);
            Beep(630, 150);
        }
        else
        {
            MemoryLogs = "SpeedHack: Pattern not found";
            Beep(330, 300);
        }
    }

    void SpeedHackDisable()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "SpeedHack Disable: Emulator not found";
            Beep(330, 200);
            return;
        }
        // Padr�es corrigidos para terem o mesmo tamanho
        std::vector<BYTE> replace = {
            0x01, 0x00, 0x00, 0x00, 0x02, 0x2b, 0x07, 0x3d
        };
        std::vector<BYTE> search = {
            0x01, 0x00, 0x00, 0x00, 0x92, 0xe4, 0x70, 0x3d
        };

        MemoryLogs = "Disabling SpeedHack...";
        bool success = ReplacePattern(0x00000000, 0x7FFFFFFF, search, replace);
        if (success)
        {
            MemoryLogs = "SpeedHack - Disabled!";
            Beep(430, 150);
            Sleep(50);
            Beep(330, 150);
        }
        else
        {
            MemoryLogs = "SpeedHack Disable: Pattern not found";
            Beep(330, 300);
        }
    }

    std::vector<DWORD_PTR> WallhackAddresses;
    bool WallhackLoaded = false;

    bool WallHackScan()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "Wall Hack: Emulator Not Found!";
            Beep(330, 200);
            return false;
        }

        MemoryLogs = "Wall Hack: Scanning...";
        Beep(530, 100);

        std::vector<BYTE> SearchWallhack = {
            0x3F, 0xAE, 0x47, 0x81, 0x3F, 0xAE, 0x47, 0x81, 0x3F, 0xAE, 0x47, 0x81, 0x3F, 0xEF, 0xFF, 0x7F, 0x3F, 0xAE, 0x47, 0x81, 0x3F
        };

        WallhackAddresses.clear();
        WallhackLoaded = false;

        std::vector<DWORD_PTR> tempResults;
        if (!FastFindPattern(0x0000000000000000, 0x00007FFFFFFFFFFF, SearchWallhack, tempResults) || tempResults.empty())
        {
            MemoryLogs = "Wall Hack: Scan Failed!";
            Beep(330, 300);
            return false;
        }

        WallhackAddresses = tempResults;
        WallhackLoaded = true;

        MemoryLogs = "Wall Hack: Loaded! (" + std::to_string(WallhackAddresses.size()) + " addresses)";
        Beep(430, 150);
        Sleep(50);
        Beep(530, 150);
        return true;
    }

    void WallHackON()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "Wall Hack: Emulator Not Found!";
            return;
        }

        if (!WallhackLoaded || WallhackAddresses.empty())
        {
            if (!WallHackScan()) return;
        }

        std::vector<BYTE> replace = {
            0x3F, 0xAE, 0x47, 0x81, 0x3F, 0xAE, 0x47, 0x81, 0xBF, 0xAE, 0x47, 0x81, 0x3F, 0xEF, 0xFF, 0x7F, 0x3F, 0xAE, 0x47, 0x81, 0x3F
        };

        MemoryLogs = "Wall Hack: Activating...";
        int successCount = 0;

        for (const auto& address : WallhackAddresses)
        {
            DWORD oldProtect;
            VirtualProtectEx(ProcessHandle, (LPVOID)address, replace.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
            if (WriteProcessMemory(ProcessHandle, (LPVOID)address, replace.data(), replace.size(), nullptr))
                successCount++;
        }

        if (successCount > 0)
        {
            MemoryLogs = "Wall Hack: Activated! (" + std::to_string(successCount) + "/" + std::to_string(WallhackAddresses.size()) + ")";
            Beep(430, 150);
            Sleep(50);
            Beep(530, 150);
        }
        else
        {
            MemoryLogs = "Wall Hack: Activate Failed!";
            Beep(330, 300);
        }
    }

    void WallHackOFF()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "Wall Hack: Emulator Not Found!";
            return;
        }

        if (!WallhackLoaded || WallhackAddresses.empty())
        {
            MemoryLogs = "Wall Hack: Load pattern first!";
            Beep(330, 300);
            return;
        }

        std::vector<BYTE> replace = {
            0x3F, 0xAE, 0x47, 0x81, 0x3F, 0xAE, 0x47, 0x81, 0x3F, 0xAE, 0x47, 0x81, 0x3F, 0xEF, 0xFF, 0x7F, 0x3F, 0xAE, 0x47, 0x81, 0x3F
        };

        MemoryLogs = "Wall Hack: Deactivating...";
        int successCount = 0;

        for (const auto& address : WallhackAddresses)
        {
            DWORD oldProtect;
            VirtualProtectEx(ProcessHandle, (LPVOID)address, replace.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
            if (WriteProcessMemory(ProcessHandle, (LPVOID)address, replace.data(), replace.size(), nullptr))
                successCount++;
        }

        if (successCount > 0)
        {
            MemoryLogs = "Wall Hack: Deactivated! (" + std::to_string(successCount) + "/" + std::to_string(WallhackAddresses.size()) + ")";
            Beep(430, 150);
            Sleep(50);
            Beep(330, 150);
        }
        else
        {
            MemoryLogs = "Wall Hack: Deactivate Failed!";
            Beep(330, 300);
        }
    }

    void SniperSwitchON()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "Sniper Switch : Emulator Not Found!";
            return;
        }

        MemoryLogs = "Sniper Switch : Applying";

        std::vector<BYTE> search = {
            0xCC, 0x00, 0x00, 0x80, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xCC, 0x00, 0x00, 0x20, 0x41, 0x00, 0x00, 0x34, 0x42, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xCC
        };
        std::pair<size_t, BYTE> patches[] = {
            { 0, 0x01 },
            { 4, 0x00 },
            { 16, 0x3F }
        };

        bool st = PatchBytesAtOffsetsForPattern(0x0000000000000000, 0x00007FFFFFFFFFFF, search, patches, 3);

        if (st) { MemoryLogs = "Sniper Switch : Successfully Injected!"; Beep(430, 150); Sleep(50); Beep(530, 150); }
        else { MemoryLogs = "Sniper Switch : Failed To Apply!"; Beep(330, 300); }
    }

    void SniperSwitchOFF()
    {
        const char* emulator = GetEmulatorRunning();
        if (!emulator || !AttackProcess(emulator))
        {
            MemoryLogs = "Sniper Switch : Emulator Not Found!";
            return;
        }

        MemoryLogs = "Sniper Switch : Removing";

        std::vector<BYTE> search = {
            0x01, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xCC, 0x00, 0x00, 0x20, 0x41, 0x00, 0x00, 0x34, 0x42, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xCC
        };
        std::pair<size_t, BYTE> patches[] = {
            { 0, 0x3F },
            { 4, 0x3E },
            { 16, 0x3F }
        };

        bool st = PatchBytesAtOffsetsForPattern(0x0000000000000000, 0x00007FFFFFFFFFFF, search, patches, 3);

        if (st) { MemoryLogs = "Sniper Switch : Successfully Removed!"; Beep(430, 150); Sleep(50); Beep(330, 150); }
        else { MemoryLogs = "Sniper Switch : Failed To Remove!"; Beep(330, 300); }
    }
};