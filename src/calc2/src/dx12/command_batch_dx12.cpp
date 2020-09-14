#include "command_batch_dx12.h"

namespace calc2
{
namespace
{
static constexpr uint32_t kNumDescriptors = 8192;
}

CommandBatchDX12::CommandBatchDX12() : fence_(Dx12::instance().CreateFence(0))
{
    descriptor_heap_ = Dx12::instance().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                             kNumDescriptors);
}

void CommandBatchDX12::Reset()
{
    next_free_handle_ = 0;
    next_free_cb_     = 0;
    uav_handles_.clear();
    srv_handles_.clear();
}
}  // namespace calc2