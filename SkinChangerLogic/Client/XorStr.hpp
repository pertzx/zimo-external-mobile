#pragma once
#include "XOR.hpp"
#include <string>

#define XorStrS(str) ([]() -> std::string { return std::string(XorStr(str)); }())

#define XorW(str) ([]() -> std::wstring { \
    std::string _xs = XorStrS(str); \
    return std::wstring(_xs.begin(), _xs.end()); \
}())
