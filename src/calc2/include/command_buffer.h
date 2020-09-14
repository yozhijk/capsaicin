#pragma once

#include "common.h"

namespace calc2
{
class Program;
class CommandAllocator;

struct DispatchDim
{
    uint32_t x = 1;
    uint32_t y = 1;
    uint32_t z = 1;
};

class CommandBuffer
{
public:
    virtual ~CommandBuffer() noexcept = 0;

    virtual void Reset(CommandAllocator& command_allocator)         = 0;
    virtual void Dispatch(const DispatchDim& dim, Program& program) = 0;
    virtual void Close()                                            = 0;
};

inline CommandBuffer::~CommandBuffer()
{
}
}  // namespace calc2