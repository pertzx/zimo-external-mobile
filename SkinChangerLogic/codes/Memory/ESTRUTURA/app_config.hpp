#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP


#include <ESTRUTURA/types.hpp>
#include <string>
#include "../../XorStr.hpp"

namespace config {
    inline std::wstring default_process_name() {
        return std::wstring(XorStr(L"HD-Player.exe"));
    }
    inline std::wstring default_module_name() {
        return std::wstring(XorStr(L"BstkVMM.dll"));
    }
    inline constexpr u32     k_remote_thread_timeout_ms     = 5000;
    inline constexpr usize   k_default_dump_size            = 64;
    inline constexpr usize   k_default_string_character_cap = 256;
}

#endif
