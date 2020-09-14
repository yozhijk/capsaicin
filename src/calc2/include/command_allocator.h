#pragma once

#include "common.h"

namespace calc2
{
class CommandBuffer;
class CommandAllocator
{
public:
    virtual ~CommandAllocator() noexcept = 0;

    virtual void AllocateCommandBuffer(CommandBuffer& cmd_buffer) = 0;
};

inline CommandAllocator::~CommandAllocator()
{
}
}  // namespace calc2