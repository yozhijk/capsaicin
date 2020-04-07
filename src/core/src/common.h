#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"
#include "utils/singleton.h"
#include "yecs/yecs.h"

#ifdef __GNUC__
#define PACK(__Declaration__) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK(__Declaration__) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))
#endif

using namespace spdlog;
using namespace yecs;

using std::int32_t;
using std::int8_t;
using std::uint32_t;
using std::uint8_t;

namespace capsaicin
{
inline World& world() { return Singleton<World>::instance(); };
template <typename T, typename U>
T align(T val, U a)
{
    return T((val + a - 1) / a * a);
}
template <typename T, typename U>
T ceil_divide(T val, U a)
{
    return T((val + a - 1) / a);
}
}  // namespace capsaicin
