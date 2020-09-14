#pragma once

#include "common.h"

namespace calc2
{
class Image
{
public:
    Image(const ImageDesc& desc) noexcept : desc_(desc) {}
    virtual ~Image() noexcept = 0;

    const ImageDesc& desc() const noexcept { return desc_; }

private:
    ImageDesc desc_;
};
inline Image::~Image()
{
}
}  // namespace calc2
