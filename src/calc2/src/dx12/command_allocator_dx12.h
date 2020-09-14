#pragma once

#include "command_allocator.h"
#include "src/dx12/dx12.h"

namespace calc2
{
class CommandAllocatorDX12 : public CommandAllocator
{
public:
    CommandAllocatorDX12(ComPtr<ID3D12CommandAllocator> alloc) : cmd_alloc_(alloc) {}

    CommandAllocatorDX12(const CommandAllocatorDX12&) = delete;
    CommandAllocatorDX12& operator=(const CommandAllocatorDX12&) = delete;

    ~CommandAllocatorDX12() noexcept = default;

    operator ID3D12CommandAllocator*() const noexcept { return cmd_alloc_.Get(); }

    void AllocateCommandBuffer(CommandBuffer& cmd_buffer) override;

private:
    ComPtr<ID3D12CommandAllocator> cmd_alloc_;
};

}  // namespace calc2