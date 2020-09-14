#pragma once

#include "common.h"

namespace calc2
{
class Buffer
{
public:
    Buffer(const BufferDesc& desc) noexcept : desc_(desc) {}
    virtual ~Buffer() noexcept = 0;

    const BufferDesc& desc() const noexcept { return desc_; }

private:
    BufferDesc desc_;
};
inline Buffer::~Buffer()
{
}
}  // namespace calc2