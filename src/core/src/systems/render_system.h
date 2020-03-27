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

    // Push command list for execution.
    void PushCommandList(ID3D12CommandList* command_list);
    // Add resource to the autorealease pool, it will be freed
    // when all command buffers are finished execution for the current GPU frame.
    void AddAutoreleaseResource(ComPtr<ID3D12Resource> resource);

    // Properties.
    static constexpr uint32_t num_gpu_frames_in_flight() { return kNumGPUFramesInFlight; }
    static constexpr uint32_t constant_buffer_alignment() { return kConstantBufferAlignment; }
    uint32_t window_width() const { return window_width_; }
    uint32_t window_height() const { return window_height_; }
    uint32_t current_gpu_frame_index() const { return current_gpu_frame_index_; }
    ID3D12CommandAllocator* current_frame_command_allocator() { return gpu_frame_data_[current_gpu_frame_index_].command_allocator.Get(); }
    HWND hwnd() { return hwnd_; }

private:
    static constexpr uint32_t kNumGPUFramesInFlight = 2;
    static constexpr uint32_t kConstantBufferAlignment = 256;
    static constexpr uint32_t kMaxCommandBuffersPerFrame = 1024;

    void InitWindow();
    void InitMainPipeline();
    void InitRaytracingPipeline();

    void Render(float time);
    void WaitForGPUFrame(uint32_t index);
    void ExecuteCommandLists(uint32_t index);
    void Raytrace(ID3D12Resource* scene, ID3D12Resource* camera);

    // Per-frame GPU data.
    struct GPUFrameData
    {
        ComPtr<ID3D12CommandAllocator> command_allocator = nullptr;
        std::array<ID3D12CommandList*, kMaxCommandBuffersPerFrame> command_lists = {nullptr};
        std::atomic_uint32_t num_command_lists = 0;
        uint64_t submission_id = 0;

        std::vector<ComPtr<ID3D12Resource>> autorelease_pool;
    };

    std::array<GPUFrameData, kNumGPUFramesInFlight> gpu_frame_data_;

    HWND hwnd_;

    // Current backbuffer index.
    UINT current_gpu_frame_index_ = 0;
    // Swapchain.
    ComPtr<IDXGISwapChain3> swapchain_ = nullptr;
    // TODO: this is debug fence, change to ringbuffer later.
    ComPtr<ID3D12Fence> frame_submission_fence_ = nullptr;
    // Render target descriptor heap for the swapchain.
    ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap_ = nullptr;
    // UAV heap.
    ComPtr<ID3D12DescriptorHeap> uav_descriptor_heap_ = nullptr;
    // SRV descriptor heap.
    ComPtr<ID3D12DescriptorHeap> srv_descriptor_heap_ = nullptr;
    // Main commnand list.
    ComPtr<ID3D12GraphicsCommandList> command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> raytracing_command_list_ = nullptr;
    // Render target chain.
    std::array<ComPtr<ID3D12Resource>, kNumGPUFramesInFlight> backbuffers_ = {nullptr};
    // Use one UAV per render target for better interleaving.
    std::array<ComPtr<ID3D12Resource>, kNumGPUFramesInFlight> raytracing_outputs_ = {nullptr};

    // Shader tables.
    ComPtr<ID3D12Resource> raygen_shader_table = nullptr;
    ComPtr<ID3D12Resource> hitgroup_shader_table = nullptr;
    ComPtr<ID3D12Resource> miss_shader_table = nullptr;

    //
    ComPtr<ID3D12RootSignature> root_signature_ = nullptr;
    ComPtr<ID3D12PipelineState> pipeline_state_ = nullptr;

    ComPtr<ID3D12RootSignature> raytracing_root_signature_ = nullptr;
    ComPtr<ID3D12StateObject> raytracing_pipeline_state_ = nullptr;

    // Window event.
    HANDLE win32_event_ = INVALID_HANDLE_VALUE;

    uint32_t window_width_ = 0;
    uint32_t window_height_ = 0;
    uint32_t next_submission_id_ = 1;
};
}  // namespace capsaicin