#pragma once

#include "src/dx12/dx12.h"
#include "src/dx12/fence_dx12.h"

namespace calc2
{
constexpr uint32_t kMaxCommandBuffersPerBatch = 2048;

class Image;

struct CommandBatchDX12
{
    ComPtr<ID3D12DescriptorHeap>                          descriptor_heap_;
    unordered_map<Image*, uint32_t>                       uav_handles_;
    unordered_map<Image*, uint32_t>                       srv_handles_;
    array<ID3D12CommandList*, kMaxCommandBuffersPerBatch> command_buffers_;
    atomic<uint32_t>                                      next_free_handle_ = 0;
    atomic<uint32_t>                                      next_free_cb_     = 0;
    uint32_t                                              submission_id_    = 0;
    FenceDX12                                             fence_;

    CommandBatchDX12();
    void Reset();
};
}  // namespace calc2