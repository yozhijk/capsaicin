#include "device_dx12.h"

#include "command_allocator.h"
#include "command_buffer.h"
#include "fence.h"
#include "image.h"
#include "program.h"
#include "src/dx12/buffer_dx12.h"
#include "src/dx12/command_allocator_dx12.h"
#include "src/dx12/command_batch_dx12.h"
#include "src/dx12/command_buffer_dx12.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/fence_dx12.h"
#include "src/dx12/format_conversion.h"
#include "src/dx12/image_dx12.h"
#include "src/dx12/program_dx12.h"
#include "src/dx12/shader_compiler.h"

namespace calc2
{
DeviceDX12::DeviceDX12()
{
}

unique_ptr<Buffer> DeviceDX12::CreateBuffer(const BufferDesc& desc)
{
    debug("DeviceDX12::CreateBuffer");
    return std::make_unique<BufferDX12>(desc);
}
unique_ptr<Image> DeviceDX12::CreateImage(const ImageDesc& desc)
{
    debug("DeviceDX12::CreateImage");
    return std::make_unique<ImageDX12>(desc);
}

unique_ptr<Program> DeviceDX12::CreateProgram(const ProgramDesc& desc)
{
    debug("DeviceDX12::CreateProgram");

    auto shader = ShaderCompiler::instance().CompileFromFile(
        desc.file_name, desc.shader_model, desc.entry_point, desc.defines);

    return std::make_unique<ProgramDX12>(shader);
}

unique_ptr<CommandBuffer> DeviceDX12::CreateCommandBuffer(CommandAllocator& alloc)
{
    debug("DeviceDX12::CreateCommandBuffer");

    auto cmd_list = Dx12::instance().CreateCommandList(static_cast<CommandAllocatorDX12&>(alloc));
    cmd_list->Close();
    return std::make_unique<CommandBufferDX12>(*this, cmd_list);
}

unique_ptr<CommandAllocator> DeviceDX12::CreateCommandAllocator()
{
    debug("DeviceDX12::CreateCommandAllocator");
    return std::make_unique<CommandAllocatorDX12>(Dx12::instance().CreateCommandAllocator());
}

unique_ptr<Fence> DeviceDX12::CreateFence()
{
    debug("DeviceDX12::CreateFence");
    return std::make_unique<FenceDX12>(Dx12::instance().CreateFence(0));
}

void DeviceDX12::PushCommandBuffer(CommandBuffer& command_buffer)
{
    auto& batch           = batches_[current_batch_index_ % kMaxConcurrentBatches];
    auto  index           = batch.next_free_cb_.fetch_add(1);
    auto& cmd_buffer_dx12 = static_cast<CommandBufferDX12&>(command_buffer);

    if (index >= kMaxCommandBuffersPerBatch)
    {
        throw std::runtime_error("DeviceDX12: max number of command buffer buffers exceeded");
    }

    batch.command_buffers_[index] = static_cast<ID3D12GraphicsCommandList*>(cmd_buffer_dx12);
}

void DeviceDX12::SignalFence(Fence& fence, uint32_t value)
{
    auto& fence_dx12 = static_cast<FenceDX12&>(fence);
    Dx12::instance().command_queue()->Signal(static_cast<ID3D12Fence*>(fence_dx12), value);
}

void DeviceDX12::WaitOnFence(Fence& fence, uint32_t min_value)
{
    auto& fence_dx12 = static_cast<FenceDX12&>(fence);
    Dx12::instance().command_queue()->Wait(static_cast<ID3D12Fence*>(fence_dx12), min_value);
}

void DeviceDX12::Flush()
{
    auto& batch         = batches_[current_batch_index_ % kMaxConcurrentBatches];
    auto  command_queue = Dx12::instance().command_queue();

    if (batch.next_free_cb_ != 0)
    {
        command_queue->ExecuteCommandLists(batch.next_free_cb_, batch.command_buffers_.data());

        batch.submission_id_ = current_batch_index_ + 1;
        SignalFence(batch.fence_, current_batch_index_ + 1);
    }

    ++current_batch_index_;

    // Wait until next batch is available (CPU sync).
    auto& next_batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    next_batch.fence_.Wait(next_batch.submission_id_);
    next_batch.Reset();
}

uint32_t DeviceDX12::GetUAVHandleIndex(Image& image)
{
    auto&    batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    uint32_t index = ~0u;
    auto     it    = batch.uav_handles_.find(&image);

    if (it != batch.uav_handles_.cend())
    {
        index                      = batch.next_free_handle_++;
        batch.uav_handles_[&image] = index;
        CreateUAV(image, index);
    }
    else
    {
        index = it->second;
    }

    return index;
}

uint32_t DeviceDX12::GetSRVHandleIndex(Image& image)
{
    auto&    batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    uint32_t index = ~0u;
    auto     it    = batch.srv_handles_.find(&image);

    if (it != batch.srv_handles_.cend())
    {
        index                      = batch.next_free_handle_.fetch_add(1);
        batch.srv_handles_[&image] = index;
        CreateSRV(image, index);
    }
    else
    {
        index = it->second;
    }

    return index;
}

ID3D12DescriptorHeap* DeviceDX12::current_descriptor_heap()
{
    auto& batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    return batch.descriptor_heap_.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE DeviceDX12::GetUAVHandleGPU(Image& image)
{
    auto&    batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    uint32_t index = GetUAVHandleIndex(image);

    auto increment = Dx12::instance().device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        batch.descriptor_heap_->GetGPUDescriptorHandleForHeapStart(), index, increment);
}

D3D12_GPU_DESCRIPTOR_HANDLE DeviceDX12::GetSRVHandleGPU(Image& image)
{
    auto&    batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    uint32_t index = GetSRVHandleIndex(image);

    auto increment = Dx12::instance().device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        batch.descriptor_heap_->GetGPUDescriptorHandleForHeapStart(), index, increment);
}

D3D12_CPU_DESCRIPTOR_HANDLE DeviceDX12::GetUAVHandleCPU(Image& image)
{
    auto&    batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    uint32_t index = GetUAVHandleIndex(image);

    auto increment = Dx12::instance().device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        batch.descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), index, increment);
}

D3D12_CPU_DESCRIPTOR_HANDLE DeviceDX12::GetSRVHandleCPU(Image& image)
{
    auto&    batch = batches_[current_batch_index_ % kMaxConcurrentBatches];
    uint32_t index = GetSRVHandleIndex(image);

    auto increment = Dx12::instance().device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        batch.descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), index, increment);
}

void DeviceDX12::CreateUAV(Image& image, uint32_t index)
{
    auto& image_dx12 = static_cast<ImageDX12&>(image);

    auto uav_desc = image_dx12.uav_desc();

    dx12api().device()->CreateUnorderedAccessView(
        image_dx12, nullptr, &uav_desc, GetUAVHandleCPU(image));
}

void DeviceDX12::CreateSRV(Image& image, uint32_t index)
{
    auto& image_dx12 = static_cast<ImageDX12&>(image);

    auto srv_desc = image_dx12.srv_desc();

    dx12api().device()->CreateShaderResourceView(image_dx12, &srv_desc, GetSRVHandleCPU(image));
}
}  // namespace calc2
