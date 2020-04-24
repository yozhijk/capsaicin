#include "asset_load_system.h"

#include <DirectXMath.h>

#include "src/common.h"
#include "src/systems/render_system.h"
#include "src/systems/texture_system.h"
#include "tiny_obj_loader.h"

using namespace tinyobj;
using namespace std;
using namespace DirectX;

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
    std::uint32_t texture_index = ~0u;
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
void LoadObjFile(AssetComponent& asset, std::vector<MeshData>& meshes)
{
    attrib_t attrib;
    vector<shape_t> shapes;
    vector<material_t> objmaterials;

    string warn;
    string err;

    bool ret = LoadObj(&attrib, &shapes, &objmaterials, &warn, &err, asset.file_name.c_str(), "../../../assets/");

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

    map<tinyobj::index_t, uint32_t, IndexLess> index_cache;

    std::vector<uint32_t> texture_indices;
    for (uint32_t j = 0; j < objmaterials.size(); ++j)
    {
        if (!objmaterials[j].diffuse_texname.empty())
        {
            std::string path = "";
            std::string full_name = path + objmaterials[j].diffuse_texname;
            texture_indices.push_back(world().GetSystem<TextureSystem>().GetTextureIndex(full_name));
        }
        else
        {
            texture_indices.push_back(~0u);
        }
    }

    for (uint32_t shape_index = 0u; shape_index < shapes.size(); ++shape_index)
    {
        MeshData mesh_data;
        index_cache.clear();

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

        mesh_data.texture_index =
            (shapes[shape_index].mesh.material_ids.empty() || shapes[shape_index].mesh.material_ids[0] == -1)
                ? ~0u
                : texture_indices[shapes[shape_index].mesh.material_ids[0]];

        meshes.push_back(mesh_data);
    }
}

void CreateGeometryStorage(std::vector<MeshData>& mesh_data_array,
                           GeometryStorage& storage,
                           ID3D12GraphicsCommandList* command_list,
                           RenderSystem& render_system)
{
    std::vector<MeshComponent> meshes;

    for (auto& mesh_data : mesh_data_array)
    {
        auto entity = world().CreateEntity().AddComponent<MeshComponent>().Build();
        auto& mesh_component = world().GetComponent<MeshComponent>(entity);
        // auto& mesh_component = world().AddComponent<MeshComponent>(mesh_data.entity);
        mesh_component.first_vertex_offset = storage.vertex_count;
        mesh_component.first_index_offset = storage.index_count;
        mesh_component.vertex_count = mesh_data.positions.size() / 3;
        mesh_component.index_count = mesh_data.indices.size();
        mesh_component.index = meshes.size();
        mesh_component.material_index = mesh_data.texture_index;

        // Create upload buffers.
        auto vertex_upload_buffer =
            dx12api().CreateUploadBuffer(mesh_data.positions.size() * sizeof(float), mesh_data.positions.data());
        auto index_upload_buffer =
            dx12api().CreateUploadBuffer(mesh_data.indices.size() * sizeof(uint32_t), mesh_data.indices.data());
        auto normals_upload_buffer =
            dx12api().CreateUploadBuffer(mesh_data.normals.size() * sizeof(float), mesh_data.normals.data());
        auto texcoord_upload_buffer =
            dx12api().CreateUploadBuffer(mesh_data.texcoords.size() * sizeof(float), mesh_data.texcoords.data());

        render_system.AddAutoreleaseResource(vertex_upload_buffer);
        render_system.AddAutoreleaseResource(index_upload_buffer);
        render_system.AddAutoreleaseResource(normals_upload_buffer);
        render_system.AddAutoreleaseResource(texcoord_upload_buffer);

        // Copy data.
        command_list->CopyBufferRegion(storage.vertices.Get(),
                                       mesh_component.first_vertex_offset * sizeof(XMFLOAT3),
                                       vertex_upload_buffer.Get(),
                                       0,
                                       mesh_data.positions.size() * sizeof(float));
        command_list->CopyBufferRegion(storage.indices.Get(),
                                       mesh_component.first_index_offset * sizeof(uint32_t),
                                       index_upload_buffer.Get(),
                                       0,
                                       mesh_data.indices.size() * sizeof(uint32_t));
        command_list->CopyBufferRegion(storage.normals.Get(),
                                       mesh_component.first_vertex_offset * sizeof(XMFLOAT3),
                                       normals_upload_buffer.Get(),
                                       0,
                                       mesh_data.normals.size() * sizeof(float));
        command_list->CopyBufferRegion(storage.texcoords.Get(),
                                       mesh_component.first_vertex_offset * sizeof(XMFLOAT2),
                                       texcoord_upload_buffer.Get(),
                                       0,
                                       mesh_data.texcoords.size() * sizeof(float));

        meshes.push_back(mesh_component);
        storage.vertex_count += mesh_component.vertex_count;
        storage.index_count += mesh_component.index_count;
        ++storage.mesh_count;
    }

    // Upload mesh buffer.
    auto mesh_upload_buffer = dx12api().CreateUploadBuffer(storage.mesh_count * sizeof(MeshComponent), meshes.data());
    render_system.AddAutoreleaseResource(mesh_upload_buffer);

    command_list->CopyBufferRegion(
        storage.mesh_descs.Get(), 0, mesh_upload_buffer.Get(), 0, storage.mesh_count * sizeof(MeshComponent));

    // Transitions for mesh buffes to non pixel shader resource.
    D3D12_RESOURCE_BARRIER copy_dest_to_uav[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            storage.vertices.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            storage.indices.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            storage.normals.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            storage.mesh_descs.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            storage.texcoords.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};

    // Transition to UAV.
    command_list->ResourceBarrier(ARRAYSIZE(copy_dest_to_uav), copy_dest_to_uav);
}
}  // namespace

AssetLoadSystem::AssetLoadSystem()
{
    storage_.vertices = dx12api().CreateUAVBuffer(kVertexPoolSize * sizeof(XMFLOAT3), D3D12_RESOURCE_STATE_COPY_DEST);
    storage_.indices = dx12api().CreateUAVBuffer(kIndexPoolSize * sizeof(uint32_t), D3D12_RESOURCE_STATE_COPY_DEST);
    storage_.normals = dx12api().CreateUAVBuffer(kVertexPoolSize * sizeof(XMFLOAT3), D3D12_RESOURCE_STATE_COPY_DEST);
    storage_.texcoords = dx12api().CreateUAVBuffer(kVertexPoolSize * sizeof(XMFLOAT2), D3D12_RESOURCE_STATE_COPY_DEST);
    storage_.mesh_descs =
        dx12api().CreateUAVBuffer(kMeshPoolSize * sizeof(MeshComponent), D3D12_RESOURCE_STATE_COPY_DEST);
}

void AssetLoadSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    // Create command list if needed.
    if (!upload_command_list_)
    {
        upload_command_list_ = dx12api().CreateCommandList(render_system.current_frame_command_allocator());
        upload_command_list_->Close();
    }

    auto& assets = access.Write<AssetComponent>();

    // Find entities with AssetComponents which have not been loaded yet.
    auto entities = entity_query().Filter([&assets](Entity e) { return assets.HasComponent(e); }).entities();

    if (!entities.empty())
    {
        info("AssetLoadSystem: found {} assets", entities.size());
    }

    if (!entities.empty())
    {
        // Load asset.
        std::vector<MeshData> meshes;
        for (auto e : entities)
        {
            auto& asset = world().GetComponent<AssetComponent>(e);
            info("AssetLoadSystem: Loading {}", asset.file_name);

            MeshData mesh_data;
            LoadObjFile(asset, meshes);

            world().DestroyEntity(e);
        }

        upload_command_list_->Reset(render_system.current_frame_command_allocator(), nullptr);

        info("AssetLoadSystem: Allocating GPU buffers");

        CreateGeometryStorage(meshes, storage_, upload_command_list_.Get(), render_system);

        upload_command_list_->Close();
        render_system.PushCommandList(upload_command_list_);
    }
}
}  // namespace capsaicin