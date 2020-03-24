#include "tlas_system.h"

#include "blas_system.h"

namespace capsaicin
{
namespace
{
void BuildTLAS(const EntitySet::EntityStorage& entities, TLASComponent& tlas)
{
    auto fence = dx12api().CreateFence(0);

    auto command_allocator = dx12api().CreateCommandAllocator();
    auto command_list = dx12api().CreateCommandList(command_allocator.Get());

    ComPtr<ID3D12GraphicsCommandList4> cmdlist4 = nullptr;
    command_list->QueryInterface(IID_PPV_ARGS(&cmdlist4));

    ComPtr<ID3D12Device5> device5 = nullptr;
    dx12api().device()->QueryInterface(IID_PPV_ARGS(&device5));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS build_input;
    build_input.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    build_input.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    build_input.NumDescs = 1;
    build_input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build_input.InstanceDescs = 0;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&build_input, &info);

    auto scratch_buffer = dx12api().CreateUAVBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    tlas.tlas = dx12api().CreateUAVBuffer(info.ResultDataMaxSizeInBytes,
                                          D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    // Fill instance descs.
    uint32_t instance_index = 0;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instance_descs(entities.size());
    for (auto e : entities)
    {
        auto& blas = world().GetComponent<BLASComponent>(e);
        instance_descs[instance_index].AccelerationStructure = blas.blas->GetGPUVirtualAddress();
        instance_descs[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        instance_descs[instance_index].InstanceContributionToHitGroupIndex = 0;
        instance_descs[instance_index].InstanceID = instance_index;
        instance_descs[instance_index].InstanceMask = 0xff;

        memset(&instance_descs[instance_index].Transform[0][0], 0, 12 * sizeof(float));
        instance_descs[instance_index].Transform[0][0] = 1.f;
        instance_descs[instance_index].Transform[1][1] = 1.f;
        instance_descs[instance_index].Transform[2][2] = 1.f;
    }

    auto upload_buffer =
        dx12api().CreateUploadBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * entities.size(), instance_descs.data());
    build_input.InstanceDescs = upload_buffer->GetGPUVirtualAddress();

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_desc;
    build_desc.Inputs = build_input;
    build_desc.ScratchAccelerationStructureData = scratch_buffer->GetGPUVirtualAddress();
    build_desc.SourceAccelerationStructureData = 0;
    build_desc.DestAccelerationStructureData = tlas.tlas->GetGPUVirtualAddress();

    cmdlist4->BuildRaytracingAccelerationStructure(&build_desc, 0, nullptr);
    command_list->Close();

    // Execute command list synchronously.
    ID3D12CommandList* command_lists[] = {command_list.Get()};
    dx12api().command_queue()->ExecuteCommandLists(1, command_lists);
    dx12api().command_queue()->Signal(fence.Get(), 1);

    while (fence->GetCompletedValue() != 1) std::this_thread::yield();
}
}  // namespace

TLASSystem::TLASSystem() { world().CreateEntity().AddComponent<TLASComponent>().Build(); }

void TLASSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& tlases = access.Write<TLASComponent>();
    auto& blases = access.Read<BLASComponent>();

    auto entities = entity_query().Filter([&tlases](Entity e) { return tlases.HasComponent(e); }).entities();

    if (entities.size() > 1)
    {
        error("TLASSystem: more than one TLAS found");
        throw std::runtime_error("TLASSystem: more than one TLAS found");
    }

    auto entities_with_blas = entity_query().Filter([&blases](Entity e) { return blases.HasComponent(e); }).entities();

    auto& tlas = world().GetComponent<TLASComponent>(entities[0]);

    if (!tlas.built)
    {
        info("TLASSystem: Building TLAS");
        BuildTLAS(entities_with_blas, tlas);
        tlas.built = true;
    }
}
}  // namespace capsaicin