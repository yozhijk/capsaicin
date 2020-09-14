#pragma once

#include "common.h"

namespace calc2
{
class Fence
{
public:
    virtual ~Fence() noexcept = 0;
};
inline Fence::~Fence()
{
}
}  // namespace calc2