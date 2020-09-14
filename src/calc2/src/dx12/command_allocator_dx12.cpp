#include "command_allocator_dx12.h"

#include "src/dx12/command_buffer_dx12.h"

namespace calc2
{
void CommandAllocatorDX12::AllocateCommandBuffer(CommandBuffer& cmd_buffer)
{
    auto& cmd_buffer_dx12 = static_cast<CommandBufferDX12&>(cmd_buffer);
    ((ID3D12GraphicsCommandList*)(cmd_buffer_dx12))->Reset(*this, nullptr);
}
}  // namespace calc2