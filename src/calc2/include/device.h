#pragma once

#include "common.h"

namespace calc2
{
class Buffer;
class Image;
class CommandAllocator;
class CommandBuffer;
class Program;
class Fence;

class Device
{
public:
    Device()              = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    virtual ~Device() noexcept       = default;

    virtual unique_ptr<Buffer>           CreateBuffer(const BufferDesc& desc)         = 0;
    virtual unique_ptr<Image>            CreateImage(const ImageDesc& desc)           = 0;
    virtual unique_ptr<Program>          CreateProgram(const ProgramDesc& desc)       = 0;
    virtual unique_ptr<CommandAllocator> CreateCommandAllocator()                     = 0;
    virtual unique_ptr<CommandBuffer>    CreateCommandBuffer(CommandAllocator& alloc) = 0;
    virtual unique_ptr<Fence>            CreateFence()                                = 0;

    virtual void PushCommandBuffer(CommandBuffer& command_buffer) = 0;
    virtual void SignalFence(Fence& fence, uint32_t value)        = 0;
    virtual void WaitOnFence(Fence& fence, uint32_t min_value)    = 0;
    virtual void Flush()                                          = 0;
};
}  // namespace calc2
