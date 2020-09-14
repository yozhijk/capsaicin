#pragma once

#include "common.h"

namespace calc2
{
class Buffer;
class Image;
class Program
{
public:
    virtual ~Program() noexcept                                   = 0;
    virtual void SetConstantBuffer(uint32_t slot, Buffer& buffer) = 0;
    virtual void SetConstants(void* data, size_t size)            = 0;
    virtual void SetBuffer(uint32_t slot, Buffer& buffer)         = 0;
    virtual void SetImage(uint32_t slot, Image& image)            = 0;
    virtual void SetSampledImage(uint32_t slot, Image& image)     = 0;
};

inline Program::~Program()
{
}
}  // namespace calc2