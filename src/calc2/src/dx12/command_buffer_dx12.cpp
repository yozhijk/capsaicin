#include "command_buffer_dx12.h"

#include "src/dx12/command_allocator_dx12.h"
#include "src/dx12/program_dx12.h"
#include "src/dx12/device_dx12.h"

namespace calc2
{
void CommandBufferDX12::Reset(CommandAllocator& command_allocator)
{
    auto& command_allocator_dx12 = static_cast<CommandAllocatorDX12&>(command_allocator);
    cmd_list_->Reset(command_allocator_dx12, nullptr);
}
void CommandBufferDX12::Dispatch(const DispatchDim& dim, Program& program)
{
    auto& program_dx12 = static_cast<ProgramDX12&>(program);

    // Bind pipeline state.
    cmd_list_->SetPipelineState(program_dx12.pipeline_state());

    // Bind descriptor heap.
    ID3D12DescriptorHeap* desc_heap[] = {device_.current_descriptor_heap()};
    cmd_list_->SetDescriptorHeaps(1, desc_heap);

    // Bind arguments.


    cmd_list_->Dispatch(dim.x, dim.y, dim.z);
}
void CommandBufferDX12::Close()
{
    cmd_list_->Close();
}
}  // namespace calc2
