#include "render_system.h"

namespace capsaicin
{
namespace
{
enum MainRootSignature
{
    kConstants = 0,
    kNumEntries
};

struct Constants
{
    float rotation;
};
}  // namespace

RenderSystem::RenderSystem(HWND hwnd) : hwnd_(hwnd)
{
    info("RenderSystem: Initializing");

    rtv_descriptor_heap_ = dx12api().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBackbufferCount);

    for (uint32_t i = 0; i < kBackbufferCount; ++i)
    {
        gpu_frame_data_[i].command_allocator = dx12api().CreateCommandAllocator();
        gpu_frame_data_[i].command_list = dx12api().CreateCommandList(gpu_frame_data_[i].command_allocator.Get());
        gpu_frame_data_[i].command_list->Close();
    }

    InitWindow();
    InitMainPipeline();
}
RenderSystem::~RenderSystem() {}

void RenderSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    static auto time = std::chrono::high_resolution_clock::now();
    static auto prev_time = std::chrono::high_resolution_clock::now();
    static float rotation = 0.f;

    time = std::chrono::high_resolution_clock::now();
    auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time - prev_time).count();
    rotation += delta_ms * 3.14f / 1000.f;

    backbuffer_index_ = swapchain_->GetCurrentBackBufferIndex();

    WaitForGPUFrame(backbuffer_index_);

    GPUFrameData& gpu_frame_data = gpu_frame_data_[backbuffer_index_];

    ThrowIfFailed(gpu_frame_data.command_allocator->Reset(), "Command allocator reset failed");

    ID3D12GraphicsCommandList* command_list = gpu_frame_data.command_list.Get();

    command_list->Reset(gpu_frame_data.command_allocator.Get(), nullptr);

    UINT rtv_increment_size = dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor_handle(
        rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), backbuffer_index_, rtv_increment_size);

    auto rt_transition = CD3DX12_RESOURCE_BARRIER::Transition(
        backbuffers_[backbuffer_index_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list->ResourceBarrier(1, &rt_transition);

    command_list->SetGraphicsRootSignature(root_signature_.Get());
    command_list->SetGraphicsRoot32BitConstants(MainRootSignature::kConstants, 1u, &rotation, 0);
    command_list->SetPipelineState(pipeline_state_.Get());

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(window_width_), static_cast<float>(window_height_)};
    D3D12_RECT scissor_rect{0, 0, static_cast<LONG>(window_width_), static_cast<LONG>(window_height_)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor_rect);
    command_list->OMSetRenderTargets(1, &descriptor_handle, FALSE, nullptr);

    FLOAT shitty_red[] = {0.77f, 0.15f, 0.1f, 1.f};
    command_list->ClearRenderTargetView(descriptor_handle, shitty_red, 0, nullptr);

    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(3, 1, 0, 0);

    command_list->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            backbuffers_[backbuffer_index_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    command_list->Close();

    ID3D12CommandList* to_submit[] = {command_list};
    dx12api().command_queue()->ExecuteCommandLists(1, to_submit);

    ThrowIfFailed(swapchain_->Present(1, 0), "Present failed");

    gpu_frame_data.submission_id = next_submission_id_;
    ThrowIfFailed(dx12api().command_queue()->Signal(frame_submission_fence_.Get(), next_submission_id_++),
                  "Cannot signal fence");

    prev_time = time;
}

void RenderSystem::InitWindow()
{
    RECT window_rect;
    GetWindowRect(hwnd_, &window_rect);

    window_width_ = static_cast<UINT>(window_rect.right - window_rect.left);
    window_height_ = static_cast<UINT>(window_rect.bottom - window_rect.top);

    info("RenderSystem: Creating swap chain with {} render buffers", kBackbufferCount);
    swapchain_ = dx12api().CreateSwapchain(hwnd_, window_width_, window_height_, kBackbufferCount);

    frame_submission_fence_ = dx12api().CreateFence();
    win32_event_ = CreateEvent(nullptr, FALSE, FALSE, "Capsaicin frame sync event");

    {
        info("RenderSystem: Initializing backbuffers");
        uint32_t rtv_increment_size =
            dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (uint32_t i = 0; i < kBackbufferCount; ++i)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE descriptor_handle(
                rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), i, rtv_increment_size);
            ThrowIfFailed(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])), "Cannot retrieve swapchain buffer");
            dx12api().device()->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, descriptor_handle);
        }
    }
}

void RenderSystem::InitMainPipeline()
{
    CD3DX12_ROOT_PARAMETER root_entries[MainRootSignature::kNumEntries] = {};
    root_entries[kConstants].InitAsConstants(sizeof(Constants), 0);

    CD3DX12_ROOT_SIGNATURE_DESC desc = {};
    desc.Init(MainRootSignature::kNumEntries, root_entries);
    root_signature_ = dx12api().CreateRootSignature(desc);

    ShaderCompiler& shader_compiler{ShaderCompiler::instance()};
    auto vertex_shader = shader_compiler.CompileFromFile("../../../src/core/shaders/simple.hlsl", "vs_6_0", "VsMain");
    auto pixel_shader = shader_compiler.CompileFromFile("../../../src/core/shaders/simple.hlsl", "ps_6_0", "PsMain");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {nullptr, 0};
    pso_desc.pRootSignature = root_signature_.Get();
    pso_desc.VS = vertex_shader;
    pso_desc.PS = pixel_shader;
    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;

    pipeline_state_ = dx12api().CreatePipelineState(pso_desc);
}

void RenderSystem::WaitForGPUFrame(uint32_t index)
{
    if (frame_submission_fence_->GetCompletedValue() < gpu_frame_data_[index].submission_id)
    {
        frame_submission_fence_->SetEventOnCompletion(gpu_frame_data_[index].submission_id, win32_event_);
        WaitForSingleObject(win32_event_, INFINITE);
    }
}

}  // namespace capsaicin