#include "voxel_visualizer_system.h"

#include "src/systems/asset_load_system.h"
#include "src/systems/camera_system.h"
#include "src/systems/render_system.h"
#include "src/systems/voxel_system.h"
#include "src/utils/tri_box_test.h"
#include "src/utils/vector_math.h"

namespace capsaicin
{
namespace
{
namespace RootSignature
{
enum
{
    kConstants = 0,
    kCameraBuffer,
    kGrid,
    kOctree,
    kOutput,
    kNumEntries
};
}

struct Constants
{
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t padding;

    float scene_aabb_min[4];
    float scene_aabb_max[4];
};
}  // namespace

VoxelVisualizerSystem::VoxelVisualizerSystem()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    render_command_list_ =
        dx12api().CreateCommandList(render_system.current_frame_command_allocator());
    render_command_list_->Close();

    InitPipeline();
    InitOutput();
}

void VoxelVisualizerSystem::InitPipeline()
{
    // Global Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE output_descriptor_range;
        output_descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);

        CD3DX12_ROOT_PARAMETER root_entries[RootSignature::kNumEntries] = {};
        root_entries[RootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
        root_entries[RootSignature::kCameraBuffer].InitAsConstantBufferView(1);
        root_entries[RootSignature::kGrid].InitAsUnorderedAccessView(0);
        root_entries[RootSignature::kOctree].InitAsUnorderedAccessView(1);
        root_entries[RootSignature::kOutput].InitAsDescriptorTable(1, &output_descriptor_range);

        CD3DX12_ROOT_SIGNATURE_DESC desc = {};
        desc.Init(RootSignature::kNumEntries, root_entries);
        root_signature_ = dx12api().CreateRootSignature(desc);
    }

    {
        auto shader = ShaderCompiler::instance().CompileFromFile(
            "../../../src/core/shaders/voxel_visualize.hlsl", "cs_6_3", "Visualize");

        pipeline_state_ = dx12api().CreateComputePipelineState(shader, root_signature_.Get());
    }
}

void VoxelVisualizerSystem::InitOutput()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  window_width  = render_system.window_width();
    auto  window_height = render_system.window_height();

    auto texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                     window_width,
                                                     window_height,
                                                     1,
                                                     0,
                                                     1,
                                                     0,
                                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    output_ = dx12api().CreateResource(texture_desc,
                                       CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

uint32_t VoxelVisualizerSystem::PopulateOutputDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto  base_index    = render_system.AllocateDescriptorRange(1);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension        = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav_desc.Texture2D.MipSlice   = 0;
    uav_desc.Texture2D.PlaneSlice = 0;

    // Create color buffer.
    dx12api().device()->CreateUnorderedAccessView(
        output_.Get(), nullptr, &uav_desc, render_system.GetDescriptorHandleCPU(base_index));
    return base_index;
}

void VoxelVisualizerSystem::Run(ComponentAccess& access,
                                EntityQuery&     entity_query,
                                tf::Subflow&     subflow)
{
    auto& render_system           = world().GetSystem<RenderSystem>();
    auto& voxel_system            = world().GetSystem<VoxelSystem>();
    auto  window_width            = render_system.window_width();
    auto  window_height           = render_system.window_height();
    auto  command_allocator       = render_system.current_frame_command_allocator();
    auto  descriptor_heap         = render_system.current_frame_descriptor_heap();
    auto  output_descriptor_table = PopulateOutputDescriptorTable();
    auto  grid_buffer             = voxel_system.grid_buffer();
    auto  octree_buffer           = voxel_system.octree_buffer();
    auto  timestamp_query_heap    = render_system.current_frame_timestamp_query_heap();
    auto [start_time_index, end_time_index] =
        render_system.AllocateTimestampQueryPair("Voxel raytracing");

    auto& cameras = access.Read<CameraComponent>();
    auto  entities =
        entity_query().Filter([&cameras](Entity e) { return cameras.HasComponent(e); }).entities();
    if (entities.size() != 1)
    {
        error("VoxelVisualizerSystem: no cameras found");
        throw std::runtime_error("RaytracingSystem: no cameras found");
    }

    auto& camera = cameras.GetComponent(entities[0]);

    Constants constants{render_system.window_width(),
                        render_system.window_height(),
                        render_system.frame_count(),
                        0,
                        {voxel_system.scene_aabb().pmin.x,
                         voxel_system.scene_aabb().pmin.y,
                         voxel_system.scene_aabb().pmin.z},
                        {voxel_system.scene_aabb().pmax.x,
                         voxel_system.scene_aabb().pmax.y,
                         voxel_system.scene_aabb().pmax.z}};

    render_command_list_->Reset(command_allocator, nullptr);
    render_command_list_->EndQuery(
        timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_time_index);

    ID3D12DescriptorHeap* desc_heaps[] = {descriptor_heap};
    render_command_list_->SetDescriptorHeaps(ARRAYSIZE(desc_heaps), desc_heaps);
    render_command_list_->SetComputeRootSignature(root_signature_.Get());
    render_command_list_->SetPipelineState(pipeline_state_.Get());
    render_command_list_->SetComputeRoot32BitConstants(
        RootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    render_command_list_->SetComputeRootDescriptorTable(
        RootSignature::kOutput, render_system.GetDescriptorHandleGPU(output_descriptor_table));
    render_command_list_->SetComputeRootConstantBufferView(
        RootSignature::kCameraBuffer, camera.camera_buffer->GetGPUVirtualAddress());
    render_command_list_->SetComputeRootUnorderedAccessView(RootSignature::kGrid,
                                                            grid_buffer->GetGPUVirtualAddress());
    render_command_list_->SetComputeRootUnorderedAccessView(RootSignature::kOctree,
                                                            octree_buffer->GetGPUVirtualAddress());

    render_command_list_->Dispatch(ceil_divide(window_width, 8), ceil_divide(window_height, 8), 1);
    render_command_list_->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));
    render_command_list_->EndQuery(
        timestamp_query_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_time_index);
    render_command_list_->Close();
    render_system.PushCommandList(render_command_list_);
}
}  // namespace capsaicin