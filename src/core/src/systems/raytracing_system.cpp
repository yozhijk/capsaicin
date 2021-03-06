#include "raytracing_system.h"

#include "src/common.h"
#include "src/systems/asset_load_system.h"
#include "src/systems/camera_system.h"
#include "src/systems/gui_system.h"
#include "src/systems/texture_system.h"
#include "src/systems/tlas_system.h"

namespace capsaicin
{
namespace
{
// Signature for GI raytracing pass.
namespace IndirectLightingRootSignature
{
enum
{
    kConstants = 0,
    kCameraBuffer,
    kPrevCameraBuffer,
    kAccelerationStructure,
    kBlueNoiseTexture,
    kTextures,
    kSceneData,
    kGBuffer,
    kIndirectLightingHistory,
    kPrevGBufferNormalDepth,
    kOutputIndirectLighting,
    kNumEntries
};
}

// Signature for primary visibility.
namespace PrimaryVisibilityRootSignature
{
enum
{
    kConstants = 0,
    kCameraBuffer,
    kAccelerationStructure,
    kBlueNoiseTexture,
    kGBuffer,
    kNumEntries
};
}

// Signature for direct lighting pass.
namespace DirectLightingRootSignature
{
enum
{
    kConstants = 0,
    kCameraBuffer,
    kAccelerationStructure,
    kBlueNoiseTexture,
    kTextures,
    kSceneData,
    kGBuffer,
    kOutputDirectLighting,
    kOutputNormalDepthAlbedo,
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

// Signature for spatial gather pass.
namespace SpatialGatherRootSignature
{
enum
{
    kConstants = 0,
    kOutput,
    kBlueNoise,
    kNumEntries
};
}

// Signature for a combine pass.
namespace CombineIlluminationRootSignature
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

struct TAConstants
{
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t padding;

    float    alpha;
    uint32_t adjust_velocity;
    uint32_t padding1;
    uint32_t padding2;
};

struct EAWConstants
{
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t stride;

    float normal_sigma;
    float depth_sigma;
    float luma_sigma;
    float padding;
};

// Find TLAS component and retrieve TLAS resource.
TLASComponent GetSceneTLASComponent(ComponentAccess& access, EntityQuery& entity_query)
{
    // Find scene TLAS.
    auto& tlases = access.Read<TLASComponent>();
    auto  entities =
        entity_query().Filter([&tlases](Entity e) { return tlases.HasComponent(e); }).entities();
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
    auto  entities =
        entity_query().Filter([&cameras](Entity e) { return cameras.HasComponent(e); }).entities();
    if (entities.size() != 1)
    {
        error("RaytracingSystem: no cameras found");
        throw std::runtime_error("RaytracingSystem: no cameras found");
    }

    return cameras.GetComponent(entities[0]);
}
}  // namespace

RaytracingSystem::RaytracingSystem(const RaytracingOptions& options) : options_(options)
{
    info("RaytracingSystem: Initializing");

    auto command_allocator = world().GetSystem<RenderSystem>().current_frame_command_allocator();

    // Create command list for visibility raytracing.
    rt_indirect_command_list_ = dx12api().CreateCommandList(command_allocator);
    rt_indirect_command_list_->Close();

    rt_primary_command_list_ = dx12api().CreateCommandList(command_allocator);
    rt_primary_command_list_->Close();

    rt_direct_command_list_ = dx12api().CreateCommandList(command_allocator);
    rt_direct_command_list_->Close();

    copy_gbuffer_command_list_ = dx12api().CreateCommandList(command_allocator);
    copy_gbuffer_command_list_->Close();

    indirect_ta_command_list_ = dx12api().CreateCommandList(command_allocator);
    indirect_ta_command_list_->Close();

    taa_command_list_ = dx12api().CreateCommandList(command_allocator);
    taa_command_list_->Close();

    eaw_command_list_ = dx12api().CreateCommandList(command_allocator);
    eaw_command_list_->Close();

    ci_command_list_ = dx12api().CreateCommandList(command_allocator);
    ci_command_list_->Close();

    sg_command_list_ = dx12api().CreateCommandList(command_allocator);
    sg_command_list_->Close();

    // Initialize raytracing pipeline.
    InitInidirectLightingPipeline();
    InitDirectLightingPipeline();
    InitPrimaryVisibilityPipeline();
    InitRenderStructures();
    InitTemporalAccumulatePipelines();
    InitEAWDenoisePipeline();
    InitSpatialGatherPipeline();
    InitCombinePipeline();
    CreateRenderOutputs();
}

RaytracingSystem::~RaytracingSystem() = default;

void RaytracingSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& settings = access.Write<SettingsComponent>()[0];

    auto tlas   = GetSceneTLASComponent(access, entity_query);
    auto camera = GetCamera(access, entity_query);

    auto& geometry_storage = world().GetSystem<AssetLoadSystem>().geometry_storage();

    GPUSceneData scene_data;
    scene_data.index_buffer     = geometry_storage.indices.Get();
    scene_data.vertex_buffer    = geometry_storage.vertices.Get();
    scene_data.normal_buffer    = geometry_storage.normals.Get();
    scene_data.texcoord_buffer  = geometry_storage.texcoords.Get();
    scene_data.mesh_desc_buffer = geometry_storage.mesh_descs.Get();

    auto scene_data_descriptor_table       = PopulateSceneDataDescriptorTable(scene_data);
    auto scene_textures_descriptor_table   = PopulateSceneTexturesDescriptorTable();
    auto internal_descriptor_table         = PopulateInternalDataDescritptorTable();
    auto history_descriptor_table          = PopulateIndirectHistoryDescritorTable();
    auto combined_history_descriptor_table = PopulateCombinedHistoryDescritorTable();
    auto eaw_descriptor_table              = PopulateEAWOutputDescritorTable();
    auto indirect_ta_input_descritor_table = PopulateIndirectTAInputDescritorTable();
    auto taa_input_descritor_table         = PopulateTAAInputDescritorTable();
    auto combine_descriptor_table          = PopulateCombineDescriptorTable();
    auto spatial_gather_descriptor_table   = PopulateSpatialGatherDescriptorTable();
    auto gbuffer_descriptor_table          = PopulateGBufferDescriptorTable();
    auto output_direct_descriptor_table    = PopulateOutputDirectDescriptorTable();
    auto output_indirect_descritor_table   = PopulateOutputIndirectDescriptorTable();
    auto output_normal_depth_albedo        = PopulateOutputNormalDepthAlbedo();
    auto prev_gbuffer_descriptor_table     = PopulatePrevGBufferDescriptorTable();

    // Save previous GBuffer.
    CopyGBuffer();

    // Raytrace visibility buffer.
    RaytracePrimaryVisibility(tlas.tlas.Get(),
                              camera.camera_buffer.Get(),
                              internal_descriptor_table,
                              gbuffer_descriptor_table);

    // Calculate direct illumination on GBuffer.
    CalculateDirectLighting(tlas.tlas.Get(),
                            camera.camera_buffer.Get(),
                            scene_data_descriptor_table,
                            scene_textures_descriptor_table,
                            internal_descriptor_table,
                            gbuffer_descriptor_table,
                            output_direct_descriptor_table,
                            output_normal_depth_albedo);

    // Do raytracing pass.
    CalculateIndirectLighting(tlas.tlas.Get(),
                              camera.camera_buffer.Get(),
                              camera.prev_camera_buffer.Get(),
                              scene_data_descriptor_table,
                              scene_textures_descriptor_table,
                              internal_descriptor_table,
                              gbuffer_descriptor_table,
                              combined_history_descriptor_table,
                              prev_gbuffer_descriptor_table,
                              output_indirect_descritor_table,
                              settings);

    // Do spatial gather.
    SpatialGather(spatial_gather_descriptor_table, internal_descriptor_table, settings);

    // Do temporal integration step.
    IntegrateTemporally(camera.camera_buffer.Get(),
                        camera.prev_camera_buffer.Get(),
                        internal_descriptor_table,
                        indirect_ta_input_descritor_table,
                        history_descriptor_table,
                        settings);

    // Denoise.
    Denoise(eaw_descriptor_table, settings);

    // Recombine.
    CombineIllumination(combine_descriptor_table, settings);

    // TAA
    ApplyTAA(camera.camera_buffer.Get(),
             camera.prev_camera_buffer.Get(),
             internal_descriptor_table,
             taa_input_descritor_table,
             combined_history_descriptor_table,
             settings);
}

ID3D12Resource* RaytracingSystem::current_frame_output()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    return combined_history_[render_system.frame_count() % 2].Get();
}

void RaytracingSystem::InitInidirectLightingPipeline()
{
    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE gbuffer_descriptor_range;
        gbuffer_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5);

        CD3DX12_DESCRIPTOR_RANGE indirect_history_descriptor_range;
        indirect_history_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 6);

        CD3DX12_DESCRIPTOR_RANGE prev_gbuffer_descriptor_range;
        prev_gbuffer_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 7);

        CD3DX12_DESCRIPTOR_RANGE output_indirect_descriptor_range;
        output_indirect_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 8);

        CD3DX12_DESCRIPTOR_RANGE scene_data_descriptor_range;
        scene_data_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0);

        CD3DX12_DESCRIPTOR_RANGE blue_noise_texture_descriptor_range;
        blue_noise_texture_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        CD3DX12_DESCRIPTOR_RANGE textures_descriptor_range;
        textures_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1024, 2);

        CD3DX12_ROOT_PARAMETER
        root_entries[IndirectLightingRootSignature::kNumEntries] = {};
        root_entries[IndirectLightingRootSignature::kConstants].InitAsConstants(sizeof(Constants),
                                                                                0);
        root_entries[IndirectLightingRootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[IndirectLightingRootSignature::kPrevCameraBuffer].InitAsConstantBufferView(2);
        root_entries[IndirectLightingRootSignature::kAccelerationStructure]
            .InitAsShaderResourceView(0);
        root_entries[IndirectLightingRootSignature::kBlueNoiseTexture].InitAsDescriptorTable(
            1, &blue_noise_texture_descriptor_range);
        root_entries[IndirectLightingRootSignature::kTextures].InitAsDescriptorTable(
            1, &textures_descriptor_range);
        root_entries[IndirectLightingRootSignature::kSceneData].InitAsDescriptorTable(
            1, &scene_data_descriptor_range);
        root_entries[IndirectLightingRootSignature::kGBuffer].InitAsDescriptorTable(
            1, &gbuffer_descriptor_range);
        root_entries[IndirectLightingRootSignature::kIndirectLightingHistory].InitAsDescriptorTable(
            1, &indirect_history_descriptor_range);
        root_entries[IndirectLightingRootSignature::kPrevGBufferNormalDepth].InitAsDescriptorTable(
            1, &prev_gbuffer_descriptor_range);
        root_entries[IndirectLightingRootSignature::kOutputIndirectLighting].InitAsDescriptorTable(
            1, &output_indirect_descriptor_range);

        CD3DX12_STATIC_SAMPLER_DESC sampler_desc(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(IndirectLightingRootSignature::kNumEntries, root_entries, 1, &sampler_desc);
        rt_indirect_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    std::vector<std::string> defines;
    if (options_.lowres_indirect)
    {
        defines.push_back("LOWRES_INDIRECT");
    }
    if (options_.gbuffer_feedback)
    {
        defines.push_back("GBUFFER_FEEDBACK");
    }

    auto shader = ShaderCompiler::instance().CompileFromFile(
        "../../../src/core/shaders/rt_indirect.hlsl", "lib_6_3", "", defines);

    CD3DX12_STATE_OBJECT_DESC pipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    auto                  lib     = pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = shader;

    lib->SetDXILLibrary(&libdxil);
    lib->DefineExport(L"CalculateIndirectDiffuseLighting");
    lib->DefineExport(L"ClosestHit");
    lib->DefineExport(L"ShadowAnyHit");
    lib->DefineExport(L"Miss");
    lib->DefineExport(L"ShadowMiss");

    auto hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(L"ClosestHit");
    hit_group->SetHitGroupExport(L"HitGroup");
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shadow_hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    shadow_hit_group->SetAnyHitShaderImport(L"ShadowAnyHit");
    shadow_hit_group->SetHitGroupExport(L"ShadowHitGroup");
    shadow_hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shader_config  = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size   = sizeof(XMFLOAT4);
    UINT attribute_size = sizeof(XMFLOAT4);
    shader_config->Config(payload_size, attribute_size);

    auto global_root_signature =
        pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_signature->SetRootSignature(rt_indirect_root_signature_.Get());

    auto pipeline_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    uint32_t max_recursion_depth = 1;
    pipeline_config->Config(max_recursion_depth);

    // Create the state object.
    ThrowIfFailed(device5->CreateStateObject(pipeline, IID_PPV_ARGS(&rt_indirect_pipeline_state_)),
                  "Couldn't create DirectX Raytracing state object.\n");

    ComPtr<ID3D12StateObjectProperties> state_object_props;
    ThrowIfFailed(rt_indirect_pipeline_state_.As(&state_object_props), "");

    auto raygen_shader_id =
        state_object_props->GetShaderIdentifier(L"CalculateIndirectDiffuseLighting");
    auto miss_shader_id            = state_object_props->GetShaderIdentifier(L"Miss");
    auto shadow_miss_shader_id     = state_object_props->GetShaderIdentifier(L"ShadowMiss");
    auto hitgroup_shader_id        = state_object_props->GetShaderIdentifier(L"HitGroup");
    auto shadow_hitgroup_shader_id = state_object_props->GetShaderIdentifier(L"ShadowHitGroup");

    uint32_t shader_record_size = align(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES),
                                        uint32_t(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    rt_indirect_raygen_shader_table =
        dx12api().CreateUploadBuffer(shader_record_size, raygen_shader_id);
    rt_indirect_hitgroup_shader_table = dx12api().CreateUploadBuffer(2 * shader_record_size);
    rt_indirect_miss_shader_table     = dx12api().CreateUploadBuffer(2 * shader_record_size);

    char* data = nullptr;
    rt_indirect_hitgroup_shader_table->Map(0, nullptr, (void**)&data);
    memcpy(data, hitgroup_shader_id, shader_record_size);
    memcpy(data + shader_record_size, shadow_hitgroup_shader_id, shader_record_size);
    rt_indirect_hitgroup_shader_table->Unmap(0, nullptr);

    rt_indirect_miss_shader_table->Map(0, nullptr, (void**)&data);
    memcpy(data, miss_shader_id, shader_record_size);
    memcpy(data + shader_record_size, shadow_miss_shader_id, shader_record_size);
    rt_indirect_miss_shader_table->Unmap(0, nullptr);
}

void RaytracingSystem::CreateRenderOutputs()
{
    info("RaytracingSystem: Initializing render outputs");

    auto& render_system = world().GetSystem<RenderSystem>();
    auto  window_width  = render_system.window_width();
    auto  window_height = render_system.window_height();

    {
        {
            auto texture_desc =
                CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
                                             window_width,
                                             window_height,
                                             1,
                                             0,
                                             1,
                                             0,
                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            output_direct_ =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            output_temp_[0] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            output_temp_[1] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            // Half resolution indirect.
            if (options_.lowres_indirect)
            {
                texture_desc.Width >>= 1;
                texture_desc.Height >>= 1;
            }

            output_indirect_ =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            indirect_temp_ =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // History data.
        {
            auto texture_desc =
                CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
                                             window_width,
                                             window_height,
                                             1,
                                             0,
                                             1,
                                             0,
                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            indirect_history_[0] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            indirect_history_[1] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            combined_history_[0] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            combined_history_[1] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            moments_history_[0] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            moments_history_[1] =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            // GBuffers.
            texture_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            gbuffer_normal_depth_ =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            prev_gbuffer_normal_depth_ =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            texture_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            gbuffer_geo_ =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            gbuffer_albedo_ =
                dx12api().CreateResource(texture_desc,
                                         CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
}

void RaytracingSystem::InitTemporalAccumulatePipelines()
{
    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);

        CD3DX12_DESCRIPTOR_RANGE history_descriptor_range;
        history_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 2);

        CD3DX12_DESCRIPTOR_RANGE blue_noise_texture_descriptor_range;
        blue_noise_texture_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER
        root_entries[TemporalAccumulateRootSignature::kNumEntries] = {};
        root_entries[TemporalAccumulateRootSignature::kConstants].InitAsConstants(
            sizeof(TAConstants), 0);
        root_entries[TemporalAccumulateRootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[TemporalAccumulateRootSignature::kPrevCameraBuffer].InitAsConstantBufferView(
            2);
        root_entries[TemporalAccumulateRootSignature::kBlueNoiseTexture].InitAsDescriptorTable(
            1, &blue_noise_texture_descriptor_range);
        root_entries[TemporalAccumulateRootSignature::kCurrentFrameOutput].InitAsDescriptorTable(
            1, &output_descriptor_range);
        root_entries[TemporalAccumulateRootSignature::kHistory].InitAsDescriptorTable(
            1, &history_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(TemporalAccumulateRootSignature::kNumEntries, root_entries);
        ta_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    // Temporal accumulation.
    {
        std::vector<std::string> defines;

        if (options_.lowres_indirect)
        {
            defines.push_back("UPSCALE2X");
        }

        if (options_.use_variance)
        {
            // We only use SVGF for fullres.
            defines.push_back("CALCULATE_VARIANCE");
        }

        auto shader = ShaderCompiler::instance().CompileFromFile(
            "../../../src/core/shaders/temporal_accumulation.hlsl",
            "cs_6_3",
            "Accumulate",
            defines);

        ta_pipeline_state_ = dx12api().CreateComputePipelineState(shader, ta_root_signature_.Get());
    }

    // Temporal antialiasing.
    {
        auto shader = ShaderCompiler::instance().CompileFromFile(
            "../../../src/core/shaders/temporal_accumulation.hlsl", "cs_6_3", "TAA");
        taa_pipeline_state_ =
            dx12api().CreateComputePipelineState(shader, ta_root_signature_.Get());
    }
}

void RaytracingSystem::InitRenderStructures()
{
    auto& texture_system = world().GetSystem<TextureSystem>();
    blue_noise_texture_  = texture_system.GetTexture("bluenoise256.png");
}

void RaytracingSystem::InitEAWDenoisePipeline()
{
    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0);

        CD3DX12_ROOT_PARAMETER
        root_entries[EAWDenoisingRootSignature::kNumEntries] = {};
        root_entries[EAWDenoisingRootSignature::kConstants].InitAsConstants(sizeof(EAWConstants),
                                                                            0);
        root_entries[EAWDenoisingRootSignature::kOutput].InitAsDescriptorTable(
            1, &output_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(EAWDenoisingRootSignature::kNumEntries, root_entries);
        eaw_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    std::vector<std::string> defines;

    if (options_.use_variance)
    {
        // We only use SVGF for fullres.
        defines.push_back("USE_VARIANCE");
    }

    {
        auto shader = ShaderCompiler::instance().CompileFromFile(
            "../../../src/core/shaders/eaw_blur.hlsl", "cs_6_3", "Blur", defines);

        eaw_pipeline_state_ =
            dx12api().CreateComputePipelineState(shader, eaw_root_signature_.Get());
    }

    {
        auto shader = ShaderCompiler::instance().CompileFromFile(
            "../../../src/core/shaders/eaw_blur.hlsl", "cs_6_3", "BlurDisocclusion", defines);

        deaw_pipeline_state_ =
            dx12api().CreateComputePipelineState(shader, eaw_root_signature_.Get());
    }
}

void RaytracingSystem::InitCombinePipeline()
{
    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0);

        CD3DX12_ROOT_PARAMETER
        root_entries[CombineIlluminationRootSignature::kNumEntries] = {};
        root_entries[CombineIlluminationRootSignature::kConstants].InitAsConstants(
            sizeof(Constants), 0);
        root_entries[CombineIlluminationRootSignature::kOutput].InitAsDescriptorTable(
            1, &output_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(CombineIlluminationRootSignature::kNumEntries, root_entries);
        ci_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    auto shader = ShaderCompiler::instance().CompileFromFile(
        "../../../src/core/shaders/combine_illumination.hlsl", "cs_6_3", "Combine");

    ci_pipeline_state_ = dx12api().CreateComputePipelineState(shader, ci_root_signature_.Get());
}

void RaytracingSystem::InitSpatialGatherPipeline()
{
    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0);

        CD3DX12_DESCRIPTOR_RANGE blue_noise_texture_descriptor_range;
        blue_noise_texture_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER
        root_entries[SpatialGatherRootSignature::kNumEntries] = {};
        root_entries[SpatialGatherRootSignature::kConstants].InitAsConstants(sizeof(EAWConstants),
                                                                             0);
        root_entries[SpatialGatherRootSignature::kOutput].InitAsDescriptorTable(
            1, &output_descriptor_range);
        root_entries[SpatialGatherRootSignature::kBlueNoise].InitAsDescriptorTable(
            1, &blue_noise_texture_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(SpatialGatherRootSignature::kNumEntries, root_entries);
        sg_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    std::vector<std::string> defines;

    if (options_.lowres_indirect)
    {
        defines.push_back("UPSCALE2X");
    }

    auto shader = ShaderCompiler::instance().CompileFromFile(
        "../../../src/core/shaders/spatial_gather.hlsl", "cs_6_3", "Gather", defines);

    sg_pipeline_state_ = dx12api().CreateComputePipelineState(shader, sg_root_signature_.Get());
}

void RaytracingSystem::InitPrimaryVisibilityPipeline()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  window_width  = render_system.window_width();
    auto  window_height = render_system.window_height();

    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE gbuffer_descriptor_range;
        gbuffer_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_DESCRIPTOR_RANGE blue_noise_texture_descriptor_range;
        blue_noise_texture_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        CD3DX12_ROOT_PARAMETER
        root_entries[PrimaryVisibilityRootSignature::kNumEntries] = {};
        root_entries[PrimaryVisibilityRootSignature::kConstants].InitAsConstants(sizeof(Constants),
                                                                                 0);
        root_entries[PrimaryVisibilityRootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[PrimaryVisibilityRootSignature::kAccelerationStructure]
            .InitAsShaderResourceView(0);
        root_entries[PrimaryVisibilityRootSignature::kBlueNoiseTexture].InitAsDescriptorTable(
            1, &blue_noise_texture_descriptor_range);
        root_entries[PrimaryVisibilityRootSignature::kGBuffer].InitAsDescriptorTable(
            1, &gbuffer_descriptor_range);

        CD3DX12_STATIC_SAMPLER_DESC sampler_desc(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(PrimaryVisibilityRootSignature::kNumEntries, root_entries, 1, &sampler_desc);

        rt_primary_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    auto shader = ShaderCompiler::instance().CompileFromFile(
        "../../../src/core/shaders/rt_primary_visibility.hlsl", "lib_6_3", "");

    CD3DX12_STATE_OBJECT_DESC pipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    auto                  lib     = pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = shader;

    lib->SetDXILLibrary(&libdxil);
    lib->DefineExport(L"TracePrimaryRays");
    lib->DefineExport(L"ClosestHit");

    auto hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(L"ClosestHit");
    hit_group->SetHitGroupExport(L"HitGroup");
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    // Ray payload: 2xfloat barycentrics + 1xuint instance id + 1xuint shape id.
    auto shader_config  = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size   = 2 * sizeof(float) + 2 * sizeof(uint32_t);
    UINT attribute_size = sizeof(XMFLOAT4);
    shader_config->Config(payload_size, attribute_size);

    auto global_root_signature =
        pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_signature->SetRootSignature(rt_primary_root_signature_.Get());

    auto pipeline_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    uint32_t max_recursion_depth = 1;
    pipeline_config->Config(max_recursion_depth);

    // Create the state object.
    ThrowIfFailed(device5->CreateStateObject(pipeline, IID_PPV_ARGS(&rt_primary_pipeline_state_)),
                  "Couldn't create DirectX Raytracing state object.\n");

    ComPtr<ID3D12StateObjectProperties> state_object_props;
    ThrowIfFailed(rt_primary_pipeline_state_.As(&state_object_props), "");

    auto raygen_shader_id   = state_object_props->GetShaderIdentifier(L"TracePrimaryRays");
    auto hitgroup_shader_id = state_object_props->GetShaderIdentifier(L"HitGroup");

    uint32_t shader_record_size = align(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES),
                                        uint32_t(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    rt_primary_raygen_shader_table =
        dx12api().CreateUploadBuffer(shader_record_size, raygen_shader_id);
    rt_primary_hitgroup_shader_table =
        dx12api().CreateUploadBuffer(shader_record_size, hitgroup_shader_id);
    rt_primary_miss_shader_table = dx12api().CreateUploadBuffer(shader_record_size);

    char* data = nullptr;
    rt_primary_miss_shader_table->Map(0, nullptr, (void**)&data);
    memset(data, 0, shader_record_size);
    rt_primary_miss_shader_table->Unmap(0, nullptr);
}

void RaytracingSystem::InitDirectLightingPipeline()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  window_width  = render_system.window_width();
    auto  window_height = render_system.window_height();

    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE gbuffer_descriptor_range;
        gbuffer_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5);

        CD3DX12_DESCRIPTOR_RANGE blue_noise_texture_descriptor_range;
        blue_noise_texture_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        CD3DX12_DESCRIPTOR_RANGE output_direct_range;
        output_direct_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 6);
        CD3DX12_DESCRIPTOR_RANGE output_normal_depth_albedo;
        output_normal_depth_albedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 7);

        CD3DX12_DESCRIPTOR_RANGE scene_data_descriptor_range;
        scene_data_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0);

        CD3DX12_DESCRIPTOR_RANGE textures_descriptor_range;
        textures_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1024, 2);

        CD3DX12_ROOT_PARAMETER
        root_entries[DirectLightingRootSignature::kNumEntries] = {};
        root_entries[DirectLightingRootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
        root_entries[DirectLightingRootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[DirectLightingRootSignature::kAccelerationStructure].InitAsShaderResourceView(
            0);
        root_entries[DirectLightingRootSignature::kBlueNoiseTexture].InitAsDescriptorTable(
            1, &blue_noise_texture_descriptor_range);
        root_entries[DirectLightingRootSignature::kTextures].InitAsDescriptorTable(
            1, &textures_descriptor_range);
        root_entries[DirectLightingRootSignature::kSceneData].InitAsDescriptorTable(
            1, &scene_data_descriptor_range);
        root_entries[DirectLightingRootSignature::kGBuffer].InitAsDescriptorTable(
            1, &gbuffer_descriptor_range);
        root_entries[DirectLightingRootSignature::kOutputDirectLighting].InitAsDescriptorTable(
            1, &output_direct_range);
        root_entries[DirectLightingRootSignature::kOutputNormalDepthAlbedo].InitAsDescriptorTable(
            1, &output_normal_depth_albedo);

        CD3DX12_STATIC_SAMPLER_DESC sampler_desc(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(DirectLightingRootSignature::kNumEntries, root_entries, 1, &sampler_desc);

        rt_direct_root_signature_ = dx12api().CreateRootSignature(desc);
    }

    auto shader = ShaderCompiler::instance().CompileFromFile(
        "../../../src/core/shaders/rt_direct_lighting.hlsl", "lib_6_3", "");

    CD3DX12_STATE_OBJECT_DESC pipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    auto                  lib     = pipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = shader;

    lib->SetDXILLibrary(&libdxil);
    lib->DefineExport(L"CalculateDirectLighting");
    lib->DefineExport(L"ShadowAnyHit");
    lib->DefineExport(L"ShadowMiss");

    auto hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetAnyHitShaderImport(L"ShadowAnyHit");
    hit_group->SetHitGroupExport(L"HitGroup");
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    // Ray payload: 1xbool
    auto shader_config  = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size   = sizeof(uint32_t);
    UINT attribute_size = sizeof(XMFLOAT4);
    shader_config->Config(payload_size, attribute_size);

    auto global_root_signature =
        pipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    global_root_signature->SetRootSignature(rt_direct_root_signature_.Get());

    auto pipeline_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    uint32_t max_recursion_depth = 1;
    pipeline_config->Config(max_recursion_depth);

    // Create the state object.
    ThrowIfFailed(device5->CreateStateObject(pipeline, IID_PPV_ARGS(&rt_direct_pipeline_state_)),
                  "Couldn't create DirectX Raytracing state object.\n");

    ComPtr<ID3D12StateObjectProperties> state_object_props;
    ThrowIfFailed(rt_direct_pipeline_state_.As(&state_object_props), "");

    auto raygen_shader_id   = state_object_props->GetShaderIdentifier(L"CalculateDirectLighting");
    auto hitgroup_shader_id = state_object_props->GetShaderIdentifier(L"HitGroup");
    auto miss_shader_id     = state_object_props->GetShaderIdentifier(L"ShadowMiss");

    uint32_t shader_record_size = align(uint32_t(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES),
                                        uint32_t(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

    rt_direct_raygen_shader_table =
        dx12api().CreateUploadBuffer(shader_record_size, raygen_shader_id);
    rt_direct_hitgroup_shader_table =
        dx12api().CreateUploadBuffer(shader_record_size, hitgroup_shader_id);
    rt_direct_miss_shader_table = dx12api().CreateUploadBuffer(shader_record_size, miss_shader_id);
}

void RaytracingSystem::CopyGBuffer()
{
    auto& render_system     = world().GetSystem<RenderSystem>();
    auto  window_width      = render_system.window_width();
    auto  window_height     = render_system.window_height();
    auto  command_allocator = render_system.current_frame_command_allocator();

    copy_gbuffer_command_list_->Reset(command_allocator, nullptr);

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(gbuffer_normal_depth_.Get(),
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_STATE_COPY_SOURCE),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(prev_gbuffer_normal_depth_.Get(),
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_STATE_COPY_DEST)

        };

        copy_gbuffer_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;
    dst_loc.pResource        = prev_gbuffer_normal_depth_.Get();

    D3D12_TEXTURE_COPY_LOCATION src_loc = dst_loc;
    src_loc.pResource                   = gbuffer_normal_depth_.Get();

    D3D12_BOX copy_box{0, 0, 0, window_width, window_height, 1};
    copy_gbuffer_command_list_->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, &copy_box);

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(gbuffer_normal_depth_.Get(),
                                                 D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(prev_gbuffer_normal_depth_.Get(),
                                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};

        copy_gbuffer_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    copy_gbuffer_command_list_->Close();
    render_system.PushCommandList(copy_gbuffer_command_list_);
}

void RaytracingSystem::RaytracePrimaryVisibility(ID3D12Resource* scene,
                                                 ID3D12Resource* camera,
                                                 uint32_t        internal_descriptor_table,
                                                 uint32_t        gbuffer_descriptor_table)
{
    auto& render_system        = world().GetSystem<RenderSystem>();
    auto  window_width         = render_system.window_width();
    auto  window_height        = render_system.window_height();
    auto  command_allocator    = render_system.current_frame_command_allocator();
    auto  descriptor_heap      = render_system.current_frame_descriptor_heap();
    auto  timestamp_query_heap = render_system.current_frame_timestamp_query_heap();

    auto [start_time_index, end_time_index] =
        render_system.AllocateTimestampQueryPair("RaytracePrimaryVisibility");

    Constants constants{render_system.window_width(),
                        render_system.window_height(),
                        render_system.frame_count(),
                        0};

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    rt_primary_command_list_->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    cmdlist4->Reset(command_allocator, nullptr);
    cmdlist4->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};
    cmdlist4->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    cmdlist4->SetComputeRootSignature(rt_primary_root_signature_.Get());
    cmdlist4->SetComputeRoot32BitConstants(
        PrimaryVisibilityRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    cmdlist4->SetComputeRootShaderResourceView(
        PrimaryVisibilityRootSignature::kAccelerationStructure, scene->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(
        PrimaryVisibilityRootSignature::kBlueNoiseTexture,
        render_system.GetDescriptorHandleGPU(internal_descriptor_table));
    cmdlist4->SetComputeRootConstantBufferView(PrimaryVisibilityRootSignature::kCameraBuffer,
                                               camera->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(
        PrimaryVisibilityRootSignature::kGBuffer,
        render_system.GetDescriptorHandleGPU(gbuffer_descriptor_table));

    auto shader_record_size =
        align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
    dispatch_desc.HitGroupTable.StartAddress =
        rt_primary_hitgroup_shader_table->GetGPUVirtualAddress();
    dispatch_desc.HitGroupTable.SizeInBytes   = rt_primary_hitgroup_shader_table->GetDesc().Width;
    dispatch_desc.HitGroupTable.StrideInBytes = shader_record_size;
    dispatch_desc.MissShaderTable.StartAddress =
        rt_primary_miss_shader_table->GetGPUVirtualAddress();
    dispatch_desc.MissShaderTable.SizeInBytes   = rt_primary_miss_shader_table->GetDesc().Width;
    dispatch_desc.MissShaderTable.StrideInBytes = shader_record_size;
    dispatch_desc.RayGenerationShaderRecord.StartAddress =
        rt_primary_raygen_shader_table->GetGPUVirtualAddress();
    dispatch_desc.RayGenerationShaderRecord.SizeInBytes =
        rt_primary_raygen_shader_table->GetDesc().Width;
    dispatch_desc.Width  = window_width;
    dispatch_desc.Height = window_height;
    dispatch_desc.Depth  = 1;

    cmdlist4->SetPipelineState1(rt_primary_pipeline_state_.Get());
    cmdlist4->DispatchRays(&dispatch_desc);

    D3D12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::UAV(gbuffer_geo_.Get())};

    cmdlist4->ResourceBarrier(ARRAYSIZE(barriers), barriers);

    cmdlist4->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);

    cmdlist4->Close();

    render_system.PushCommandList(cmdlist4);
}

void RaytracingSystem::CalculateDirectLighting(ID3D12Resource* scene,
                                               ID3D12Resource* camera,
                                               uint32_t        scene_data_descriptor_table,
                                               uint32_t        scene_textures_descriptor_table,
                                               uint32_t        internal_descriptor_table,
                                               uint32_t        gbuffer_descriptor_table,
                                               uint32_t        output_direct_descriptor_table,
                                               uint32_t        output_normal_depth_descriptor_table)
{
    auto& render_system        = world().GetSystem<RenderSystem>();
    auto  window_width         = render_system.window_width();
    auto  window_height        = render_system.window_height();
    auto  command_allocator    = render_system.current_frame_command_allocator();
    auto  descriptor_heap      = render_system.current_frame_descriptor_heap();
    auto  timestamp_query_heap = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] =
        render_system.AllocateTimestampQueryPair("RT Direct lighting");

    Constants constants{render_system.window_width(),
                        render_system.window_height(),
                        render_system.frame_count(),
                        0};

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    rt_direct_command_list_->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    cmdlist4->Reset(command_allocator, nullptr);
    cmdlist4->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    cmdlist4->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    cmdlist4->SetComputeRootSignature(rt_direct_root_signature_.Get());
    cmdlist4->SetComputeRoot32BitConstants(
        DirectLightingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    cmdlist4->SetComputeRootShaderResourceView(DirectLightingRootSignature::kAccelerationStructure,
                                               scene->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(
        DirectLightingRootSignature::kBlueNoiseTexture,
        render_system.GetDescriptorHandleGPU(internal_descriptor_table));
    cmdlist4->SetComputeRootConstantBufferView(DirectLightingRootSignature::kCameraBuffer,
                                               camera->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(
        DirectLightingRootSignature::kSceneData,
        render_system.GetDescriptorHandleGPU(scene_data_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        DirectLightingRootSignature::kTextures,
        render_system.GetDescriptorHandleGPU(scene_textures_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        DirectLightingRootSignature::kGBuffer,
        render_system.GetDescriptorHandleGPU(gbuffer_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        DirectLightingRootSignature::kOutputDirectLighting,
        render_system.GetDescriptorHandleGPU(output_direct_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        DirectLightingRootSignature::kOutputNormalDepthAlbedo,
        render_system.GetDescriptorHandleGPU(output_normal_depth_descriptor_table));

    auto shader_record_size =
        align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
    dispatch_desc.HitGroupTable.StartAddress =
        rt_direct_hitgroup_shader_table->GetGPUVirtualAddress();
    dispatch_desc.HitGroupTable.SizeInBytes   = rt_direct_hitgroup_shader_table->GetDesc().Width;
    dispatch_desc.HitGroupTable.StrideInBytes = shader_record_size;
    dispatch_desc.MissShaderTable.StartAddress =
        rt_direct_miss_shader_table->GetGPUVirtualAddress();
    dispatch_desc.MissShaderTable.SizeInBytes   = rt_direct_miss_shader_table->GetDesc().Width;
    dispatch_desc.MissShaderTable.StrideInBytes = shader_record_size;
    dispatch_desc.RayGenerationShaderRecord.StartAddress =
        rt_direct_raygen_shader_table->GetGPUVirtualAddress();
    dispatch_desc.RayGenerationShaderRecord.SizeInBytes =
        rt_direct_raygen_shader_table->GetDesc().Width;
    dispatch_desc.Width  = window_width;
    dispatch_desc.Height = window_height;
    dispatch_desc.Depth  = 1;

    cmdlist4->SetPipelineState1(rt_direct_pipeline_state_.Get());
    cmdlist4->DispatchRays(&dispatch_desc);

    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(output_direct_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(gbuffer_albedo_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(gbuffer_normal_depth_.Get())};

    cmdlist4->ResourceBarrier(ARRAYSIZE(barriers), barriers);

    cmdlist4->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);

    cmdlist4->Close();

    render_system.PushCommandList(cmdlist4);
}

void RaytracingSystem::CalculateIndirectLighting(ID3D12Resource* scene,
                                                 ID3D12Resource* camera,
                                                 ID3D12Resource* prev_camera,
                                                 uint32_t        scene_data_base_index,
                                                 uint32_t        scene_textures_descriptor_table,
                                                 uint32_t        internal_descriptor_table,
                                                 uint32_t        gbuffer_descriptor_table,
                                                 uint32_t        indirect_history_descriptor_table,
                                                 uint32_t        prev_gbuffer_descriptor_table,
                                                 uint32_t        output_indirect_descriptor_table,
                                                 const SettingsComponent& settings)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  width         = render_system.window_width();
    auto  height        = render_system.window_height();

    if (options_.lowres_indirect)
    {
        width >>= 1;
        height >>= 1;
    }

    auto command_allocator    = render_system.current_frame_command_allocator();
    auto descriptor_heap      = render_system.current_frame_descriptor_heap();
    auto timestamp_query_heap = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] =
        render_system.AllocateTimestampQueryPair("RT Indirect diffuse");

    Constants constants{width, height, render_system.frame_count(), settings.num_diffuse_bounces};

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    rt_indirect_command_list_->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    cmdlist4->Reset(command_allocator, nullptr);
    cmdlist4->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    cmdlist4->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    cmdlist4->SetComputeRootSignature(rt_indirect_root_signature_.Get());
    cmdlist4->SetComputeRoot32BitConstants(
        IndirectLightingRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    cmdlist4->SetComputeRootShaderResourceView(
        IndirectLightingRootSignature::kAccelerationStructure, scene->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(
        IndirectLightingRootSignature::kBlueNoiseTexture,
        render_system.GetDescriptorHandleGPU(internal_descriptor_table));
    cmdlist4->SetComputeRootConstantBufferView(IndirectLightingRootSignature::kCameraBuffer,
                                               camera->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootConstantBufferView(IndirectLightingRootSignature::kPrevCameraBuffer,
                                               prev_camera->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(
        IndirectLightingRootSignature::kSceneData,
        render_system.GetDescriptorHandleGPU(scene_data_base_index));
    cmdlist4->SetComputeRootDescriptorTable(
        IndirectLightingRootSignature::kTextures,
        render_system.GetDescriptorHandleGPU(scene_textures_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        IndirectLightingRootSignature::kGBuffer,
        render_system.GetDescriptorHandleGPU(gbuffer_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        IndirectLightingRootSignature::kIndirectLightingHistory,
        render_system.GetDescriptorHandleGPU(indirect_history_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        IndirectLightingRootSignature::kPrevGBufferNormalDepth,
        render_system.GetDescriptorHandleGPU(prev_gbuffer_descriptor_table));
    cmdlist4->SetComputeRootDescriptorTable(
        IndirectLightingRootSignature::kOutputIndirectLighting,
        render_system.GetDescriptorHandleGPU(output_indirect_descriptor_table));

    auto shader_record_size =
        align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
    dispatch_desc.HitGroupTable.StartAddress =
        rt_indirect_hitgroup_shader_table->GetGPUVirtualAddress();
    dispatch_desc.HitGroupTable.SizeInBytes   = rt_indirect_hitgroup_shader_table->GetDesc().Width;
    dispatch_desc.HitGroupTable.StrideInBytes = shader_record_size;
    dispatch_desc.MissShaderTable.StartAddress =
        rt_indirect_miss_shader_table->GetGPUVirtualAddress();
    dispatch_desc.MissShaderTable.SizeInBytes   = rt_indirect_miss_shader_table->GetDesc().Width;
    dispatch_desc.MissShaderTable.StrideInBytes = shader_record_size;
    dispatch_desc.RayGenerationShaderRecord.StartAddress =
        rt_indirect_raygen_shader_table->GetGPUVirtualAddress();
    dispatch_desc.RayGenerationShaderRecord.SizeInBytes =
        rt_indirect_raygen_shader_table->GetDesc().Width;
    dispatch_desc.Width  = width;
    dispatch_desc.Height = height;
    dispatch_desc.Depth  = 1;

    cmdlist4->SetPipelineState1(rt_indirect_pipeline_state_.Get());
    cmdlist4->DispatchRays(&dispatch_desc);

    D3D12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::UAV(output_indirect_.Get())};

    cmdlist4->ResourceBarrier(ARRAYSIZE(barriers), barriers);

    cmdlist4->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);

    cmdlist4->Close();

    render_system.PushCommandList(cmdlist4);
}

void RaytracingSystem::IntegrateTemporally(ID3D12Resource*          camera,
                                           ID3D12Resource*          prev_camera,
                                           uint32_t                 internal_descriptor_table,
                                           uint32_t                 output_descriptor_table,
                                           uint32_t                 history_descriptor_table,
                                           const SettingsComponent& settings)
{
    auto& render_system        = world().GetSystem<RenderSystem>();
    auto  width                = render_system.window_width();
    auto  height               = render_system.window_height();
    auto  command_allocator    = render_system.current_frame_command_allocator();
    auto  descriptor_heap      = render_system.current_frame_descriptor_heap();
    auto  timestamp_query_heap = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] =
        render_system.AllocateTimestampQueryPair("Temporal upscale");

    TAConstants constants{
        width, height, render_system.frame_count(), 0, settings.temporal_upscale_feedback, 0};

    indirect_ta_command_list_->Reset(command_allocator, nullptr);
    indirect_ta_command_list_->EndQuery(
        timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    indirect_ta_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    indirect_ta_command_list_->SetComputeRootSignature(ta_root_signature_.Get());
    indirect_ta_command_list_->SetPipelineState(ta_pipeline_state_.Get());
    indirect_ta_command_list_->SetComputeRoot32BitConstants(
        TemporalAccumulateRootSignature::kConstants, sizeof(TAConstants) >> 2, &constants, 0);
    indirect_ta_command_list_->SetComputeRootDescriptorTable(
        TemporalAccumulateRootSignature::kBlueNoiseTexture,
        render_system.GetDescriptorHandleGPU(internal_descriptor_table));
    indirect_ta_command_list_->SetComputeRootConstantBufferView(
        TemporalAccumulateRootSignature::kCameraBuffer, camera->GetGPUVirtualAddress());
    indirect_ta_command_list_->SetComputeRootConstantBufferView(
        TemporalAccumulateRootSignature::kPrevCameraBuffer, prev_camera->GetGPUVirtualAddress());
    indirect_ta_command_list_->SetComputeRootDescriptorTable(
        TemporalAccumulateRootSignature::kCurrentFrameOutput,
        render_system.GetDescriptorHandleGPU(output_descriptor_table));
    indirect_ta_command_list_->SetComputeRootDescriptorTable(
        TemporalAccumulateRootSignature::kHistory,
        render_system.GetDescriptorHandleGPU(history_descriptor_table));

    indirect_ta_command_list_->Dispatch(ceil_divide(width, 8), ceil_divide(height, 8), 1);

    indirect_ta_command_list_->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::UAV(
            indirect_history_[render_system.current_gpu_frame_index() % 2].Get()));
    indirect_ta_command_list_->ResourceBarrier(
        1,
        &CD3DX12_RESOURCE_BARRIER::UAV(
            moments_history_[render_system.current_gpu_frame_index() % 2].Get()));

    indirect_ta_command_list_->EndQuery(
        timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);
    indirect_ta_command_list_->Close();
    render_system.PushCommandList(indirect_ta_command_list_);
}

void RaytracingSystem::ApplyTAA(ID3D12Resource*          camera,
                                ID3D12Resource*          prev_camera,
                                uint32_t                 internal_descriptor_table,
                                uint32_t                 output_descriptor_table,
                                uint32_t                 history_descriptor_table,
                                const SettingsComponent& settings)
{
    auto& render_system                     = world().GetSystem<RenderSystem>();
    auto  window_width                      = render_system.window_width();
    auto  window_height                     = render_system.window_height();
    auto  command_allocator                 = render_system.current_frame_command_allocator();
    auto  descriptor_heap                   = render_system.current_frame_descriptor_heap();
    auto  timestamp_query_heap              = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] = render_system.AllocateTimestampQueryPair("TAA");

    TAConstants constants{render_system.window_width(),
                          render_system.window_height(),
                          render_system.frame_count(),
                          0,
                          settings.taa_feedback,
                          1};

    taa_command_list_->Reset(command_allocator, nullptr);
    taa_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    taa_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    taa_command_list_->SetComputeRootSignature(ta_root_signature_.Get());
    taa_command_list_->SetPipelineState(taa_pipeline_state_.Get());
    taa_command_list_->SetComputeRoot32BitConstants(
        TemporalAccumulateRootSignature::kConstants, sizeof(TAConstants) >> 2, &constants, 0);
    taa_command_list_->SetComputeRootDescriptorTable(
        TemporalAccumulateRootSignature::kBlueNoiseTexture,
        render_system.GetDescriptorHandleGPU(internal_descriptor_table));
    taa_command_list_->SetComputeRootConstantBufferView(
        TemporalAccumulateRootSignature::kCameraBuffer, camera->GetGPUVirtualAddress());
    taa_command_list_->SetComputeRootConstantBufferView(
        TemporalAccumulateRootSignature::kPrevCameraBuffer, prev_camera->GetGPUVirtualAddress());
    taa_command_list_->SetComputeRootDescriptorTable(
        TemporalAccumulateRootSignature::kCurrentFrameOutput,
        render_system.GetDescriptorHandleGPU(output_descriptor_table));
    taa_command_list_->SetComputeRootDescriptorTable(
        TemporalAccumulateRootSignature::kHistory,
        render_system.GetDescriptorHandleGPU(history_descriptor_table));
    taa_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    taa_command_list_->ResourceBarrier(1,
                                       &CD3DX12_RESOURCE_BARRIER::UAV(combined_history_[0].Get()));
    taa_command_list_->ResourceBarrier(1,
                                       &CD3DX12_RESOURCE_BARRIER::UAV(combined_history_[1].Get()));
    taa_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);
    taa_command_list_->Close();

    render_system.PushCommandList(taa_command_list_);
}

void RaytracingSystem::CombineIllumination(uint32_t                 output_descriptor_table,
                                           const SettingsComponent& settings)
{
    auto& render_system        = world().GetSystem<RenderSystem>();
    auto  window_width         = render_system.window_width();
    auto  window_height        = render_system.window_height();
    auto  command_allocator    = render_system.current_frame_command_allocator();
    auto  descriptor_heap      = render_system.current_frame_descriptor_heap();
    auto  timestamp_query_heap = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] =
        render_system.AllocateTimestampQueryPair("Combine illumination");

    Constants constants{render_system.window_width(),
                        render_system.window_height(),
                        render_system.frame_count(),
                        settings.output};

    ci_command_list_->Reset(command_allocator, nullptr);
    ci_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    ci_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    ci_command_list_->SetComputeRootSignature(ci_root_signature_.Get());
    ci_command_list_->SetPipelineState(ci_pipeline_state_.Get());
    ci_command_list_->SetComputeRoot32BitConstants(
        CombineIlluminationRootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    ci_command_list_->SetComputeRootDescriptorTable(
        CombineIlluminationRootSignature::kOutput,
        render_system.GetDescriptorHandleGPU(output_descriptor_table));
    ci_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    ci_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));
    ci_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);
    ci_command_list_->Close();
    render_system.PushCommandList(ci_command_list_);
}

void RaytracingSystem::Denoise(uint32_t descriptor_table, const SettingsComponent& settings)
{
    auto& render_system                     = world().GetSystem<RenderSystem>();
    auto  window_width                      = render_system.window_width();
    auto  window_height                     = render_system.window_height();
    auto  command_allocator                 = render_system.current_frame_command_allocator();
    auto  descriptor_heap                   = render_system.current_frame_descriptor_heap();
    auto  timestamp_query_heap              = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] = render_system.AllocateTimestampQueryPair("EAW");

    EAWConstants constants{render_system.window_width(),
                           render_system.window_height(),
                           render_system.frame_count(),
                           1,
                           settings.eaw_normal_sigma,
                           settings.eaw_depth_sigma,
                           settings.eaw_luma_sigma,
                           0.f};

    eaw_command_list_->Reset(command_allocator, nullptr);
    eaw_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    // Set to 0 to skip denoise.
    if (settings.denoise)
    {
        ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};
        eaw_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
        eaw_command_list_->SetComputeRootSignature(eaw_root_signature_.Get());

        eaw_command_list_->SetPipelineState(deaw_pipeline_state_.Get());
        eaw_command_list_->SetComputeRoot32BitConstants(
            EAWDenoisingRootSignature::kConstants, sizeof(EAWConstants) >> 2, &constants, 0);
        eaw_command_list_->SetComputeRootDescriptorTable(
            EAWDenoisingRootSignature::kOutput,
            render_system.GetDescriptorHandleGPU(descriptor_table));

        eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
        eaw_command_list_->ResourceBarrier(1,
                                           &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_[0].Get()));

        eaw_command_list_->SetPipelineState(eaw_pipeline_state_.Get());
        eaw_command_list_->SetComputeRoot32BitConstants(
            EAWDenoisingRootSignature::kConstants, sizeof(EAWConstants) >> 2, &constants, 0);
        eaw_command_list_->SetComputeRootDescriptorTable(
            EAWDenoisingRootSignature::kOutput,
            render_system.GetDescriptorHandleGPU(descriptor_table + 4));

        eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
        eaw_command_list_->ResourceBarrier(1,
                                           &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_[1].Get()));

        constants.stride = 3;
        eaw_command_list_->SetComputeRoot32BitConstants(
            EAWDenoisingRootSignature::kConstants, sizeof(EAWConstants) >> 2, &constants, 0);
        eaw_command_list_->SetComputeRootDescriptorTable(
            EAWDenoisingRootSignature::kOutput,
            render_system.GetDescriptorHandleGPU(descriptor_table + 8));
        eaw_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
        eaw_command_list_->ResourceBarrier(1,
                                           &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_[0].Get()));

        if (settings.eaw5)
        {
            constants.stride = 5;
            eaw_command_list_->SetComputeRoot32BitConstants(
                EAWDenoisingRootSignature::kConstants, sizeof(EAWConstants) >> 2, &constants, 0);
            eaw_command_list_->SetComputeRootDescriptorTable(
                EAWDenoisingRootSignature::kOutput,
                render_system.GetDescriptorHandleGPU(descriptor_table + 4));
            eaw_command_list_->Dispatch(
                ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
            eaw_command_list_->ResourceBarrier(
                1, &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_[1].Get()));

            constants.stride = 7;
            eaw_command_list_->SetComputeRoot32BitConstants(
                EAWDenoisingRootSignature::kConstants, sizeof(EAWConstants) >> 2, &constants, 0);
            eaw_command_list_->SetComputeRootDescriptorTable(
                EAWDenoisingRootSignature::kOutput,
                render_system.GetDescriptorHandleGPU(descriptor_table + 8));
            eaw_command_list_->Dispatch(
                ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
            eaw_command_list_->ResourceBarrier(
                1, &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_[0].Get()));
        }
    }
    else
    {
        auto src_index = (render_system.frame_count()) % 2;

        CD3DX12_TEXTURE_COPY_LOCATION src_copy_loc(indirect_history_[src_index].Get());
        CD3DX12_TEXTURE_COPY_LOCATION dst_copy_loc(output_temp_[0].Get());

        D3D12_BOX copy_box{0, 0, 0, window_width, window_height, 1};
        eaw_command_list_->CopyTextureRegion(&dst_copy_loc, 0, 0, 0, &src_copy_loc, &copy_box);
        eaw_command_list_->ResourceBarrier(1,
                                           &CD3DX12_RESOURCE_BARRIER::UAV(output_temp_[0].Get()));
    }

    eaw_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);
    eaw_command_list_->Close();
    render_system.PushCommandList(eaw_command_list_);
}

void RaytracingSystem::SpatialGather(uint32_t                 descriptor_table,
                                     uint32_t                 blue_noise_descriptor_table,
                                     const SettingsComponent& settings)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  width         = render_system.window_width();
    auto  height        = render_system.window_height();

    if (options_.lowres_indirect)
    {
        width >>= 1;
        height >>= 1;
    }

    auto command_allocator    = render_system.current_frame_command_allocator();
    auto descriptor_heap      = render_system.current_frame_descriptor_heap();
    auto timestamp_query_heap = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] =
        render_system.AllocateTimestampQueryPair("Spatial gather");

    EAWConstants constants{render_system.window_width(),
                           render_system.window_height(),
                           render_system.frame_count(),
                           1,
                           settings.gather_normal_sigma,
                           settings.gather_depth_sigma,
                           settings.gather_luma_sigma,
                           0.f};

    sg_command_list_->Reset(command_allocator, nullptr);
    sg_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    if (settings.gather)
    {
        ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};
        sg_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
        sg_command_list_->SetComputeRootSignature(sg_root_signature_.Get());
        sg_command_list_->SetPipelineState(sg_pipeline_state_.Get());
        sg_command_list_->SetComputeRoot32BitConstants(
            SpatialGatherRootSignature::kConstants, sizeof(EAWConstants) >> 2, &constants, 0);
        sg_command_list_->SetComputeRootDescriptorTable(
            SpatialGatherRootSignature::kOutput,
            render_system.GetDescriptorHandleGPU(descriptor_table));
        sg_command_list_->SetComputeRootDescriptorTable(
            SpatialGatherRootSignature::kBlueNoise,
            render_system.GetDescriptorHandleGPU(blue_noise_descriptor_table));

        sg_command_list_->Dispatch(ceil_divide(width, 8), ceil_divide(height, 8), 1);
    }
    else
    {
        CD3DX12_TEXTURE_COPY_LOCATION src_copy_loc(output_indirect_.Get());
        CD3DX12_TEXTURE_COPY_LOCATION dst_copy_loc(indirect_temp_.Get());

        D3D12_BOX copy_box{0, 0, 0, width, height, 1};
        sg_command_list_->CopyTextureRegion(&dst_copy_loc, 0, 0, 0, &src_copy_loc, &copy_box);
    }

    sg_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(indirect_temp_.Get()));
    sg_command_list_->EndQuery(timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);
    sg_command_list_->Close();

    render_system.PushCommandList(sg_command_list_);
}

uint32_t RaytracingSystem::PopulateSceneDataDescriptorTable(GPUSceneData& scene_data)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    auto base_index = render_system.AllocateDescriptorRange(5);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Format                      = DXGI_FORMAT_UNKNOWN;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.FirstElement         = 0;
    uav_desc.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;
    uav_desc.Buffer.NumElements = scene_data.index_buffer->GetDesc().Width / sizeof(uint32_t);
    uav_desc.Buffer.StructureByteStride = sizeof(uint32_t);
    dx12api().device()->CreateUnorderedAccessView(scene_data.index_buffer,
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));

    uav_desc.Buffer.NumElements         = scene_data.vertex_buffer->GetDesc().Width / sizeof(float);
    uav_desc.Buffer.StructureByteStride = sizeof(float);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.vertex_buffer,
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));

    uav_desc.Buffer.NumElements         = scene_data.normal_buffer->GetDesc().Width / sizeof(float);
    uav_desc.Buffer.StructureByteStride = sizeof(float);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.normal_buffer,
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 2));

    uav_desc.Buffer.NumElements = scene_data.texcoord_buffer->GetDesc().Width / sizeof(XMFLOAT2);
    uav_desc.Buffer.StructureByteStride = sizeof(XMFLOAT2);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.texcoord_buffer,
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 3));

    uav_desc.Buffer.NumElements =
        scene_data.mesh_desc_buffer->GetDesc().Width / sizeof(MeshComponent);
    uav_desc.Buffer.StructureByteStride = sizeof(MeshComponent);
    dx12api().device()->CreateUnorderedAccessView(
        scene_data.mesh_desc_buffer,
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 4));

    return base_index;
}

uint32_t RaytracingSystem::PopulateOutputIndirectDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(1);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(output_indirect_.Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));
    return base_index;
}

uint32_t RaytracingSystem::PopulateInternalDataDescritptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(1);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    srv_desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.Texture2D.MipLevels           = 1;
    srv_desc.Texture2D.MostDetailedMip     = 0;
    srv_desc.Texture2D.PlaneSlice          = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0;
    srv_desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dx12api().device()->CreateShaderResourceView(
        blue_noise_texture(), &srv_desc, render_system.GetDescriptorHandleCPU(base_index));
    return base_index;
}

uint32_t RaytracingSystem::PopulateIndirectHistoryDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(5);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    auto src_index = (render_system.frame_count() + 1) % 2;
    auto dst_index = (src_index + 1) % 2;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(indirect_history_[src_index].Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));
    dx12api().device()->CreateUnorderedAccessView(
        moments_history_[src_index].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));
    dx12api().device()->CreateUnorderedAccessView(
        indirect_history_[dst_index].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 3));
    dx12api().device()->CreateUnorderedAccessView(
        moments_history_[dst_index].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 4));
    // We need 32 bits for depth here for stable reconstruction.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        prev_gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 2));

    return base_index;
}

uint32_t RaytracingSystem::PopulateCombinedHistoryDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(5);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    auto src_index = (render_system.frame_count() + 1) % 2;
    auto dst_index = (src_index + 1) % 2;

    dx12api().device()->CreateUnorderedAccessView(combined_history_[src_index].Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));
    dx12api().device()->CreateUnorderedAccessView(
        combined_history_[dst_index].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 3));

    // We need 32 bits for depth here for stable reconstruction.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        prev_gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 2));

    return base_index;
}

uint32_t RaytracingSystem::PopulateEAWOutputDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(12);
    auto  history_index = (render_system.frame_count()) % 2;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // History buffer is an input.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(indirect_history_[history_index].Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));
    dx12api().device()->CreateUnorderedAccessView(
        moments_history_[history_index].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 2));
    dx12api().device()->CreateUnorderedAccessView(
        output_temp_[0].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 3));

    dx12api().device()->CreateUnorderedAccessView(
        output_temp_[0].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 4));
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 5));
    dx12api().device()->CreateUnorderedAccessView(
        nullptr, nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 6));
    dx12api().device()->CreateUnorderedAccessView(
        output_temp_[1].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 7));

    dx12api().device()->CreateUnorderedAccessView(
        output_temp_[1].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 8));
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 9));
    dx12api().device()->CreateUnorderedAccessView(
        nullptr, nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index + 10));
    dx12api().device()->CreateUnorderedAccessView(
        output_temp_[0].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 11));

    return base_index;
}
uint32_t RaytracingSystem::PopulateIndirectTAInputDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(2);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(
        indirect_temp_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));
    // Create gbuffer output.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));

    return base_index;
}

uint32_t RaytracingSystem::PopulateDirectTAInputDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(2);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(
        output_direct_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));
    // Create gbuffer output.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));

    return base_index;
}
uint32_t RaytracingSystem::PopulateSceneTexturesDescriptorTable()
{
    auto& render_system  = world().GetSystem<RenderSystem>();
    auto& texture_system = world().GetSystem<TextureSystem>();
    auto  base_index     = render_system.AllocateDescriptorRange(1024);

    auto num_textures = texture_system.num_textures();

    for (auto i = 0; i < 1024; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        srv_desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.Texture2D.MipLevels           = 1;
        srv_desc.Texture2D.MostDetailedMip     = 0;
        srv_desc.Texture2D.PlaneSlice          = 0;
        srv_desc.Texture2D.ResourceMinLODClamp = 0;
        srv_desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        auto texture = i < num_textures ? texture_system.texture(i) : nullptr;

        dx12api().device()->CreateShaderResourceView(
            texture, &srv_desc, render_system.GetDescriptorHandleCPU(base_index + i));
    }

    return base_index;
}

uint32_t RaytracingSystem::PopulateCombineDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(3);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(
        output_direct_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));
    dx12api().device()->CreateUnorderedAccessView(
        output_temp_[0].Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));
    uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_albedo_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 2));

    return base_index;
}

uint32_t RaytracingSystem::PopulateTAAInputDescritorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(2);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(output_temp_[0].Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));
    // Create gbuffer output.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));

    return base_index;
}
uint32_t RaytracingSystem::PopulateSpatialGatherDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(3);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // History buffer is an input.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(output_indirect_.Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));

    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));
    dx12api().device()->CreateUnorderedAccessView(
        indirect_temp_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 2));

    return base_index;
}

uint32_t RaytracingSystem::PopulateGBufferDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(1);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // History buffer is an input.
    uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_geo_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));

    return base_index;
}

uint32_t RaytracingSystem::PopulatePrevGBufferDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(1);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // History buffer is an input.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(prev_gbuffer_normal_depth_.Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));

    return base_index;
}

uint32_t RaytracingSystem::PopulateOutputDirectDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(1);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // History buffer is an input.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        output_direct_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));

    return base_index;
}
uint32_t RaytracingSystem::PopulateOutputNormalDepthAlbedo()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(2);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Albedo texture.
    uav_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dx12api().device()->CreateUnorderedAccessView(gbuffer_albedo_.Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));

    // Normal + depth texture.
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dx12api().device()->CreateUnorderedAccessView(
        gbuffer_normal_depth_.Get(),
        nullptr,
        &uav_desc,
        render_system.GetDescriptorHandleCPU(base_index + 1));

    return base_index;
}
}  // namespace capsaicin
