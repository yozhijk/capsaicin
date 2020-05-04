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
    void PushCommandList(ComPtr<ID3D12CommandList> command_list);
    // Add resource to the autorealease pool, it will be freed
    // when all command buffers are finished execution for the current GPU frame.
    void AddAutoreleaseResource(ComPtr<ID3D12Resource> resource);
    // Allocate a descriptor range from the current frame's descriptor heap.
    uint32_t AllocateDescriptorRange(uint32_t num_descriptors);
    // Get CPU or GPU descriptor handle in the current descriptor heap.
    D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandleCPU(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetDescriptorHandleGPU(uint32_t index);

    // Properties.
    static constexpr uint32_t num_gpu_frames_in_flight() { return kNumGPUFramesInFlight; }
    static constexpr uint32_t constant_buffer_alignment() { return kConstantBufferAlignment; }

    uint32_t window_width() const { return window_width_; }
    uint32_t window_height() const { return window_height_; }
    uint32_t current_gpu_frame_index() const { return current_gpu_frame_index_; }
    uint32_t frame_count() const { return frame_count_; }

    ID3D12CommandAllocator* current_frame_command_allocator();
    ID3D12DescriptorHeap* current_frame_descriptor_heap();
    ID3D12Resource* current_frame_output();
    D3D12_CPU_DESCRIPTOR_HANDLE current_frame_output_descriptor_handle();

    HWND hwnd() { return hwnd_; }

private:
    static constexpr uint32_t kNumGPUFramesInFlight = 2;
    static constexpr uint32_t kConstantBufferAlignment = 256;
    static constexpr uint32_t kMaxCommandBuffersPerFrame = 4096;
    static constexpr uint32_t kMaxUAVDescriptorsPerFrame = 4096;

    // Initialize rendering into main window.
    void InitWindow();
    // Wait for GPU frame (index is from 0 to num_gpu_frames_in_flight()-1).
    void WaitForGPUFrame(uint32_t index);
    // Execute all pending command lists for a given frame.
    // (index is from 0 to num_gpu_frames_in_flight()-1).
    void ExecuteCommandLists(uint32_t index);

    // Per-frame GPU data.
    struct GPUFrameData
    {
        ComPtr<ID3D12CommandAllocator> command_allocator = nullptr;
        ComPtr<ID3D12DescriptorHeap> descriptor_heap = nullptr;
        std::array<ComPtr<ID3D12CommandList>, kMaxCommandBuffersPerFrame> command_lists = {nullptr};
        std::atomic_uint32_t num_command_lists = 0;
        std::atomic_uint32_t num_descriptors = 0;
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

    // Render target chain.
    std::array<ComPtr<ID3D12Resource>, kNumGPUFramesInFlight> backbuffers_ = {nullptr};
    // Window event.
    HANDLE win32_event_ = INVALID_HANDLE_VALUE;
    uint32_t window_width_ = 0;
    uint32_t window_height_ = 0;
    uint32_t next_submission_id_ = 1;
    uint32_t uav_descriptor_increment_ = 0;
    uint32_t rtv_descriptor_increment_ = 0;

    uint32_t frame_count_ = 0;
};

template <typename R>
using PerGPUFrameResource = std::array<R, RenderSystem::num_gpu_frames_in_flight()>;

}  // namespace capsaicin