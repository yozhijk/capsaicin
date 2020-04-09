#include "raytracing_system.h"

#include "src/common.h"
#include "src/systems/asset_load_system.h"
#include "src/systems/camera_system.h"
#include "src/systems/texture_system.h"
#include "src/systems/tlas_system.h"

namespace capsaicin
{
namespace
{
// Signature for GI raytracing pass.
namespace RaytracingRootSignature
{
enum
{
    kConstants = 0,
    kCameraBuffer,
    kAccelerationStructure,
    kBlueNoiseTexture,
    kSceneData,
    kOutput,
    kNumEntries
};
}

// Signature for temporal accumulation pass.
namespace TemporalAccumulateRootSignature
{
enum
{
    kConstants = 0,
    kCameraBuffer,
    kPrevCameraBuffer,
    kBlueNoiseTexture,
    kCurrentFrameOutput,
    kHistory,
    kNumEntries
};
}

// Signature for EAW denoising pass.
namespace EAWDenoisingRootSignature
{
enum
{
    kConstants = 0,
    kOutput,
    kNumEntries
};
}

// Root constants for raster blit.
struct Constants
{
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t padding;
};

// Find TLAS component and retrieve TLAS resource.
TLASComponent GetSceneTLASComponent(ComponentAccess& access, EntityQuery& entity_query)
{
    // Find scene TLAS.
    auto& tlases = access.Read<TLASComponent>();
    auto entities = entity_query().Filter([&tlases](Entity e) { return tlases.HasComponent(e); }).entities();
    if (entities.size() != 1)
    {
        error("RaytracingSystem: no TLASes found");
        throw std::runtime_error("RaytracingSystem: no TLASes found");
    }

    auto& tlas = tlases.GetComponent(entities[0]);
    return tlas;
}

auto GetCamera(ComponentAccess& access, EntityQuery& entity_query)
{
    auto& cameras = access.Read<CameraComponent>();
    auto entities = entity_query().Filter([&cameras](Entity e) { return cameras.HasComponent(e); }).entities();
    if (entities.size() != 1)
    {
        error("RaytracingSystem: no cameras found");
        throw std::runtime_error("RaytracingSystem: no cameras found");
    }

    return cameras.GetComponent(entities[0]);
}
}  // namespace

RaytracingSystem::RaytracingSystem()
{
    info("RaytracingSystem: Initializing");

    auto command_allocator = world().GetSystem<RenderSystem>().current_frame_command_allocator();

    // Create command list for visibility raytracing.
    raytracing_command_list_ = dx12api().CreateCommandList(command_allocator);
    raytracing_command_list_->Close();

    copy_gbuffer_command_list_ = dx12api().CreateCommandList(command_allocator);
    copy_gbuffer_command_list_->Close();

    ta_command_list_ = dx12api().CreateCommandList(command_allocator);
    ta_command_list_->Close();

    eaw_command_list_ = dx12api().CreateCommandList(command_allocator);
    eaw_command_list_->Close();

    // Initialize raytracing pipeline.
    InitPipeline();
    InitRenderStructures();
    InitTemporalAccumulatePipeline();
    InitEAWDenoisePipeline();
}

RaytracingSystem::~RaytracingSystem() = default;

void RaytracingSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto tlas = GetSceneTLASComponent(access, entity_query);
    auto camera = GetCamera(access, entity_query);

    auto& meshes = access.Read<MeshComponent>();

    GPUSceneData scene_data;
    scene_data.index_buffer = meshes[0].indices.Get();
    scene_data.vertex_buffer = meshes[0].vertices.Get();
    scene_data.normal_buffer = meshes[0].normals.Get();
    scene_data.texcoord_buffer = meshes[0].texcoords.Get();

    auto scene_data_descriptor_table = PopulateSceneDataDescriptorTable(scene_data);
    auto output_descritor_table = PopulateOutputDescriptorTable();
    auto internal_descriptor_table = PopulateInternalDataDescritptorTable();
    auto history_descriptor_table = PopulateHistoryDescritorTable();
    auto eaw_descriptor_table = PopulateEAWOutputDescritorTable();

    // Save previous GBuffer.
    CopyGBuffer();

    // Do raytracing pass.
    Raytrace(tlas.tlas.Get(),
             camera.camera_buffer.Get(),
             scene_data_descriptor_table,
             internal_descriptor_table,
             output_descritor_table);

    // Do temporal integration step.
    IntegrateTemporally(camera.camera_buffer.Get(),
                        camera.prev_camera_buffer.Get(),
                        internal_descriptor_table,
                        output_descritor_table,
                        history_descriptor_table);

    // Denoise.
    Denoise(eaw_descriptor_table);
}

ID3D12Resource* RaytracingSystem::current_frame_output_direct()
{
    //auto& render_system = world().GetSystem<RenderSystem>();
    return output_direct_.Get();
}

ID3D12Resource* RaytracingSystem::current_frame_output_indirect()
{
    // auto& render_system = world().GetSystem<RenderSystem>();
    return output_indirect_.Get();
}

void RaytracingSystem::InitPipeline()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto window_width = render_system.window_width();
    auto window_height = render_system.window_width();

    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    // Global Root Signature
    // This is a root signature that is  across all raytracing shaders invoked during a DispatchRays() call.
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 4);

        CD3DX12_DESCRIPTOR_RANGE scene_data_descriptor_range;
        scene_data_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0);

        CD3DX12_DESCRIPTOR_RANGE blue_noise_texture_descriptor_range;
        blue_noise_texture_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        CD3DX12_ROOT_PARAMETER root_entries[RaytracingRootSignature::kNumEntries] = {};
        root_entries[RaytracingRootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
        root_entries[RaytracingRootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[RaytracingRootSignature::kAccelerationStructure].InitAsShaderResourceView(0);
        root_entries[RaytracingRootSignature::kBlueNoiseTexture].InitAsDescriptorTable(
            1, &blue_noise_texture_descriptor_range);
        root_entries[RaytracingRootSignature::kSceneData].InitAsDescriptorTable(1, &scene_data_descriptor_range);
        root_entries[RaytracingRootSignature::kOutput].InitAsDescriptorTable(1, &output_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(RaytracingRootSignature::kNumEntries, root_entries);
        raytracing_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    auto shader =
        ShaderCompiler::instance().CompileFromFile("../../../src/core/shaders/visibility.hlsl", "lib_6_3", "");

    CD3DX12_STATE_OBJECT_DESC pipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    auto lib = pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = shader;

    lib->SetDXILLibrary(&libdxil);
    lib->DefineExport(L"TraceVisibility");
    lib->DefineExport(L"Hit");
    lib->DefineExport(L"ShadowHit");
    lib->DefineExport(L"Miss");
    lib->DefineExport(L"ShadowMiss");

    auto hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(L"Hit");

    hit_group->SetHitGroupExport(L"HitGroup");
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shadow_hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    shadow_hit_group->SetAnyHitShaderImport(L"ShadowHit");

    shadow_hit_group->SetHitGroupExport(L"ShadowHitGroup");
    shadow_hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shader_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size = 3 * sizeof(XMFLOAT4);
    UINT attribute_size = sizeof(XMFLOAT4);
    shader_config->Config(payload_size, attribute_size);

    auto global_root_signature = pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_signature->SetRootSignature(raytracing_root_signature_.Get());

    auto pipeline_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    uint32_t max_recursion_depth = 4;
    pipeline_config->Config(max_recursion_depth);

    // Create the state object.
    ThrowIfFailed(device5->CreateStateObject(pipeline, IID_PPV_ARGS(&raytracing_pipeline_state_)),
                  "Couldn't create DirectX Raytracing state object.\n");

    ComPtr<ID3D12StateObjectProperties> state_object_props;
    ThrowIfFailed(raytracing_pipeline_state_.As(&state_object_props), "");

    auto raygen_shader_id = state_object_props->GetShaderIdentifier(L"TraceVisibility");
    auto miss_shader_id = state_object_props->GetShaderIdentifier(L"Miss");
    auto shadow_miss_shader_id = state_object_props->GetShaderIdentifier(L"ShadowMiss");
    auto hitgroup_shader_id = state_object_props->GetShaderIdentifier(L"HitGroup");
    auto shadow_hitgroup_shader_id = state_object_props->GetShaderIdentifier(L"ShadowHitGroup");

    uint32_t shader_record_size =
        align(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES), uint32_t(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    raygen_shader_table = dx12api().CreateUploadBuffer(shader_record_size, raygen_shader_id);
    hitgroup_shader_table = dx12api().CreateUploadBuffer(2 * shader_record_size);
    miss_shader_table = dx12api().CreateUploadBuffer(2 * shader_record_size);

    char* data = nullptr;
    hitgroup_shader_table->Map(0, nullptr, (void**)&data);
    memcpy(data, hitgroup_shader_id, shader_record_size);
    memcpy(data + shader_record_size, shadow_hitgroup_shader_id, shader_record_size);
    hitgroup_shader_table->Unmap(0, nullptr);

    miss_shader_table->Map(0, nullptr, (void**)&data);
    memcpy(data, miss_shader_id, shader_record_size);
    memcpy(data + shader_record_size, shadow_miss_shader_id, shader_record_size);
    miss_shader_table->Unmap(0, nullptr);

    {
        info("RaytracingSystem: Initializing raytracing outputs");

        {
            auto texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                             window_width,
                                                             window_height,
                                                             1,
                                                             0,
                                                             1,
                                                             0,
                                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            output_direct_ = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            output_indirect_ = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            output_temp_ = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // History data.
        {
            auto texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                             window_width,
                                                             window_height,
                                                             1,
                                                             0,
                                                             1,
                                                             0,
                                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            temporal_history_[0] = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            temporal_history_[1] = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            // GBuffers are 32 bit.
            texture_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            gbuffer_ = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            prev_gbuffer_ = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
}

void RaytracingSystem::InitTemporalAccumulatePipeline()
{
    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0);

        CD3DX12_DESCRIPTOR_RANGE history_descriptor_range;
        history_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 3);

        CD3DX12_DESCRIPTOR_RANGE blue_noise_texture_descriptor_range;
        blue_noise_texture_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER root_entries[TemporalAccumulateRootSignature::kNumEntries] = {};
        root_entries[TemporalAccumulateRootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
        root_entries[TemporalAccumulateRootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[TemporalAccumulateRootSignature::kPrevCameraBuffer].InitAsConstantBufferView(2);
        root_entries[TemporalAccumulateRootSignature::kBlueNoiseTexture].InitAsDescriptorTable(
            1, &blue_noise_texture_descriptor_range);
        root_entries[TemporalAccumulateRootSignature::kCurrentFrameOutput].InitAsDescriptorTable(
            1, &output_descriptor_range);
        root_entries[TemporalAccumulateRootSignature::kHistory].InitAsDescriptorTable(1, &history_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(TemporalAccumulateRootSignature::kNumEntries, root_entries);
        ta_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    auto shader = ShaderCompiler::instance().CompileFromFile(
        "../../../src/core/shaders/temporal_accumulation.hlsl", "cs_6_3", "Accumulate");

    ta_pipeline_state_ = dx12api().CreateComputePipelineState(shader, ta_root_signature_.Get());
}

void RaytracingSystem::InitRenderStructures()
{
    auto& texture_system = world().GetSystem<TextureSystem>();
    blue_noise_texture_ = texture_system.GetTexture("bluenoise256.png");
}

void RaytracingSystem::InitEAWDenoisePipeline()
{
    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0);

        CD3DX12_ROOT_PARAMETER root_entries[EAWDenoisingRootSignature::kNumEntries] = {};
        root_entries[EAWDenoisingRootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
        root_entries[EAWDenoisingRootSignature::kOutput].InitAsDescriptorTable(1, &output_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(EAWDenoisingRootSignature::kNumEntries, root_entries);
        eaw_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    auto shader =
        ShaderCompiler::instance().CompileFromFile("../../../src/core/shaders/eaw_blur.hlsl", "cs_6_3", "Blur");

    eaw_pipeline_state_ = dx12api().CreateComputePipelineState(shader, eaw_root_signature_.Get());
}

void RaytracingSystem::CopyGBuffer()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto window_width = render_system.window_width();
    auto window_height = render_system.window_width();
    auto command_allocator = render_system.current_frame_command_allocator();

    copy_gbuffer_command_list_->Reset(command_allocator, nullptr);

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(
                gbuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(
                prev_gbuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)

        };

        copy_gbuffer_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;
    dst_loc.pResource = prev_gbuffer_.Get();

    D3D12_TEXTURE_COPY_LOCATION src_loc = dst_loc;
    src_loc.pResource = gbuffer_.Get();

    D3D12_BOX copy_box{0, 0, 0, window_width, window_height, 1};
    copy_gbuffer_command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, &copy_box);

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(
                gbuffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(
                prev_gbuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)

        };

        copy_gbuffer_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    copy_gbuffer_command_list_->Close();
    render_system.PushCommandList(copy_gbuffer_command_list_);
}

void RaytracingSystem::Raytrace(ID3D12Resource* scene,
                                ID3D12Resource* camera,
                                uint32_t scene_data_base_index,
                                uint32_t internal_descriptor_table,
                                uint32_t output_uav_index)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto window_width = render_system.window_width();
    auto window_height = render_system.window_width();
    auto command_allocator = render_system.current_frame_command_allocator();
    auto descriptor_heap = render_system.current_frame_descriptor_heap();

    Constants constants{render_system.window_width(), render_system.window_height(), render_system.frame_count(), 0};

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    raytracing_command_list_->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    cmdlist4->Reset(command_allocator, nullptr);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    cmdlist4->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    cmdlist4->SetComputeRootSignature(raytracing_root_signature_.Get());
    cmdlist4->SetComputeRoot32BitConstants(RaytracingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    cmdlist4->SetComputeRootShaderResourceView(RaytracingRootSignature::kAccelerationStructure,
                                               scene->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(RaytracingRootSignature::kBlueNoiseTexture,
                                            render_system.GetDescriptorHandleGPU(internal_descriptor_table));
    cmdlist4->SetComputeRootConstantBufferView(RaytracingRootSignature::kCameraBuffer, camera->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(RaytracingRootSignature::kSceneData,
                                            render_system.GetDescriptorHandleGPU(scene_data_base_index));
    cmdlist4->SetComputeRootDescriptorTable(RaytracingRootSignature::kOutput,
                                            render_system.GetDescriptorHandleGPU(output_uav_index));

    auto shader_record_size =
        align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
    dispatch_desc.HitGroupTable.StartAddress = hitgroup_shader_table->GetGPUVirtualAddress();
    dispatch_desc.HitGroupTable.SizeInBytes = hitgroup_shader_table->GetDesc().Width;
    dispatch_desc.HitGroupTable.StrideInBytes = shader_record_size;
    dispatch_desc.MissShaderTable.StartAddress = miss_shader_table->GetGPUVirtualAddress();
    dispatch_desc.MissShaderTable.SizeInBytes = miss_shader_table->GetDesc().Width;
    dispatch_desc.MissShaderTable.StrideInBytes = shader_record_size;
    dispatch_desc.RayGenerationShaderRecord.StartAddress = raygen_shader_table->GetGPUVirtualAddress();
    dispatch_desc.RayGenerationShaderRecord.SizeInBytes = raygen_shader_table->GetDesc().Width;
    dispatch_desc.Width = window_width;
    dispatch_desc.Height = window_height;
    dispatch_desc.Depth = 1;

    cmdlist4->SetPipelineState1(raytracing_pipeline_state_.Get());
    cmdlist4->DispatchRays(&dispatch_desc);

    D3D12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::UAV(output_direct_.Get()),
                                         CD3DX12_RESOURCE_BARRIER::UAV(output_indirect_.Get()),
                                         CD3DX12_RESOURCE_BARRIER::UAV(gbuffer_.Get())};

    cmdlist4->ResourceBarrier(ARRAYSIZE(barriers), barriers);
    cmdlist4->Close();

    render_system.PushCommandList(cmdlist4);
}

void RaytracingSystem::IntegrateTemporally(ID3D12Resource* camera,
                                           ID3D12Resource* prev_camera,
                                           uint32_t internal_descriptor_table,
                                           uint32_t output_descriptor_table,
                                           uint32_t history_descriptor_table)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto window_width = render_system.window_width();
    auto window_height = render_system.window_width();
    auto command_allocator = render_system.current_frame_command_allocator();
    auto descriptor_heap = render_system.current_frame_descriptor_heap();

    Constants constants{render_system.window_width(), render_system.window_height(), render_system.frame_count(), 0};

    ta_command_list_->Reset(command_allocator, nullptr);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    ta_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    ta_command_list_->SetComputeRootSignature(ta_root_signature_.Get());
    ta_command_list_->SetPipelineState(ta_pipeline_state_.Get());
    ta_command_list_->SetComputeRoot32BitConstants(
        TemporalAccumulateRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    ta_command_list_->SetComputeRootDescriptorTable(TemporalAccumulateRootSignature::kBlueNoiseTexture,
                                                    render_system.GetDescriptorHandleGPU(internal_descriptor_table));
    ta_command_list_->SetComputeRootConstantBufferView(TemporalAccumulateRootSignature::kCameraBuffer,
                                                       camera->GetGPUVirtualAddress());
    ta_command_list_->SetComputeRootConstantBufferView(TemporalAccumulateRootSignature::kPrevCameraBuffer,
                                                       prev_camera->GetGPUVirtualAddress());
    ta_command_list_->SetComputeRootDescriptorTable(TemporalAccumulateRootSignature::kCurrentFrameOutput,
                                                    render_system.GetDescriptorHandleGPU(output_descriptor_table));
    ta_command_list_->SetComputeRootDescriptorTable(TemporalAccumulateRootSignature::kHistory,
                                                    render_system.GetDescriptorHandleGPU(history_descriptor_table));

    ta_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    ta_command_list_->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::UAV(temporal_history_[render_system.frame_count() % 2].Get()));
    ta_command_list_->Close();
    render_system.PushCommandList(ta_command_list_);
}

void RaytracingSystem::Denoise(uint32_t descriptor_table)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto window_width = render_system.window_width();
    auto window_height = render_system.window_width();
    auto command_allocator = render_system.current_frame_command_allocator();
    auto descriptor_heap = render_system.current_frame_descriptor_heap();

    Constants constants{render_system.window_width(), render_system.window_height(), render_system.frame_count(), 1};

    eaw_command_list_->Reset(command_allocator, nullptr);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};
    eaw_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    eaw_command_list_->SetComputeRootSignature(eaw_root_signature_.Get());
    eaw_command_list_->SetPipelineState(eaw_pipeline_state_.Get());
    eaw_command_list_->SetComputeRoot32BitConstants(
        EAWDenoisingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    eaw_command_list_->SetComputeRootDescriptorTable(EAWDenoisingRootSignature::kOutput,
                                                     render_system.GetDescriptorHandleGPU(descriptor_table));

    eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    eaw_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(output_indirect_.Get()));


    constants.padding = 3;
    eaw_command_list_->SetComputeRoot32BitConstants(
        EAWDenoisingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    eaw_command_list_->SetComputeRootDescriptorTable(EAWDenoisingRootSignature::kOutput,
                                                     render_system.GetDescriptorHandleGPU(descriptor_table + 3));
    eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    eaw_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_.Get()));

    constants.padding = 5;
    eaw_command_list_->SetComputeRoot32BitConstants(
        EAWDenoisingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    eaw_command_list_->SetComputeRootDescriptorTable(EAWDenoisingRootSignature::kOutput,
                                                     render_system.GetDescriptorHandleGPU(descriptor_table + 6));
    eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    eaw_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(output_indirect_.Get()));

    constants.padding = 7;
    eaw_command_list_->SetComputeRoot32BitConstants(
        EAWDenoisingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    eaw_command_list_->SetComputeRootDescriptorTable(EAWDenoisingRootSignature::kOutput,
                                                     render_system.GetDescriptorHandleGPU(descriptor_table + 3));
    eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    eaw_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_.Get()));

    constants.padding = 9;
    eaw_command_list_->SetComputeRoot32BitConstants(
        EAWDenoisingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    eaw_command_list_->SetComputeRootDescriptorTable(EAWDenoisingRootSignature::kOutput,
                                                     render_system.GetDescriptorHandleGPU(descriptor_table + 6));
    eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    eaw_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(output_indirect_.Get()));

    eaw_command_list_->Close();
    render_system.PushCommandList(eaw_command_list_);
}

uint32_t RaytracingSystem::PopulateSceneDataDescriptorTable(GPUSceneData& scene_data)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    auto base_index = render_system.AllocateDescriptorRange(4);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uav_desc.Buffer.NumElements = scene_data.index_buffer->GetDesc().Width / sizeof(uint32_t);
    uav_desc.Buffer.StructureByteStride = sizeof(uint32_t);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.index_buffer, nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));

    uav_desc.Buffer.NumElements = scene_data.vertex_buffer->GetDesc().Width / sizeof(float);
    uav_desc.Buffer.StructureByteStride = sizeof(float);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.vertex_buffer, nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 1));

    uav_desc.Buffer.NumElements = scene_data.normal_buffer->GetDesc().Width / sizeof(float);
    uav_desc.Buffer.StructureByteStride = sizeof(float);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.normal_buffer, nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 2));

    uav_desc.Buffer.NumElements = scene_data.texcoord_buffer->GetDesc().Width / sizeof(XMFLOAT2);
    uav_desc.Buffer.StructureByteStride = sizeof(XMFLOAT2);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.texcoord_buffer, nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 3));

    return base_index;
}

uint32_t RaytracingSystem::PopulateOutputDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto base_index = render_system.AllocateDescriptorRange(3);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(
        output_direct_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));
    dx12api().device()->CreateUnorderedAccessView(
        output_indirect_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 1));

    // Create gbuffer output.
    uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 2));

    return base_index;
}

uint32_t RaytracingSystem::PopulateInternalDataDescritptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto base_index = render_system.AllocateDescriptorRange(1);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.PlaneSlice = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dx12api().device()->CreateShaderResourceView(
        blue_noise_texture(), &srv_desc, render_system.GetDescriptorHandleCPU(base_index));
    return base_index;
}

uint32_t RaytracingSystem::PopulateHistoryDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto base_index = render_system.AllocateDescriptorRange(3);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    auto src_index = (render_system.frame_count() + 1) % 2;
    auto dst_index = (src_index + 1) % 2;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(
        temporal_history_[src_index].Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));
    dx12api().device()->CreateUnorderedAccessView(
        temporal_history_[dst_index].Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 2));

    // We need 32 bits for depth here for stable reconstruction.
    uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        prev_gbuffer_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 1));

    return base_index;
}
uint32_t RaytracingSystem::PopulateEAWOutputDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto base_index = render_system.AllocateDescriptorRange(9);
    auto history_index = (render_system.frame_count() + 1) % 2;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // History buffer is an input.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        temporal_history_[history_index].Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));
    dx12api().device()->CreateUnorderedAccessView(
        output_indirect_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 2));
    // Create gbuffer output.
    uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 1));

    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        output_indirect_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 3));
    dx12api().device()->CreateUnorderedAccessView(
        output_temp_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 5));
    // Create gbuffer output.
    uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 4));

    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        output_temp_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 6));
    dx12api().device()->CreateUnorderedAccessView(
        output_indirect_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 8));
    // Create gbuffer output.
    uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 7));

    return base_index;
}
}  // namespace capsaicin
