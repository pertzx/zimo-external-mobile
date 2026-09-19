#ifndef INCLUDES_HPP
#define INCLUDES_HPP


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include "StealthApi.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ESTRUTURA/app_config.hpp>
#include <ESTRUTURA/bluestacks.hpp>
#include <ESTRUTURA/gva_memory_bridge.hpp>
#include <ESTRUTURA/kernel_structs.hpp>
#include <ESTRUTURA/memory.hpp>
#include <ESTRUTURA/memory_engine.hpp>
#include <ESTRUTURA/types.hpp>

#endif
