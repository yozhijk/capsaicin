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

using namespace spdlog;
using namespace yecs;

using std::int32_t;
using std::int8_t;
using std::uint32_t;
using std::uint8_t;

namespace capsaicin
{
inline World& world() { return Singleton<World>::instance(); };
}