#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>

static const wchar_t* g_targetProcesses[] = {
    L"HD-Player.exe",
    L"Bluestacks.exe"
};

static const wchar_t* g_bsFlavors[] = {
    L"BlueStacks_nxt",
    L"BlueStacks_msi5",
    L"BlueStacks_msi2",
    L"BlueStacks"
};

static const wchar_t* g_gamePackage = L"com.dts.freefireth";

static bool FileExists(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool InjectDll(DWORD pid, const wchar_t* dllPath) {
    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProc)
        return false;

    const size_t pathBytes = (lstrlenW(dllPath) + 1) * sizeof(wchar_t);

    LPVOID remoteMem = VirtualAllocEx(hProc, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { CloseHandle(hProc); return false; }

    if (!WriteProcessMemory(hProc, remoteMem, dllPath, pathBytes, nullptr)) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadLib) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remoteMem, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 20000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProc);

    return exitCode != 0;
}

struct FoundProcess {
    DWORD pid;
    int nameIndex;
};

static bool FindTargetProcesses(FoundProcess* out, int maxOut, int* outCount) {
    *outCount = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            for (int i = 0; i < ARRAYSIZE(g_targetProcesses); ++i) {
                if (lstrcmpiW(pe.szExeFile, g_targetProcesses[i]) == 0) {
                    if (*outCount < maxOut) {
                        out[*outCount].pid = pe.th32ProcessID;
                        out[*outCount].nameIndex = i;
                        ++(*outCount);
                    }
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return *outCount > 0;
}

static bool AskRetry(const wchar_t* text, const wchar_t* title) {
    return MessageBoxW(nullptr, text, title, MB_RETRYCANCEL | MB_ICONWARNING) == IDRETRY;
}

static bool FindBlueStacksDir(std::wstring& outDir) {
    wchar_t buf[512]{};
    GetLogicalDriveStringsW(ARRAYSIZE(buf), buf);

    const wchar_t* p = buf;
    while (*p) {
        std::wstring drive = p;
        if (!drive.empty() && drive.back() == L'\\')
            drive.pop_back();

        for (const wchar_t* flavor : g_bsFlavors) {
            std::wstring dir = drive + L"\\Program Files\\" + flavor;
            std::wstring exe = dir + L"\\HD-Player.exe";
            if (FileExists(exe.c_str())) {
                outDir = dir;
                return true;
            }
        }
        p += lstrlenW(p) + 1;
    }
    return false;
}

static bool StartEmulator(const std::wstring& bsDir) {
    std::wstring exe = bsDir + L"\\HD-Player.exe";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring cmdLine = L"\"" + exe + L"\"";

    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
        0, nullptr, bsDir.c_str(), &si, &pi);

    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok != FALSE;
}

static bool RunAdb(const std::wstring& adbPath, const std::wstring& args,
    std::string& output, DWORD timeoutMs) {
    output.clear();

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;

    PROCESS_INFORMATION pi{};

    std::wstring cmdLine = L"\"" + adbPath + L"\" " + args;

    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(wr);

    if (!ok) {
        CloseHandle(rd);
        return false;
    }

    WaitForSingleObject(pi.hProcess, timeoutMs);

    char chunk[4096];
    DWORD bytesRead = 0;
    while (ReadFile(rd, chunk, sizeof(chunk), &bytesRead, nullptr) && bytesRead > 0)
        output.append(chunk, bytesRead);

    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}



static bool TrimmedEquals(const std::string& s, const char* expected) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.compare(b, e - b + 1, expected) == 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    wchar_t dllPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
    if (!lastSlash) {
        MessageBoxW(nullptr, L"Failed to resolve launcher path.", L"WBR BODS Launcher", MB_ICONERROR);
        return 1;
    }
    *lastSlash = L'\0';
    wcscat_s(dllPath, MAX_PATH, L"\\WBR BODS.dll");

    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"WBR BODS.dll was not found next to launcher.exe.", L"WBR BODS Launcher", MB_ICONERROR);
        return 1;
    }

    std::wstring bsDir;
    if (!FindBlueStacksDir(bsDir)) {
        MessageBoxW(nullptr, L"BlueStacks installation was not found.\nInstall BlueStacks 5 in 'Program Files'.", L"WBR BODS Launcher", MB_ICONERROR);
        return 1;
    }

    std::wstring adbPath = bsDir + L"\\HD-Adb.exe";
    if (!FileExists(adbPath.c_str()))
        adbPath = bsDir + L"\\Engine\\ADB\\adb.exe";
    if (!FileExists(adbPath.c_str())) {
        MessageBoxW(nullptr, L"HD-Adb.exe was not found inside the BlueStacks folder.", L"WBR BODS Launcher", MB_ICONERROR);
        return 1;
    }

    FoundProcess targets[16];
    int count = 0;

    if (!FindTargetProcesses(targets, 16, &count)) {
        if (!StartEmulator(bsDir)) {
            MessageBoxW(nullptr, L"Failed to start BlueStacks.\nTry running launcher.exe as Administrator.", L"WBR BODS Launcher", MB_ICONERROR);
            return 1;
        }
    }

    for (;;) {
        if (FindTargetProcesses(targets, 16, &count))
            break;

        Sleep(1000);
    }

    std::string out;

    bool booted = false;
    for (int elapsed = 0; elapsed < 180; ++elapsed) {
        if (RunAdb(adbPath, L"shell getprop sys.boot_completed", out, 5000) &&
            TrimmedEquals(out, "1")) {
            booted = true;
            break;
        }
        Sleep(1000);
    }

    if (!booted) {
        if (!AskRetry(L"BlueStacks did not finish booting in time.\n\nRetry?", L"WBR BODS Launcher"))
            return 1;
    }

    bool gameRunning = false;
    for (int attempt = 0; attempt < 4 && !gameRunning; ++attempt) {
        RunAdb(adbPath,
            std::wstring(L"shell monkey -p ") + g_gamePackage +
            L" -c android.intent.category.LAUNCHER 1",
            out, 15000);

        for (int waited = 0; waited < 30; ++waited) {
            if (RunAdb(adbPath, std::wstring(L"shell pidof ") + g_gamePackage, out, 5000) &&
                !out.empty()) {
                gameRunning = true;
                break;
            }
            Sleep(1000);
        }
    }

    if (!gameRunning) {
        if (!AskRetry(L"The game (Free Fire) did not start inside BlueStacks.\nMake sure the game is installed, then retry.", L"WBR BODS Launcher"))
            return 1;
    }

    for (;;) {
        if (!FindTargetProcesses(targets, 16, &count)) {
            if (!AskRetry(L"BlueStacks closed unexpectedly. Retry?", L"WBR BODS Launcher"))
                return 1;
            continue;
        }

        bool allOk = true;
        for (int i = 0; i < count; ++i) {
            if (!InjectDll(targets[i].pid, dllPath))
                allOk = false;
        }

        if (allOk)
            return 0;

        if (!AskRetry(L"Injection failed. Make sure:\n- You run launcher.exe as Administrator.\n- Your antivirus did not block the launcher.\n\nRetry?", L"WBR BODS Launcher"))
            return 1;
    }
}
