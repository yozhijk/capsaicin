#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"
#include "src/dx12/shader_compiler.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
class RenderSystem : public System
{
public:
    RenderSystem(HWND hwnd);
    ~RenderSystem();
    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

private:
    static constexpr uint32_t kBackbufferCount = 2;

    void InitWindow();
    void InitMainPipeline();
    void WaitForGPUFrame(uint32_t index);

    // Per-frame GPU data.
    struct GPUFrameData
    {
        ComPtr<ID3D12CommandAllocator> command_allocator = nullptr;
        ComPtr<ID3D12GraphicsCommandList> command_list = nullptr;
        uint64_t submission_id = 0;
    };

    std::array<GPUFrameData, kBackbufferCount> gpu_frame_data_;

    HWND hwnd_;

    // Current backbuffer index.
    UINT backbuffer_index_ = 0;
    // Swapchain.
    ComPtr<IDXGISwapChain3> swapchain_ = nullptr;
    // TODO: this is debug fence, change to ringbuffer later.
    ComPtr<ID3D12Fence> frame_submission_fence_ = nullptr;
    // Render target descriptor heap for the swapchain.
    ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap_ = nullptr;
    // Render target chain.
    std::array<ComPtr<ID3D12Resource>, kBackbufferCount> backbuffers_ = {nullptr};

    //
    ComPtr<ID3D12RootSignature> root_signature_ = nullptr;
    ComPtr<ID3D12PipelineState> pipeline_state_ = nullptr;
    // Window event.
    HANDLE win32_event_ = INVALID_HANDLE_VALUE;

    uint32_t window_width_ = 0;
    uint32_t window_height_ = 0;
    uint32_t next_submission_id_ = 1;
};
}  // namespace capsaicin