#include "render_system.h"

#include "src/common.h"
#include "tlas_system.h"

namespace capsaicin
{
namespace
{
namespace MainRootSignature
{
enum
{
    kConstants = 0,
    kRaytracedTexture,
    kNumEntries
};
}

namespace RaytracingRootSignature
{
enum
{
    kConstants = 0,
    kAccelerationStructure,
    kOutput,
    kNumEntries
};
}

struct Constants
{
    uint32_t width;
    uint32_t height;
    float rotation;
    uint32_t padding;
};
}  // namespace

RenderSystem::RenderSystem(HWND hwnd) : hwnd_(hwnd)
{
    info("RenderSystem: Initializing");

    rtv_descriptor_heap_ = dx12api().CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBackbufferCount);
    uav_descriptor_heap_ = dx12api().CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kBackbufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    srv_descriptor_heap_ = dx12api().CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kBackbufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    for (uint32_t i = 0; i < kBackbufferCount; ++i)
    { gpu_frame_data_[i].command_allocator = dx12api().CreateCommandAllocator(); }

    // Create command list.
    command_list_ = dx12api().CreateCommandList(gpu_frame_data_[0].command_allocator.Get());
    command_list_->Close();

    raytracing_command_list_ = dx12api().CreateCommandList(gpu_frame_data_[0].command_allocator.Get());
    raytracing_command_list_->Close();

    InitWindow();
    InitMainPipeline();
    InitRaytracingPipeline();
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

    // Find scene TLAS.
    auto& tlases = access.Read<TLASComponent>();
    auto entities = entity_query().Filter([&tlases](Entity e) { return tlases.HasComponent(e); }).entities();

    auto& tlas = tlases.GetComponent(entities[0]);

    if (entities.size() != 1)
    {
        error("RenderSystem: no TLASes found");
        throw std::runtime_error("RenderSystem: no TLASes found");
    }

    backbuffer_index_ = swapchain_->GetCurrentBackBufferIndex();

    WaitForGPUFrame(backbuffer_index_);

    Raytrace(tlas.tlas.Get());

    Render(rotation);

    ThrowIfFailed(swapchain_->Present(1, 0), "Present failed");

    gpu_frame_data_[backbuffer_index_].submission_id = next_submission_id_;
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
    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER root_entries[MainRootSignature::kNumEntries] = {};
    root_entries[MainRootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
    root_entries[MainRootSignature::kRaytracedTexture].InitAsDescriptorTable(1, &range);

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

void RenderSystem::InitRaytracingPipeline()
{
    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    // Global Root Signature
    // This is a root signature that is  across all raytracing shaders invoked during a DispatchRays() call.
    {
        CD3DX12_DESCRIPTOR_RANGE range;
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_ROOT_PARAMETER root_entries[RaytracingRootSignature::kNumEntries] = {};
        root_entries[RaytracingRootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
        root_entries[RaytracingRootSignature::kAccelerationStructure].InitAsShaderResourceView(0);
        root_entries[RaytracingRootSignature::kOutput].InitAsDescriptorTable(1, &range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(RaytracingRootSignature::kNumEntries, root_entries);
        raytracing_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    auto shader = ShaderCompiler::instance().CompileFromFile("../../../src/core/shaders/primary.hlsl", "lib_6_3", "");

    CD3DX12_STATE_OBJECT_DESC pipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    auto lib = pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = shader;

    lib->SetDXILLibrary(&libdxil);
    lib->DefineExport(L"TraceVisibility");
    lib->DefineExport(L"Hit");
    lib->DefineExport(L"Miss");

    auto hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(L"Hit");

    hit_group->SetHitGroupExport(L"HitGroup");
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shader_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size = 4 * sizeof(float);    // float3 color, padding;
    UINT attribute_size = 2 * sizeof(float);  // float2 barycentrics
    shader_config->Config(payload_size, attribute_size);

    auto global_root_signature = pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_signature->SetRootSignature(raytracing_root_signature_.Get());

    auto pipeline_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    uint32_t max_recursion_depth = 1;
    pipeline_config->Config(max_recursion_depth);

    // Create the state object.
    ThrowIfFailed(device5->CreateStateObject(pipeline, IID_PPV_ARGS(&raytracing_pipeline_state_)),
                  "Couldn't create DirectX Raytracing state object.\n");

    ComPtr<ID3D12StateObjectProperties> state_object_props;
    ThrowIfFailed(raytracing_pipeline_state_.As(&state_object_props), "");

    auto raygen_shader_id = state_object_props->GetShaderIdentifier(L"TraceVisibility");
    auto miss_shader_id = state_object_props->GetShaderIdentifier(L"Miss");
    auto hitgroup_shader_id = state_object_props->GetShaderIdentifier(L"HitGroup");

    uint32_t shader_record_size =
        align(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), uint32_t(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    raygen_shader_table = dx12api().CreateUploadBuffer(shader_record_size, raygen_shader_id);
    hitgroup_shader_table = dx12api().CreateUploadBuffer(shader_record_size, hitgroup_shader_id);
    miss_shader_table = dx12api().CreateUploadBuffer(shader_record_size, miss_shader_id);

    {
        info("RenderSystem: Initializing raytracing outputs");
        uint32_t increment_size =
            dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        for (uint32_t i = 0; i < kBackbufferCount; ++i)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_uav_handle(
                uav_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), i, increment_size);
            CD3DX12_CPU_DESCRIPTOR_HANDLE cpu_srv_handle(
                srv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), i, increment_size);

            auto texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                             window_width_,
                                                             window_height_,
                                                             1,
                                                             0,
                                                             1,
                                                             0,
                                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            raytracing_outputs_[i] = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            uav_desc.Texture2D.MipSlice = 0;
            uav_desc.Texture2D.PlaneSlice = 0;
            dx12api().device()->CreateUnorderedAccessView(
                raytracing_outputs_[i].Get(), nullptr, &uav_desc, cpu_uav_handle);

            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            srv_desc.Texture2D.MipLevels = 1;
            srv_desc.Texture2D.MostDetailedMip = 0;
            srv_desc.Texture2D.PlaneSlice = 0;
            srv_desc.Texture2D.ResourceMinLODClamp = 0;
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            dx12api().device()->CreateShaderResourceView(raytracing_outputs_[i].Get(), &srv_desc, cpu_srv_handle);
        }
    }
}

void RenderSystem::Raytrace(ID3D12Resource* scene)
{
    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    raytracing_command_list_->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    cmdlist4->Reset(gpu_frame_data_[backbuffer_index_].command_allocator.Get(), nullptr);

    ID3D12DescriptorHeap* desc_heaps[] = {uav_descriptor_heap_.Get()};

    cmdlist4->SetDescriptorHeaps(1, desc_heaps);
    cmdlist4->SetComputeRootSignature(raytracing_root_signature_.Get());
    cmdlist4->SetComputeRootShaderResourceView(RaytracingRootSignature::kAccelerationStructure,
                                               scene->GetGPUVirtualAddress());

    uint32_t increment_size =
        dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_uav_handle(
        uav_descriptor_heap_->GetGPUDescriptorHandleForHeapStart(), backbuffer_index_, increment_size);

    cmdlist4->SetComputeRootDescriptorTable(RaytracingRootSignature::kOutput, gpu_uav_handle);

    D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
    dispatch_desc.HitGroupTable.StartAddress = hitgroup_shader_table->GetGPUVirtualAddress();
    dispatch_desc.HitGroupTable.SizeInBytes = hitgroup_shader_table->GetDesc().Width;
    dispatch_desc.HitGroupTable.StrideInBytes = dispatch_desc.HitGroupTable.SizeInBytes;
    dispatch_desc.MissShaderTable.StartAddress = miss_shader_table->GetGPUVirtualAddress();
    dispatch_desc.MissShaderTable.SizeInBytes = miss_shader_table->GetDesc().Width;
    dispatch_desc.MissShaderTable.StrideInBytes = dispatch_desc.MissShaderTable.SizeInBytes;
    dispatch_desc.RayGenerationShaderRecord.StartAddress = raygen_shader_table->GetGPUVirtualAddress();
    dispatch_desc.RayGenerationShaderRecord.SizeInBytes = raygen_shader_table->GetDesc().Width;
    dispatch_desc.Width = window_width_;
    dispatch_desc.Height = window_height_;
    dispatch_desc.Depth = 1;

    cmdlist4->SetPipelineState1(raytracing_pipeline_state_.Get());
    cmdlist4->DispatchRays(&dispatch_desc);

    cmdlist4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(raytracing_outputs_[backbuffer_index_].Get()));
    cmdlist4->Close();

    ID3D12CommandList* to_submit[] = {cmdlist4.Get()};
    dx12api().command_queue()->ExecuteCommandLists(1, to_submit);
}

void RenderSystem::Render(float time)
{
    Constants constants{window_width_, window_height_, time, 0};

    GPUFrameData& gpu_frame_data = gpu_frame_data_[backbuffer_index_];

    ID3D12GraphicsCommandList* command_list = command_list_.Get();

    command_list->Reset(gpu_frame_data.command_allocator.Get(), nullptr);

    UINT rtv_increment_size = dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_gpu_handle(
        rtv_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(), backbuffer_index_, rtv_increment_size);

    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(backbuffers_[backbuffer_index_].Get(),
                                                 D3D12_RESOURCE_STATE_PRESENT,
                                                 D3D12_RESOURCE_STATE_RENDER_TARGET),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(raytracing_outputs_[backbuffer_index_].Get(),
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)

        };

        command_list->ResourceBarrier(2, transitions);
    }

    ID3D12DescriptorHeap* descriptor_heaps[] = {srv_descriptor_heap_.Get()};

    command_list->SetGraphicsRootSignature(root_signature_.Get());
    command_list->SetDescriptorHeaps(1, descriptor_heaps);
    command_list->SetGraphicsRoot32BitConstants(MainRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    command_list->SetPipelineState(pipeline_state_.Get());

    uint32_t increment_size =
        dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpu_srv_handle(
        srv_descriptor_heap_->GetGPUDescriptorHandleForHeapStart(), backbuffer_index_, increment_size);

    command_list->SetGraphicsRootDescriptorTable(MainRootSignature::kRaytracedTexture, gpu_srv_handle);

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(window_width_), static_cast<float>(window_height_)};
    D3D12_RECT scissor_rect{0, 0, static_cast<LONG>(window_width_), static_cast<LONG>(window_height_)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor_rect);
    command_list->OMSetRenderTargets(1, &rtv_gpu_handle, FALSE, nullptr);

    FLOAT shitty_red[] = {0.77f, 0.15f, 0.1f, 1.f};
    command_list->ClearRenderTargetView(rtv_gpu_handle, shitty_red, 0, nullptr);

    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(3, 1, 0, 0);

    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(backbuffers_[backbuffer_index_].Get(),
                                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                 D3D12_RESOURCE_STATE_PRESENT),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(raytracing_outputs_[backbuffer_index_].Get(),
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

        };

        command_list->ResourceBarrier(2, transitions);
    }

    command_list->Close();

    ID3D12CommandList* to_submit[] = {command_list};
    dx12api().command_queue()->ExecuteCommandLists(1, to_submit);
}  // namespace capsaicin

void RenderSystem::WaitForGPUFrame(uint32_t index)
{
    if (frame_submission_fence_->GetCompletedValue() < gpu_frame_data_[index].submission_id)
    {
        frame_submission_fence_->SetEventOnCompletion(gpu_frame_data_[index].submission_id, win32_event_);
        WaitForSingleObject(win32_event_, INFINITE);
    }

    ThrowIfFailed(gpu_frame_data_[index].command_allocator->Reset(), "Command allocator reset failed");
}

}  // namespace capsaicin