#pragma once

#include < atomic>
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
#include <thread>
#include <unordered_map>
#include <vector>

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

#ifdef __GNUC__
#define PACK(__Declaration__) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK(__Declaration__) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))
#endif

using namespace spdlog;

namespace calc2
{
using std::array;
using std::atomic;
using std::int32_t;
using std::int8_t;
using std::runtime_error;
using std::string;
using std::uint32_t;
using std::uint8_t;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

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
}  // namespace calc2

namespace calc2
{
/// API device is created on.
enum class DeviceAPI
{
    kD3D12
};

/// Physical type of the device.
enum class DeviceType
{
    kDiscrete,
    kIntegrated,
    kExternal
};

/// Features supported by a device.
struct DeviceFeatures
{
    bool raytracing;
};

struct DeviceSpec
{
    DeviceType     type;
    DeviceAPI      api;
    DeviceFeatures features;
};

using MatchFunc = std::function<bool(const DeviceSpec&)>;

enum class BufferType
{
    kConstant,
    kUnorderedAccess,
    kUpload,
    kReadback
};

enum class ResourceState
{
    kUnknown,
    kCopySrc,
    kCopyDst,
    kUnorderedAccess,
    kSampled
};

enum class ImageDim
{
    k1D,
    k2D,
    k3D
};

enum class ImageType
{
    kSampled,
    kUnorderedAccess,
};

enum class ImageFormat
{
    kRGBA8Unorm,
    kRGBA16Float,
    kRGBA32Float,
};

struct BufferDesc
{
    BufferType type;
    size_t     size;
};

struct ImageDesc
{
    ImageDim    dim;
    ImageType   type;
    ImageFormat format;
    uint32_t    width;
    uint32_t    height;
    uint32_t    depth;
};

struct ProgramDesc
{
    string         file_name;
    string         entry_point;
    string         shader_model;
    vector<string> defines;
};
}  // namespace calc2