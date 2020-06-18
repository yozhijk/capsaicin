#include "render_system.h"

#include "src/common.h"
#include "src/systems/asset_load_system.h"
#include "src/systems/camera_system.h"
#include "src/systems/gui_system.h"
#include "src/systems/tlas_system.h"

namespace capsaicin
{
RenderSystem::RenderSystem(HWND hwnd) : hwnd_(hwnd)
{
    info("RenderSystem: Initializing");

    // Render target descriptor heap.
    rtv_descriptor_heap_ =
        dx12api().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kNumGPUFramesInFlight);

    // Create command allocators: one per GPU frame.
    for (uint32_t i = 0; i < kNumGPUFramesInFlight; ++i)
    {
        gpu_frame_data_[i].command_allocator = dx12api().CreateCommandAllocator();
        gpu_frame_data_[i].descriptor_heap =
            dx12api().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                           kMaxUAVDescriptorsPerFrame,
                                           D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
        gpu_frame_data_[i].timestamp_query_heap = dx12api().CreateQueryHeap(
            D3D12_QUERY_HEAP_TYPE_TIMESTAMP, kMaxCommandBuffersPerFrame * 2);
        gpu_frame_data_[i].timestamp_buffer =
            dx12api().CreateReadbackBuffer(kMaxCommandBuffersPerFrame * 2 * sizeof(std::uint64_t));
    }

    // Init window.
    InitWindow();

    // Initialize backbuffer.
    current_gpu_frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    // Save descriptor increment for UAVs and RTVs.
    uav_descriptor_increment_ = dx12api().device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtv_descriptor_increment_ =
        dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create resolve command list.
    query_resolve_command_list_ = dx12api().CreateCommandList(current_frame_command_allocator());
    query_resolve_command_list_->Close();
}

RenderSystem::~RenderSystem()
{
}

void RenderSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    // Get engine settings.
    auto& settings = access.Write<SettingsComponent>()[0];

    // Execute queued command lists.
    ExecuteCommandLists(current_gpu_frame_index());

    // Execute internal command buffer to fetch queries.
    ResolveQueryData();

    // Present
    auto sync_interval = settings.vsync ? 1 : 0;
    ThrowIfFailed(swapchain_->Present(sync_interval, 0), "Present failed");

    // Assign new submission ID to this submission.
    current_gpu_frame_data().submission_id = next_submission_id_;

    // Enqeue completion signal.
    ThrowIfFailed(
        dx12api().command_queue()->Signal(frame_submission_fence_.Get(), next_submission_id_++),
        "Cannot signal fence");

    // Move to next gpu frame.
    current_gpu_frame_index_ = swapchain_->GetCurrentBackBufferIndex();

    // Make sure previous submission for this gpu frame index are finished.
    WaitForGPUFrame(current_gpu_frame_index());

    // Advance frame counter.
    ++frame_count_;
}

D3D12_CPU_DESCRIPTOR_HANDLE
RenderSystem::current_frame_output_descriptor_handle()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(),
                                         current_gpu_frame_index(),
                                         rtv_descriptor_increment_);
    return handle;
}

ID3D12QueryHeap* RenderSystem::current_frame_timestamp_query_heap()
{
    return current_gpu_frame_data().timestamp_query_heap.Get();
}

ID3D12Resource* RenderSystem::current_frame_output()
{
    return backbuffers_[current_gpu_frame_index_].Get();
}

ID3D12CommandAllocator* RenderSystem::current_frame_command_allocator()
{
    return current_gpu_frame_data().command_allocator.Get();
}
ID3D12DescriptorHeap* RenderSystem::current_frame_descriptor_heap()
{
    return current_gpu_frame_data().descriptor_heap.Get();
}

void RenderSystem::InitWindow()
{
    RECT window_rect;
    GetWindowRect(hwnd_, &window_rect);

    window_width_  = static_cast<UINT>(window_rect.right - window_rect.left);
    window_height_ = static_cast<UINT>(window_rect.bottom - window_rect.top);

    info("RenderSystem: Creating swap chain with {} render buffers", kNumGPUFramesInFlight);
    swapchain_ =
        dx12api().CreateSwapchain(hwnd_, window_width_, window_height_, kNumGPUFramesInFlight);

    frame_submission_fence_ = dx12api().CreateFence();
    win32_event_            = CreateEvent(nullptr, FALSE, FALSE, "Capsaicin frame sync event");

    {
        info("RenderSystem: Initializing backbuffers");
        uint32_t rtv_increment_size =
            dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (uint32_t i = 0; i < kNumGPUFramesInFlight; ++i)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor_handle(
                rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), i, rtv_increment_size);
            ThrowIfFailed(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])),
                          "Cannot retrieve swapchain buffer");
            dx12api().device()->CreateRenderTargetView(
                backbuffers_[i].Get(), nullptr, descriptor_handle);
        }
    }
}

void RenderSystem::WaitForGPUFrame(uint32_t index)
{
    auto fence_value = frame_submission_fence_->GetCompletedValue();

    if (fence_value < gpu_frame_data_[index].submission_id)
    {
        frame_submission_fence_->SetEventOnCompletion(gpu_frame_data_[index].submission_id,
                                                      win32_event_);
        WaitForSingleObject(win32_event_, INFINITE);
    }

    // Readback timestamp counters.
    ReadbackTimestamps(index);

    // Reset command allocator for the frame.
    ThrowIfFailed(gpu_frame_data_[index].command_allocator->Reset(),
                  "Command allocator reset failed");

    if (!gpu_frame_data_[index].autorelease_pool.empty())
    {
        info("Releasing {} autorelease resources", gpu_frame_data_[index].autorelease_pool.size());
        // Release resources.
        gpu_frame_data_[index].autorelease_pool.clear();
    }

    gpu_frame_data_[index].num_descriptors           = 0;
    gpu_frame_data_[index].num_timestamp_query_pairs = 0;
}

void RenderSystem::ExecuteCommandLists(uint32_t index)
{
    auto num_command_lists = gpu_frame_data_[index].num_command_lists.load();

    std::vector<ID3D12CommandList*> command_lists(num_command_lists);
    std::transform(gpu_frame_data_[index].command_lists.cbegin(),
                   gpu_frame_data_[index].command_lists.cbegin() + num_command_lists,
                   command_lists.begin(),
                   [](ComPtr<ID3D12CommandList> cmd_list) { return cmd_list.Get(); });

    dx12api().command_queue()->ExecuteCommandLists(num_command_lists, command_lists.data());
    gpu_frame_data_[index].num_command_lists = 0;
}

void RenderSystem::ResolveQueryData()
{
    auto& gpu_frame_data = current_gpu_frame_data();

    query_resolve_command_list_->Reset(current_frame_command_allocator(), nullptr);

    query_resolve_command_list_->ResolveQueryData(gpu_frame_data.timestamp_query_heap.Get(),
                                                  D3D12_QUERY_TYPE_TIMESTAMP,
                                                  0,
                                                  gpu_frame_data.num_timestamp_query_pairs * 2,
                                                  gpu_frame_data.timestamp_buffer.Get(),
                                                  0);

    query_resolve_command_list_->Close();

    PushCommandList(query_resolve_command_list_);
}

void RenderSystem::ReadbackTimestamps(uint32_t frame_index)
{
    std::uint64_t timestamp_freq = 0;
    dx12api().command_queue()->GetTimestampFrequency(&timestamp_freq);

    gpu_timings_.clear();

    auto& gpu_frame_data = current_gpu_frame_data();

    uint64_t* ptr = nullptr;
    gpu_frame_data.timestamp_buffer->Map(0, nullptr, (void**)&ptr);

    for (auto i = 0u; i < gpu_frame_data.num_timestamp_query_pairs; ++i)
    {
        float value = float(ptr[i * 2 + 1] - ptr[i * 2]) / timestamp_freq;
        gpu_timings_.emplace_back(gpu_frame_data.query_names[i], value);
    }

    gpu_frame_data.timestamp_buffer->Unmap(0, nullptr);
}

RenderSystem::GPUFrameData& RenderSystem::current_gpu_frame_data()
{
    return current_gpu_frame_data();
}

void RenderSystem::AddAutoreleaseResource(ComPtr<ID3D12Resource> resource)
{
    current_gpu_frame_data().autorelease_pool.push_back(resource);
}

uint32_t RenderSystem::AllocateDescriptorRange(uint32_t num_descriptors)
{
    auto idx = current_gpu_frame_data().num_descriptors.fetch_add(num_descriptors);

    if (idx > kMaxUAVDescriptorsPerFrame)
    {
        error("RenderSystem: Max number of UAV descriptors exceeded");
        throw std::runtime_error("RenderSystem: Max number of UAV descriptors exceeded");
    }

    return idx;
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderSystem::GetDescriptorHandleCPU(uint32_t index)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        current_gpu_frame_data().descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
        index,
        uav_descriptor_increment_);

    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RenderSystem::GetDescriptorHandleGPU(uint32_t index)
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(
        current_gpu_frame_data().descriptor_heap->GetGPUDescriptorHandleForHeapStart(),
        index,
        uav_descriptor_increment_);

    return handle;
}

std::pair<uint32_t, uint32_t> RenderSystem::AllocateTimestampQueryPair(const std::string& name)
{
    auto val = current_gpu_frame_data().num_timestamp_query_pairs.fetch_add(1);
    current_gpu_frame_data().query_names[val] = name;
    return std::make_pair(2 * val, 2 * val + 1);
}

const std::vector<std::pair<std::string, float>>& RenderSystem::gpu_timings() const
{
    return gpu_timings_;
}

void RenderSystem::PushCommandList(ComPtr<ID3D12CommandList> command_list)
{
    auto idx = current_gpu_frame_data().num_command_lists.fetch_add(1);

    if (idx >= kMaxCommandBuffersPerFrame)
    {
        error("RenderSystem: Max number of command buffer exceeded");
        throw std::runtime_error("RenderSystem: Max number of command buffer exceeded");
    }

    current_gpu_frame_data().command_lists[idx] = command_list;
}

}  // namespace capsaicin
