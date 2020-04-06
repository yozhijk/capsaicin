#include "raytracing_system.h"

#include "src/common.h"
#include "src/systems/asset_load_system.h"
#include "src/systems/camera_system.h"
#include "src/systems/tlas_system.h"

namespace capsaicin
{
namespace
{
// Signature for visibility raytracing pass.
namespace RaytracingRootSignature
{
enum
{
    kConstants = 0,
    kCameraBuffer,
    kAccelerationStructure,
    kSceneData,
    kOutput,
    kNumEntries
};
}

// Root constants for raster blit.
struct Constants
{
    uint32_t width;
    uint32_t height;
    float rotation;
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

ID3D12Resource* GetCamera(ComponentAccess& access, EntityQuery& entity_query)
{
    auto& cameras = access.Read<CameraComponent>();
    auto entities = entity_query().Filter([&cameras](Entity e) { return cameras.HasComponent(e); }).entities();
    if (entities.size() != 1)
    {
        error("RaytracingSystem: no cameras found");
        throw std::runtime_error("RaytracingSystem: no cameras found");
    }

    auto& camera = cameras.GetComponent(entities[0]);
    return camera.camera_buffer.Get();
}
}  // namespace

RaytracingSystem::RaytracingSystem()
{
    info("RaytracingSystem: Initializing");

    auto command_allocator = world().GetSystem<RenderSystem>().current_frame_command_allocator();

    // Create command list for visibility raytracing.
    raytracing_command_list_ = dx12api().CreateCommandList(command_allocator);
    raytracing_command_list_->Close();

    // Initialize raytracing pipeline.
    InitPipeline();
}

RaytracingSystem::~RaytracingSystem() = default;

void RaytracingSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto tlas = GetSceneTLASComponent(access, entity_query);

    auto& meshes = access.Read<MeshComponent>();

    GPUSceneData scene_data;
    scene_data.index_buffer = meshes[0].indices.Get();
    scene_data.vertex_buffer = meshes[0].vertices.Get();
    scene_data.normal_buffer = meshes[0].normals.Get();
    scene_data.texcoord_buffer = meshes[0].texcoords.Get();

    auto scene_data_base_index = PopulateSceneDataDescriptorTable(scene_data);
    auto output_uav_index = PopulateOutputDescriptorTable();

    Raytrace(tlas.tlas.Get(), GetCamera(access, entity_query), scene_data_base_index, output_uav_index);
}

ID3D12Resource* RaytracingSystem::current_frame_output()
{
    auto& render_system = world().GetSystem<RenderSystem>();

    return raytracing_outputs_[render_system.current_gpu_frame_index()].Get();
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
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_DESCRIPTOR_RANGE scene_data_descriptor_range;
        scene_data_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 1);

        CD3DX12_ROOT_PARAMETER root_entries[RaytracingRootSignature::kNumEntries] = {};
        root_entries[RaytracingRootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
        root_entries[RaytracingRootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[RaytracingRootSignature::kAccelerationStructure].InitAsShaderResourceView(0);
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
    lib->DefineExport(L"Miss");

    auto hit_group = pipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hit_group->SetClosestHitShaderImport(L"Hit");

    hit_group->SetHitGroupExport(L"HitGroup");
    hit_group->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shader_config = pipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payload_size = 4 * sizeof(float);                           // float3 color, padding;
    UINT attribute_size = 2 * sizeof(float) + 2 * sizeof(uint32_t);  // float2 barycentrics, triangle id, shape id
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
        info("RaytracingSystem: Initializing raytracing outputs");
        uint32_t increment_size =
            dx12api().device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        for (uint32_t i = 0; i < RenderSystem::num_gpu_frames_in_flight(); ++i)
        {
            auto texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                             window_width,
                                                             window_height,
                                                             1,
                                                             0,
                                                             1,
                                                             0,
                                                             D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            raytracing_outputs_[i] = dx12api().CreateResource(
                texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
}

void RaytracingSystem::Raytrace(ID3D12Resource* scene,
                                ID3D12Resource* camera,
                                uint32_t scene_data_base_index,
                                uint32_t output_uav_index)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto window_width = render_system.window_width();
    auto window_height = render_system.window_width();
    auto command_allocator = render_system.current_frame_command_allocator();
    auto descriptor_heap = render_system.current_frame_descriptor_heap();
    auto current_gpu_frame_index = render_system.current_gpu_frame_index();

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    raytracing_command_list_->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    cmdlist4->Reset(command_allocator, nullptr);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};

    cmdlist4->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    cmdlist4->SetComputeRootSignature(raytracing_root_signature_.Get());
    cmdlist4->SetComputeRootShaderResourceView(RaytracingRootSignature::kAccelerationStructure,
                                               scene->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootConstantBufferView(RaytracingRootSignature::kCameraBuffer, camera->GetGPUVirtualAddress());
    cmdlist4->SetComputeRootDescriptorTable(RaytracingRootSignature::kSceneData,
                                            render_system.GetDescriptorHandleGPU(scene_data_base_index));
    cmdlist4->SetComputeRootDescriptorTable(RaytracingRootSignature::kOutput,
                                            render_system.GetDescriptorHandleGPU(output_uav_index));

    D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
    dispatch_desc.HitGroupTable.StartAddress = hitgroup_shader_table->GetGPUVirtualAddress();
    dispatch_desc.HitGroupTable.SizeInBytes = hitgroup_shader_table->GetDesc().Width;
    dispatch_desc.HitGroupTable.StrideInBytes = dispatch_desc.HitGroupTable.SizeInBytes;
    dispatch_desc.MissShaderTable.StartAddress = miss_shader_table->GetGPUVirtualAddress();
    dispatch_desc.MissShaderTable.SizeInBytes = miss_shader_table->GetDesc().Width;
    dispatch_desc.MissShaderTable.StrideInBytes = dispatch_desc.MissShaderTable.SizeInBytes;
    dispatch_desc.RayGenerationShaderRecord.StartAddress = raygen_shader_table->GetGPUVirtualAddress();
    dispatch_desc.RayGenerationShaderRecord.SizeInBytes = raygen_shader_table->GetDesc().Width;
    dispatch_desc.Width = window_width;
    dispatch_desc.Height = window_height;
    dispatch_desc.Depth = 1;

    cmdlist4->SetPipelineState1(raytracing_pipeline_state_.Get());
    cmdlist4->DispatchRays(&dispatch_desc);

    cmdlist4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(raytracing_outputs_[current_gpu_frame_index].Get()));
    cmdlist4->Close();

    render_system.PushCommandList(cmdlist4.Get());
}

uint32_t RaytracingSystem::PopulateSceneDataDescriptorTable(GPUSceneData& scene_data)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    auto base_index = render_system.AllocateDescriptorRange(4);
    auto base_cpu_handle = render_system.GetDescriptorHandleCPU(base_index);

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
    auto base_index = render_system.AllocateDescriptorRange(1);
    auto current_gpu_frame_index = render_system.current_gpu_frame_index();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;
    dx12api().device()->CreateUnorderedAccessView(raytracing_outputs_[current_gpu_frame_index].Get(),
                                                  nullptr,
                                                  &uav_desc,
                                                  render_system.GetDescriptorHandleCPU(base_index));
    return base_index;
}
}  // namespace capsaicin
