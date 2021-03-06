#include "blas_system.h"

#include <DirectXMath.h>

#include "src/systems/asset_load_system.h"
#include "src/systems/render_system.h"

using namespace DirectX;

namespace capsaicin
{
namespace
{
void BuildBLAS(MeshComponent&             gpu_mesh,
               BLASComponent&             blas,
               ID3D12GraphicsCommandList* command_list,
               RenderSystem&              render_system)
{
    auto& geometry_storage = world().GetSystem<AssetLoadSystem>().geometry_storage();

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    command_list->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc;
    geometry_desc.Type  = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry_desc.Triangles.VertexBuffer.StartAddress =
        geometry_storage.vertices->GetGPUVirtualAddress() +
        gpu_mesh.first_vertex_offset * sizeof(XMFLOAT3);
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = 3 * sizeof(float);
    geometry_desc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry_desc.Triangles.VertexCount                = gpu_mesh.vertex_count;
    geometry_desc.Triangles.IndexBuffer = geometry_storage.indices->GetGPUVirtualAddress() +
                                          gpu_mesh.first_index_offset * sizeof(uint32_t);
    geometry_desc.Triangles.IndexCount   = gpu_mesh.index_count;
    geometry_desc.Triangles.IndexFormat  = DXGI_FORMAT_R32_UINT;
    geometry_desc.Triangles.Transform3x4 = 0;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS build_input;
    build_input.Type        = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build_input.Flags       = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    build_input.NumDescs    = 1;
    build_input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build_input.pGeometryDescs = &geometry_desc;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&build_input, &info);

    auto scratch_buffer = dx12api().CreateUAVBuffer(info.ScratchDataSizeInBytes,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    render_system.AddAutoreleaseResource(scratch_buffer);

    blas.blas = dx12api().CreateUAVBuffer(info.ResultDataMaxSizeInBytes,
                                          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc;
    build_desc.Inputs                           = build_input;
    build_desc.ScratchAccelerationStructureData = scratch_buffer->GetGPUVirtualAddress();
    build_desc.SourceAccelerationStructureData  = 0;
    build_desc.DestAccelerationStructureData    = blas.blas->GetGPUVirtualAddress();

    cmdlist4->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);
    cmdlist4->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(blas.blas.Get()));
}
}  // namespace
void BLASSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    // Create command list if needed.
    if (!build_command_list_)
    {
        build_command_list_ =
            dx12api().CreateCommandList(render_system.current_frame_command_allocator());
        build_command_list_->Close();
    }

    auto& meshes = access.Read<MeshComponent>();
    auto& blases = access.Read<BLASComponent>();

    // Find entities with mesh component which have not been handled yet.
    auto entities = entity_query()
                        .Filter([&meshes, &blases](Entity e) {
                            return meshes.HasComponent(e) && !blases.HasComponent(e);
                        })
                        .entities();

    if (!entities.empty())
    {
        info("BLASSystem: found {} meshes", entities.size());
    }

    if (!entities.empty())
    {
        build_command_list_->Reset(render_system.current_frame_command_allocator(), nullptr);

        // Load asset.
        for (auto e : entities)
        {
            auto& gpu_mesh = world().GetComponent<MeshComponent>(e);
            auto& blas     = world().AddComponent<BLASComponent>(e);

            info("BLASSystem: Building BLAS");
            BuildBLAS(gpu_mesh, blas, build_command_list_.Get(), render_system);
        }

        build_command_list_->Close();
        render_system.PushCommandList(build_command_list_);
    }
}
}  // namespace capsaicin