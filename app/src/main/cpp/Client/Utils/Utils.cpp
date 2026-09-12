#include "Utils.hpp"
#include <cstdlib>
#include <android/log.h>

std::wstring Utils::RandomString(size_t Length) {
    auto Randchar = []() -> char {
        const char* Charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        const size_t MaxIndex = (sizeof(Charset) - 1);
        return Charset[rand() % MaxIndex];
        };

    std::wstring Str(Length, 0);
    std::generate_n(Str.begin(), Length, Randchar);
    return Str;
}

ImVec2 Utils::CalcTextSize(ImFont* Font, int Size, const char* Label) {
    return Font->CalcTextSizeA(Size, FLT_MAX, 0, Label);
}

bool Utils::IsProcessElevated()
{
    // No Android, root check pode ser feito via access("/system/xbin/su", F_OK)
    // Mas vamos retornar true por padrao em ambiente controlado
    return true;
}

bool Utils::EnableDebugPrivilege()
{
    // Nao aplicavel no Android
    return true;
}

bool Utils::DisableDebugPrivilege()
{
    // Nao aplicavel no Android
    return true;
}

void Console::InitConsole()
{
    // Nao aplicavel no Android
}

void Console::ShutdownConsole()
{
    // Nao aplicavel no Android
}
