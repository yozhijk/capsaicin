#pragma once

#include "device.h"
#include "src/dx12/command_batch_dx12.h"
#include "src/dx12/dx12.h"

namespace calc2
{
constexpr uint32_t kMaxConcurrentBatches      = 8;

class DeviceDX12 : public Device
{
public:
    DeviceDX12();
    DeviceDX12(const DeviceDX12&) = delete;
    DeviceDX12& operator=(const DeviceDX12&) = delete;

    ~DeviceDX12() noexcept = default;

    unique_ptr<Buffer>           CreateBuffer(const BufferDesc& desc) override;
    unique_ptr<Image>            CreateImage(const ImageDesc& desc) override;
    unique_ptr<Program>          CreateProgram(const ProgramDesc& desc) override;
    unique_ptr<CommandAllocator> CreateCommandAllocator() override;
    unique_ptr<CommandBuffer>    CreateCommandBuffer(CommandAllocator& alloc) override;
    unique_ptr<Fence>            CreateFence() override;

    void PushCommandBuffer(CommandBuffer& command_buffer) override;
    void SignalFence(Fence& fence, uint32_t value) override;
    void WaitOnFence(Fence& fence, uint32_t min_value) override;
    void Flush() override;

    D3D12_GPU_DESCRIPTOR_HANDLE GetUAVHandleGPU(Image& image);
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandleGPU(Image& image);

    D3D12_CPU_DESCRIPTOR_HANDLE GetUAVHandleCPU(Image& image);
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVHandleCPU(Image& image);

    void CreateUAV(Image& image, uint32_t index);
    void CreateSRV(Image& image, uint32_t index);

private:
    uint32_t GetUAVHandleIndex(Image& image);
    uint32_t GetSRVHandleIndex(Image& image);

    ID3D12DescriptorHeap* current_descriptor_heap();

    array<CommandBatchDX12, kMaxConcurrentBatches> batches_;
    uint32_t                                       current_batch_index_ = 0;

    friend class CommandBufferDX12;
};
}  // namespace calc2