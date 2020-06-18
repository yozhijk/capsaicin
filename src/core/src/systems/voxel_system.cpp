#include "voxel_system.h"

#include "src/systems/asset_load_system.h"
#include "src/systems/render_system.h"
#include "src/utils/tri_box_test.h"
#include "src/utils/vector_math.h"
#include "src/utils/voxel.h"

namespace capsaicin
{
namespace
{
static constexpr uint32_t kResolution = 256;

Aabb CalculateAabb(const std::vector<MeshData>& meshes)
{
    Aabb aabb;
    for (auto& m : meshes)
    {
        for (uint32_t i = 0; i < m.indices.size(); ++i)
        {
            aabb.grow(float3(&m.positions[3 * m.indices[i]]));
        }
    }
    return aabb;
}

VoxelGrid<uint32_t> VoxelizeMesh(const Aabb& scene_aabb, const MeshData& mesh)
{
    auto grid_res = int3{kResolution, kResolution, kResolution};
    VoxelGrid<uint32_t> voxels(grid_res);

    float3 voxel_size = scene_aabb.extents() / grid_res;

    float r[3] = {
        voxel_size.x / 2,
        voxel_size.y / 2,
        voxel_size.z / 2,
    };

    for (uint32_t i = 0; i < mesh.indices.size() / 3; ++i)
    {
        auto v0 = float3(&mesh.positions[3 * mesh.indices[3 * i + 0]]);
        auto v1 = float3(&mesh.positions[3 * mesh.indices[3 * i + 1]]);
        auto v2 = float3(&mesh.positions[3 * mesh.indices[3 * i + 2]]);

        float v[3][3] = {{v0.x, v0.y, v0.z}, {v1.x, v1.y, v1.z}, {v2.x, v2.y, v2.z}};

        Aabb aabb(v0, v1);
        aabb.grow(v2);

        float3 min_voxel = (aabb.pmin - scene_aabb.pmin) / voxel_size;
        float3 max_voxel = (aabb.pmax - scene_aabb.pmin) / voxel_size;

        min_voxel = vmin(min_voxel, float3(kResolution - 1, kResolution - 1, kResolution - 1));
        max_voxel = vmin(max_voxel, float3(kResolution - 1, kResolution - 1, kResolution - 1));

        for (uint32_t zv = uint32_t(floor(min_voxel.z)); zv <= uint32_t(floor(max_voxel.z)); ++zv)
            for (uint32_t yv = uint32_t(floor(min_voxel.y)); yv <= uint32_t(floor(max_voxel.y)); ++yv)
                for (uint32_t xv = uint32_t(floor(min_voxel.x)); xv <= uint32_t(floor(max_voxel.x)); ++xv)
                {
                    Aabb voxel_aabb{float3(xv, yv, zv) * voxel_size, float3(xv + 1, yv + 1, zv + 1) * voxel_size};
                    voxel_aabb.pmin += scene_aabb.pmin;
                    voxel_aabb.pmax += scene_aabb.pmin;

                    float c[3] = {
                        voxel_aabb.center().x,
                        voxel_aabb.center().y,
                        voxel_aabb.center().z,
                    };

                    if (TriBoxOverlap(c, r, v))
                    {
                        voxels.voxel(int3{(int)xv, (int)yv, (int)zv}) = 1;
                    }
                }
    }

    return voxels;
}

void UploadVoxelGrid(const VoxelGrid<uint32_t>& voxels,
                     ID3D12Resource* buffer,
                     ID3D12GraphicsCommandList* command_list,
                     RenderSystem& render_system)
{
    // Create upload buffers.
    auto upload_buffer = dx12api().CreateUploadBuffer(voxels.data_size(), voxels.data());

    render_system.AddAutoreleaseResource(upload_buffer);

    // Copy data.
    command_list->CopyBufferRegion(buffer, 0, upload_buffer.Get(), 0, voxels.data_size());

    //
    D3D12_RESOURCE_BARRIER copy_dest_to_uav[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};

    // Transition to UAV.
    command_list->ResourceBarrier(ARRAYSIZE(copy_dest_to_uav), copy_dest_to_uav);
}

void UploadOctreeGrid(const VoxelOctree<uint32_t>& octree,
                      ID3D12Resource* buffer,
                      ID3D12GraphicsCommandList* command_list,
                      RenderSystem& render_system)
{
    auto size = octree.node_count() * sizeof(VoxelOctree<uint32_t>::Node);

    // Create upload buffers.
    auto upload_buffer = dx12api().CreateUploadBuffer(size, octree.data());

    render_system.AddAutoreleaseResource(upload_buffer);

    // Copy data.
    command_list->CopyBufferRegion(buffer, 0, upload_buffer.Get(), 0, size);

    //
    D3D12_RESOURCE_BARRIER copy_dest_to_uav[] = {CD3DX12_RESOURCE_BARRIER::Transition(
        buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};

    // Transition to UAV.
    command_list->ResourceBarrier(ARRAYSIZE(copy_dest_to_uav), copy_dest_to_uav);
}
}  // namespace

VoxelSystem::VoxelSystem()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    grid_buffer_ = dx12api().CreateUAVBuffer(kResolution * kResolution * kResolution * sizeof(uint32_t),
                                             D3D12_RESOURCE_STATE_COPY_DEST);
    upload_command_list_ = dx12api().CreateCommandList(render_system.current_frame_command_allocator());
    upload_command_list_->Close();
}

void VoxelSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    auto& meshes = access.Read<CPUMeshComponent>();

    auto mesh_entities = entity_query().Filter([&meshes](Entity e) { return meshes.HasComponent(e); }).entities();

    if (mesh_entities.size() > 0)
    {
        std::vector<MeshData> cpu_meshes(mesh_entities.size());

        std::transform(mesh_entities.cbegin(), mesh_entities.cend(), cpu_meshes.begin(), [&meshes](Entity e) {
            return meshes.GetComponent(e).mesh_data;
        });

        upload_command_list_->Reset(render_system.current_frame_command_allocator(), nullptr);

        scene_aabb_ = CalculateAabb(cpu_meshes);
        auto grid_res = int3{kResolution, kResolution, kResolution};

        info("VoxelSystem: Voxelizing");
        VoxelGrid<uint32_t> voxelization(grid_res);
        for (auto& m : cpu_meshes)
        {
            auto voxels = VoxelizeMesh(scene_aabb_, m);
            voxelization.Merge(voxels, std::plus{});
        }

        info("VoxelSystem: Building octree");
        VoxelOctree<uint32_t> octree(voxelization);

        uint32_t num_voxels = 0;
        auto dim = voxelization.dim();
        for (auto i = 0; i < dim.x; ++i)
            for (auto j = 0; j < dim.y; ++j)
                for (auto k = 0; k < dim.z; ++k)
                {
                    if (voxelization.voxel(int3{i, j, k}) > 0)
                        ++num_voxels;
                }

        //for (auto j = 0; j < dim.y; ++j)
        //{
        //    std::cout << "\n\n\n\n";
        //    for (auto i = 0; i < dim.x; ++i)
        //    {
        //        std::cout << "\n";
        //        for (auto k = 0; k < dim.z; ++k)
        //        {
        //            bool valid = (voxelization.voxel(int3{i, j, k}) > 0);
        //            std::cout << (valid ? 1 : 0);
        //        }
        //    }
        //}

        uint32_t num_octree_voxels = 0;
        for (auto i = 0; i < octree.node_count(); ++i)
        {
            if (octree.data()[i].leaf)
                ++num_octree_voxels;
        }

        octree.TraverseNode(0, 0, 0);

        if (!octree_buffer_)
        {
            octree_buffer_ = dx12api().CreateUAVBuffer(octree.node_count() * sizeof(VoxelOctree<uint32_t>::Node),
                                                       D3D12_RESOURCE_STATE_COPY_DEST);
        }

        info("VoxelSystem: Uploading");
        UploadVoxelGrid(voxelization, grid_buffer_.Get(), upload_command_list_.Get(), render_system);
        UploadOctreeGrid(octree, octree_buffer_.Get(), upload_command_list_.Get(), render_system);

        upload_command_list_->Close();
        render_system.PushCommandList(upload_command_list_.Get());

        for (auto e : mesh_entities)
        {
            world().DestroyEntity(e);
        }
    }
}
}  // namespace capsaicin