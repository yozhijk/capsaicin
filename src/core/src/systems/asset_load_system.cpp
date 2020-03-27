#include "asset_load_system.h"

#include "src/common.h"
#include "src/systems/render_system.h"
#include "tiny_obj_loader.h"

using namespace tinyobj;
using namespace std;

namespace capsaicin
{
namespace
{
struct MeshData
{
    vector<float> positions;
    vector<float> normals;
    vector<float> texcoords;
    vector<uint32_t> indices;
};

// Index comparison operator.
struct IndexLess
{
    bool operator()(index_t const& lhs, index_t const& rhs) const
    {
        return lhs.vertex_index != rhs.vertex_index
                   ? lhs.vertex_index < rhs.vertex_index
                   : lhs.normal_index != rhs.normal_index
                         ? lhs.normal_index < rhs.normal_index
                         : lhs.texcoord_index != rhs.texcoord_index ? lhs.texcoord_index < rhs.texcoord_index : false;
    }
};

// Load obj file into a single mesh.
void LoadObjFile(AssetComponent& asset, MeshData& mesh_data)
{
    attrib_t attrib;
    vector<shape_t> shapes;
    vector<material_t> objmaterials;

    string warn;
    string err;

    bool ret = LoadObj(&attrib, &shapes, &objmaterials, &warn, &err, asset.file_name.c_str(), "");

    if (!err.empty())
    {
        error(err);
        throw std::runtime_error(err.c_str());
    }

    if (!ret)
    {
        error("AssetLoadSystem: Couldn't load {}", asset.file_name);
        throw std::runtime_error("Couldn't load {}" + asset.file_name);
    }

    mesh_data.positions.clear();
    mesh_data.normals.clear();
    mesh_data.texcoords.clear();
    mesh_data.indices.clear();

    map<tinyobj::index_t, uint32_t, IndexLess> index_cache;

    for (uint32_t shape_index = 0u; shape_index < shapes.size(); ++shape_index)
    {
        for (uint32_t i = 0u; i < shapes[shape_index].mesh.indices.size(); ++i)
        {
            auto index = shapes[shape_index].mesh.indices[i];
            auto iter = index_cache.find(index);
            if (iter != index_cache.cend())
            {
                mesh_data.indices.push_back(iter->second);
            }
            else
            {
                uint32_t vertex_index = (uint32_t)mesh_data.positions.size() / 3;
                mesh_data.indices.push_back(vertex_index);
                index_cache[index] = vertex_index;

                mesh_data.positions.push_back(attrib.vertices[3 * index.vertex_index]);
                mesh_data.positions.push_back(attrib.vertices[3 * index.vertex_index + 1]);
                mesh_data.positions.push_back(attrib.vertices[3 * index.vertex_index + 2]);

                if (index.normal_index != -1)
                {
                    mesh_data.normals.push_back(attrib.normals[3 * index.normal_index]);
                    mesh_data.normals.push_back(attrib.normals[3 * index.normal_index + 1]);
                    mesh_data.normals.push_back(attrib.normals[3 * index.normal_index + 2]);
                }
                else
                {
                    mesh_data.normals.push_back(0.f);
                    mesh_data.normals.push_back(0.f);
                    mesh_data.normals.push_back(0.f);
                }

                if (index.texcoord_index != -1)
                {
                    mesh_data.texcoords.push_back(attrib.texcoords[2 * index.texcoord_index]);
                    mesh_data.texcoords.push_back(attrib.texcoords[2 * index.texcoord_index + 1]);
                }
                else
                {
                    mesh_data.texcoords.push_back(0.f);
                    mesh_data.texcoords.push_back(0.f);
                }
            }
        }
    }
}

void CreateGPUBuffers(const MeshData& mesh_data,
                      MeshComponent& mesh_component,
                      ID3D12GraphicsCommandList* command_list,
                      RenderSystem& render_system)
{
    // Create mesh buffers in GPU memory.
    mesh_component.vertices =
        dx12api().CreateUAVBuffer(mesh_data.positions.size() * sizeof(float), D3D12_RESOURCE_STATE_COPY_DEST);
    mesh_component.indices =
        dx12api().CreateUAVBuffer(mesh_data.indices.size() * sizeof(uint32_t), D3D12_RESOURCE_STATE_COPY_DEST);

    // Create upload buffers.
    auto vertex_upload_buffer =
        dx12api().CreateUploadBuffer(mesh_data.positions.size() * sizeof(float), mesh_data.positions.data());
    auto index_upload_buffer =
        dx12api().CreateUploadBuffer(mesh_data.indices.size() * sizeof(uint32_t), mesh_data.indices.data());

    render_system.AddAutoreleaseResource(vertex_upload_buffer);
    render_system.AddAutoreleaseResource(index_upload_buffer);

    // Transitions for mesh buffes to non pixel shader resource.
    D3D12_RESOURCE_BARRIER copy_dest_to_uav[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(mesh_component.vertices.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(mesh_component.indices.Get(),
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};

    // Copy data.
    command_list->CopyBufferRegion(
        mesh_component.vertices.Get(), 0, vertex_upload_buffer.Get(), 0, mesh_data.positions.size() * sizeof(float));
    command_list->CopyBufferRegion(
        mesh_component.indices.Get(), 0, index_upload_buffer.Get(), 0, mesh_data.indices.size() * sizeof(uint32_t));

    // Transition to UAV.
    command_list->ResourceBarrier(2, &copy_dest_to_uav[0]);
}
}  // namespace

void AssetLoadSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    // Create command list if needed.
    if (!upload_command_list_)
    {
        upload_command_list_ = dx12api().CreateCommandList(render_system.current_frame_command_allocator());
        upload_command_list_->Close();
    }

    auto& assets = access.Read<AssetComponent>();
    auto& meshes = access.Read<MeshComponent>();

    // Find entities with AssetComponents which have not been loaded yet.
    auto entities =
        entity_query()
            .Filter([&assets, &meshes](Entity e) { return assets.HasComponent(e) && !meshes.HasComponent(e); })
            .entities();

    if (!entities.empty())
    {
        info("AssetLoadSystem: found {} assets", entities.size());
    }

    if (!entities.empty())
    {
        upload_command_list_->Reset(render_system.current_frame_command_allocator(), nullptr);

        // Load asset.
        for (auto e : entities)
        {
            auto& asset = world().GetComponent<AssetComponent>(e);
            auto& gpu_mesh = world().AddComponent<MeshComponent>(e);

            info("AssetLoadSystem: Loading {}", asset.file_name);

            MeshData mesh_data;
            LoadObjFile(asset, mesh_data);

            info("AssetLoadSystem: Allocating GPU buffers for {}", asset.file_name);
            CreateGPUBuffers(mesh_data, gpu_mesh, upload_command_list_.Get(), render_system);
        }

        upload_command_list_->Close();
        render_system.PushCommandList(upload_command_list_.Get());
    }
}
}  // namespace capsaicin