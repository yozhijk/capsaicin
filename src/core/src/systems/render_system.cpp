#include "render_system.h"

#include "src/common.h"
#include "src/systems/asset_load_system.h"
#include "src/systems/camera_system.h"
#include "src/systems/tlas_system.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_dx12.h"
#include "third_party/imgui/imgui_impl_win32.h"

using namespace ImGui;

namespace capsaicin
{
RenderSystem::RenderSystem(HWND hwnd) : hwnd_(hwnd)
{
    info("RenderSystem: Initializing");

    // Render target descriptor heap.
    rtv_descriptor_heap_ = dx12api().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kNumGPUFramesInFlight);
    imgui_descriptor_heap_ = dx12api().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    // Create command allocators: one per GPU frame.
    for (uint32_t i = 0; i < kNumGPUFramesInFlight; ++i)
    {
        gpu_frame_data_[i].command_allocator = dx12api().CreateCommandAllocator();
        gpu_frame_data_[i].descriptor_heap = dx12api().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                                            kMaxUAVDescriptorsPerFrame,
                                                                            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    }

    // Init window.
    InitWindow();

    // Initialize backbuffer.
    current_gpu_frame_index_ = swapchain_->GetCurrentBackBufferIndex();
    // Save descriptor increment for UAVs and RTVs.
    uav_descriptor_increment_ =
        dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtv_descriptor_increment_ = dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

RenderSystem::~RenderSystem()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void RenderSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    ExecuteCommandLists(current_gpu_frame_index());

    RenderGUI();

    ThrowIfFailed(swapchain_->Present(0, 0), "Present failed");

    gpu_frame_data_[current_gpu_frame_index()].submission_id = next_submission_id_;
    ThrowIfFailed(dx12api().command_queue()->Signal(frame_submission_fence_.Get(), next_submission_id_++),
                  "Cannot signal fence");

    current_gpu_frame_index_ = swapchain_->GetCurrentBackBufferIndex();

    WaitForGPUFrame(current_gpu_frame_index());

    ++frame_count_;
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderSystem::current_frame_output_descriptor_handle()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(),
                                         current_gpu_frame_index(),
                                         rtv_descriptor_increment_);
    return handle;
}

ID3D12Resource* RenderSystem::current_frame_output() { return backbuffers_[current_gpu_frame_index_].Get(); }
ID3D12CommandAllocator* RenderSystem::current_frame_command_allocator()
{
    return gpu_frame_data_[current_gpu_frame_index_].command_allocator.Get();
}
ID3D12DescriptorHeap* RenderSystem::current_frame_descriptor_heap()
{
    return gpu_frame_data_[current_gpu_frame_index_].descriptor_heap.Get();
}

void RenderSystem::InitWindow()
{
    RECT window_rect;
    GetWindowRect(hwnd_, &window_rect);

    window_width_ = static_cast<UINT>(window_rect.right - window_rect.left);
    window_height_ = static_cast<UINT>(window_rect.bottom - window_rect.top);

    info("RenderSystem: Creating swap chain with {} render buffers", kNumGPUFramesInFlight);
    swapchain_ = dx12api().CreateSwapchain(hwnd_, window_width_, window_height_, kNumGPUFramesInFlight);

    frame_submission_fence_ = dx12api().CreateFence();
    win32_event_ = CreateEvent(nullptr, FALSE, FALSE, "Capsaicin frame sync event");

    {
        info("RenderSystem: Initializing backbuffers");
        uint32_t rtv_increment_size =
            dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (uint32_t i = 0; i < kNumGPUFramesInFlight; ++i)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor_handle(
                rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), i, rtv_increment_size);
            ThrowIfFailed(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])), "Cannot retrieve swapchain buffer");
            dx12api().device()->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, descriptor_handle);
        }
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX12_Init(dx12api().device(),
                        num_gpu_frames_in_flight(),
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        imgui_descriptor_heap_.Get(),
                        imgui_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(),
                        imgui_descriptor_heap_->GetGPUDescriptorHandleForHeapStart());
}

void RenderSystem::WaitForGPUFrame(uint32_t index)
{
    auto fence_value = frame_submission_fence_->GetCompletedValue();

    if (fence_value < gpu_frame_data_[index].submission_id)
    {
        frame_submission_fence_->SetEventOnCompletion(gpu_frame_data_[index].submission_id, win32_event_);
        WaitForSingleObject(win32_event_, INFINITE);
    }

    // Reset command allocator for the frame.
    ThrowIfFailed(gpu_frame_data_[index].command_allocator->Reset(), "Command allocator reset failed");

    if (!gpu_frame_data_[index].autorelease_pool.empty())
    {
        info("Releasing {} autorelease resources", gpu_frame_data_[index].autorelease_pool.size());
        // Release resources.
        gpu_frame_data_[index].autorelease_pool.clear();
    }

    gpu_frame_data_[index].num_descriptors = 0;
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

void RenderSystem::RenderGUI()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    static float f = 0.0f;
    static int counter = 0;

    ImGui::Begin("Hello, world!");  // Create a window called "Hello, world!" and append into it.

    ImGui::Text("This is some useful text.");           // Display some text (you can use a format strings too)

    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);             // Edit 1 float using a slider from 0.0f to 1.0f

    if (ImGui::Button("Button"))  // Buttons return true when clicked (most widgets return true when edited/activated)
        counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);

    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();
}

void RenderSystem::AddAutoreleaseResource(ComPtr<ID3D12Resource> resource)
{
    gpu_frame_data_[current_gpu_frame_index()].autorelease_pool.push_back(resource);
}

uint32_t RenderSystem::AllocateDescriptorRange(uint32_t num_descriptors)
{
    auto idx = gpu_frame_data_[current_gpu_frame_index()].num_descriptors.fetch_add(num_descriptors);

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
        gpu_frame_data_[current_gpu_frame_index()].descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
        index,
        uav_descriptor_increment_);

    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RenderSystem::GetDescriptorHandleGPU(uint32_t index)
{
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(
        gpu_frame_data_[current_gpu_frame_index()].descriptor_heap->GetGPUDescriptorHandleForHeapStart(),
        index,
        uav_descriptor_increment_);

    return handle;
}

void RenderSystem::PushCommandList(ComPtr<ID3D12CommandList> command_list)
{
    auto idx = gpu_frame_data_[current_gpu_frame_index()].num_command_lists.fetch_add(1);

    if (idx >= kMaxCommandBuffersPerFrame)
    {
        error("RenderSystem: Max number of command buffer exceeded");
        throw std::runtime_error("RenderSystem: Max number of command buffer exceeded");
    }

    gpu_frame_data_[current_gpu_frame_index()].command_lists[idx] = command_list;
}

}  // namespace capsaicin
