#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <iostream>
#include <algorithm>

namespace ProcessKiller {

    // Função para encerrar um processo pelo nome exato
    bool KillProcessByName(const std::wstring& processName) {
        HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, NULL);
        PROCESSENTRY32W pEntry;
        pEntry.dwSize = sizeof(pEntry);
        BOOL hRes = Process32FirstW(hSnapShot, &pEntry);
        bool killed = false;
        if (hRes == TRUE) {
            do {
                if (std::wstring(pEntry.szExeFile) == processName) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, 0, pEntry.th32ProcessID);
                    if (hProcess != NULL) {
                        BOOL result = TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                        if (result) {
                            killed = true;
                        }
                    }
                }
            } while (Process32NextW(hSnapShot, &pEntry));
        }
        CloseHandle(hSnapShot);
        return killed;
    }

    // Função para encerrar todos os processos que contenham uma substring no nome
    bool KillProcessesBySubstring(const std::wstring& substring) {
        HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, NULL);
        PROCESSENTRY32W pEntry;
        pEntry.dwSize = sizeof(pEntry);
        BOOL hRes = Process32FirstW(hSnapShot, &pEntry);
        bool killed = false;
        if (hRes == TRUE) {
            do {
                std::wstring currentProcessName = pEntry.szExeFile;
                // Converter para minúsculas para comparação sem distinção de maiúsculas/minúsculas
                std::transform(currentProcessName.begin(), currentProcessName.end(), currentProcessName.begin(), ::tolower);
                std::wstring lowerSubstring = substring;
                std::transform(lowerSubstring.begin(), lowerSubstring.end(), lowerSubstring.begin(), ::tolower);

                if (currentProcessName.find(lowerSubstring) != std::wstring::npos) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, 0, pEntry.th32ProcessID);
                    if (hProcess != NULL) {
                        BOOL result = TerminateProcess(hProcess, 0);
                        CloseHandle(hProcess);
                        if (result) {
                            killed = true;
                        }
                    }
                }
            } while (Process32NextW(hSnapShot, &pEntry));
        }
        CloseHandle(hSnapShot);
        return killed;
    }

    // Função para encerrar o painel e os processos do emulador
    void UnloadAndKillProcesses() {
        // 1. Encerrar todos os processos que contenham "hd-adb" no nome
        KillProcessesBySubstring(L"HD-Adb.exe");

        // 2. Encerrar o processo do emulador "hd-player" (para cobrir casos onde o nome é exato)
        KillProcessByName(L"HD-Player.exe");
        KillProcessByName(L"HD-Adb.exe");
        KillProcessByName(L"Nox.exe");
        KillProcessByName(L"Ld9BoxHeadless.exe");
        KillProcessByName(L"MEmu.exe");
        KillProcessByName(L"AndroidProcess.exe");

        // 3. Encerrar o processo do painel (o executável principal)
        KillProcessByName(L"NashProject.exe");
    }
}
