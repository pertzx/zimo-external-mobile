#pragma once

#include <Windows.h>
#include <unordered_map>
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

class MemoryUtils {
public:
    inline static void* vmPtr = nullptr;
    inline static void* pVMAddr = nullptr;
    inline static void* cpuAddr = nullptr;

    inline static std::unordered_map<uintptr_t, uintptr_t> Cache;

    using PGMPhysReadFunc = int(__cdecl*)(void*, uintptr_t, void*, size_t);
    using VMMGetCpuByIdFunc = void* (__cdecl*)(void*, int);
    using PGMPhysGCPtr2GCPhysFunc = int(__cdecl*)(void*, uintptr_t, uintptr_t*);
    using PGMPhysSimpleWriteGCPhysFunc = int(__cdecl*)(void*, uintptr_t, void*, size_t);

    inline static PGMPhysReadFunc ogPhysRead = nullptr;
    inline static VMMGetCpuByIdFunc ogCPU = nullptr;
    inline static PGMPhysGCPtr2GCPhysFunc ogCast = nullptr;
    inline static PGMPhysSimpleWriteGCPhysFunc ogWrite = nullptr;

    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* buffer) {
        buffer->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    static int __cdecl HookedPGMPhysRead(void* pVM, uintptr_t GCPhys, void* pvBuf, size_t cbRead) {
        if (!vmPtr) {
            vmPtr = pVM;
            std::cout << "Memory initialized: " << vmPtr << std::endl;
        }
        return ogPhysRead(pVM, GCPhys, pvBuf, cbRead);
    }

    static int HookWrite(void* pVM, uintptr_t GCPhys, void* pvBuf, size_t cbRead) {
        return ogWrite(pVM, GCPhys, pvBuf, cbRead);
    }

    static int HookRead(void* pVM, uintptr_t GCPhys, void* pvBuf, size_t cbRead) {
        return ogPhysRead(pVM, GCPhys, pvBuf, cbRead);
    }

    static void* CPU(void* pVM, int cpuId) {
        return ogCPU(pVM, cpuId);
    }

    static int Cast(void* pVCpu, uintptr_t address, uintptr_t* physAddress) {
        return ogCast(pVCpu, address, physAddress);
    }

    static void Initialize(void* pVM) {
        pVMAddr = pVM;
        cpuAddr = CPU(pVM, 0);
        Cache.clear();
    }

    static constexpr uint32_t MAX_CPU = 4U;
    static constexpr uintptr_t MIN_VALID_ADDRESS = 0x1000;

    static bool Convert(uintptr_t address, uintptr_t& phys) {
        phys = 0;
        if (address <= MIN_VALID_ADDRESS) return false;

        auto it = Cache.find(address);
        if (it != Cache.end() && it->second != 0) {
            phys = it->second;
            return true;
        }

        for (uint32_t i = 0; i < MAX_CPU; ++i) {
            void* cpu = CPU(pVMAddr, i);
            if (!cpu) continue;

            uintptr_t tempPhys = 0;
            if (Cast(cpu, address, &tempPhys) == 0) {
                phys = tempPhys;
                Cache[address] = tempPhys;
                return true;
            }
        }
        return false;
    }

    template<typename T>
    bool Read(uintptr_t address, T& out) {
        uintptr_t physAddress;
        if (!Convert(address, physAddress)) return false;
        return HookRead(pVMAddr, physAddress, &out, sizeof(T)) == 0;
    }

    template<typename T>
    T Read(uintptr_t address) {
        T result{};
        Read(address, result);
        return result;
    }

    template<typename T>
    static void Write(uintptr_t address, const T& value) {
        uintptr_t physAddress;
        if (Convert(address, physAddress)) {
            HookWrite(pVMAddr, physAddress, (void*)&value, sizeof(T));
        }
    }

    template<typename T>
    static bool ReadFast2(uintptr_t address, T* data) {
        uintptr_t physAddress;
        if (!Convert(address, physAddress)) return false;
        return HookRead(pVMAddr, physAddress, data, sizeof(T)) == 0;
    }

    template<typename T>
    bool ReadArray(uintptr_t address, std::vector<T>& array) {
        uintptr_t convertedAddress;
        bool result = Convert(address, convertedAddress);
        if (!result) {
            return false;
        }

        size_t size = sizeof(T) * array.size();
        DWORD status = HookRead(pVMAddr, convertedAddress, array.data(), size);

        return status == 0;
    }

    // ===== FUNCAO MELHORADA DE CONVERSAO UTF-16 PARA UTF-8 =====
    // Suporta TODOS os caracteres Unicode, incluindo suplementares
    // Acentos, kanji, chines, arabe, cirilico, simbolos, emojis, etc.
    std::string utf16_to_utf8(const std::wstring& wstr) {
        std::string str;
        
        // Trata strings vazias
        if (wstr.empty()) return "";
        
        try {
            // Calcula o tamanho necessario com tratamento de erros
            // Usa CP_UTF8 para conversao completa de Unicode
            int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
            
            if (len > 0) {
                str.resize(len - 1);
                int result = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
                
                // Validacao de sucesso
                if (result == 0) {
                    // Se falhar, tenta com WC_ERR_INVALID_CHARS para caracteres invalidos
                    len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        str.resize(len - 1);
                        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
                    } else {
                        // Fallback: converte caractere por caractere
                        str.clear();
                        for (wchar_t ch : wstr) {
                            if (ch == L'\0') break;
                            
                            // Converte cada caractere individualmente
                            char buf[4] = {0};
                            int charLen = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, buf, 4, nullptr, nullptr);
                            if (charLen > 0) {
                                str.append(buf, charLen);
                            } else {
                                // Se nao conseguir converter, usa caractere de substituicao
                                str += '?';
                            }
                        }
                    }
                }
            }
        } catch (...) {
            // Em caso de excecao, retorna string vazia
            str.clear();
        }
        
        return str;
    }

    // ===== FUNCAO MELHORADA DE LEITURA DE STRINGS =====
    // Suporta TODOS os tipos de caracteres Unicode
    // Acentos, simbolos especiais, emojis, caracteres suplementares, etc.
    std::string String(uintptr_t address, int size, bool unicode = true) {
        std::vector<byte> stringBytes(size);
        if (!ReadArray(address, stringBytes)) return "";

        std::string result;
        
        if (unicode) {
            // Trata UTF-16 com suporte COMPLETO a caracteres especiais
            std::wstring wstr;
            wstr.reserve(size / 2 + 1);
            
            // Copia bytes como wchar_t, mantendo TODOS os caracteres validos
            for (int i = 0; i < size; i += 2) {
                if (i + 1 < size) {
                    wchar_t ch = *reinterpret_cast<wchar_t*>(&stringBytes[i]);
                    
                    // Para no primeiro nulo
                    if (ch == L'\0') break;
                    
                    // Aceita TODOS os caracteres Unicode validos
                    // Incluindo caracteres suplementares (surrogates)
                    wstr += ch;
                }
            }
            
            result = utf16_to_utf8(wstr);
        }
        else {
            // Trata ANSI com suporte a caracteres especiais
            result = std::string(reinterpret_cast<char*>(stringBytes.data()), size);
        }

        // Remove apenas caracteres nulos
        size_t nullPos = result.find('\0');
        if (nullPos != std::string::npos) {
            result = result.substr(0, nullPos);
        }
        
        // Remove APENAS caracteres de controle invalidos (< 32)
        // MANTEM espacos e TODOS os caracteres especiais validos
        result.erase(std::remove_if(result.begin(), result.end(), 
            [](unsigned char c) { return c < 32 && c != ' ' && c != '\t' && c != '\n'; }), result.end());
        
        // Trata espaços múltiplos consecutivos
        std::string cleaned;
        bool lastWasSpace = false;
        for (char c : result) {
            if (c == ' ' || c == '\t') {
                if (!lastWasSpace) {
                    cleaned += ' ';
                    lastWasSpace = true;
                }
            } else {
                cleaned += c;
                lastWasSpace = false;
            }
        }
        
        // Remove espaços no início e fim
        size_t start = cleaned.find_first_not_of(' ');
        size_t end = cleaned.find_last_not_of(' ');
        if (start != std::string::npos) {
            result = cleaned.substr(start, end - start + 1);
        } else {
            result = cleaned;
        }

        return result;
    }
};
inline MemoryUtils Mem;
