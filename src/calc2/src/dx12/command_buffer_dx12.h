#pragma once

#include "command_buffer.h"
#include "device.h"
#include "src/dx12/dx12.h"

namespace calc2
{
class DeviceDX12;
class CommandBufferDX12 : public CommandBuffer
{
public:
    CommandBufferDX12(DeviceDX12& device, ComPtr<ID3D12GraphicsCommandList> cmd_list)
        : cmd_list_(cmd_list), device_(device)
    {
    }
    CommandBufferDX12(const CommandBufferDX12&) = delete;
    CommandBufferDX12& operator=(const CommandBufferDX12&) = delete;

    ~CommandBufferDX12() noexcept = default;

    operator ID3D12GraphicsCommandList*() { return cmd_list_.Get(); }

    void Reset(CommandAllocator& command_allocator) override;
    void Dispatch(const DispatchDim& dim, Program& program) override;
    void Close() override;

private:
    DeviceDX12&                       device_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
};

}  // namespace calc2