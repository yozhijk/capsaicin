#include "blas_system.h"

#include "asset_load_system.h"

namespace capsaicin
{
namespace
{
void BuildBLAS(MeshComponent& gpu_mesh, BLASComponent& blas)
{
    auto fence = dx12api().CreateFence(0);

    auto command_allocator = dx12api().CreateCommandAllocator();
    auto command_list = dx12api().CreateCommandList(command_allocator.Get());

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    command_list->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc;
    geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry_desc.Triangles.VertexBuffer.StartAddress = gpu_mesh.vertices->GetGPUVirtualAddress();
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = 3 * sizeof(float);
    geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry_desc.Triangles.VertexCount = gpu_mesh.vertices->GetDesc().Width / (3 * sizeof(float));
    geometry_desc.Triangles.IndexBuffer = gpu_mesh.indices->GetGPUVirtualAddress();
    geometry_desc.Triangles.IndexCount = gpu_mesh.indices->GetDesc().Width / sizeof(uint32_t);
    geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geometry_desc.Triangles.Transform3x4 = 0;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS build_input;
    build_input.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build_input.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    build_input.NumDescs = 1;
    build_input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build_input.pGeometryDescs = &geometry_desc;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&build_input, &info);

    auto scratch_buffer =
        dx12api().CreateUAVBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    blas.blas = dx12api().CreateUAVBuffer(info.ResultDataMaxSizeInBytes,
                                          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc;
    build_desc.Inputs = build_input;
    build_desc.ScratchAccelerationStructureData = scratch_buffer->GetGPUVirtualAddress();
    build_desc.SourceAccelerationStructureData = 0;
    build_desc.DestAccelerationStructureData = blas.blas->GetGPUVirtualAddress();

    cmdlist4->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);
    command_list->Close();

    // Execute command list synchronously.
    ID3D12CommandList* command_lists[] = {command_list.Get()};
    dx12api().command_queue()->ExecuteCommandLists(1, command_lists);
    dx12api().command_queue()->Signal(fence.Get(), 1);

    while (fence->GetCompletedValue() != 1) std::this_thread::yield();
}
}  // namespace
void BLASSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& meshes = access.Read<MeshComponent>();
    auto& blases = access.Read<BLASComponent>();

    // Find entities with mesh component which have not been handled yet.
    auto entities =
        entity_query()
            .Filter([&meshes, &blases](Entity e) { return meshes.HasComponent(e) && !blases.HasComponent(e); })
            .entities();

    if (!entities.empty())
    {
        info("BLASSystem: found {} meshes", entities.size());
    }

    // Load asset.
    for (auto e : entities)
    {
        auto& gpu_mesh = world().GetComponent<MeshComponent>(e);
        auto& blas = world().AddComponent<BLASComponent>(e);

        info("BLASSystem: Building BLAS");
        BuildBLAS(gpu_mesh, blas);
    }
}
}  // namespace capsaicin