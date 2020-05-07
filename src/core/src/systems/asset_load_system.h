#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
struct AssetComponent
{
    std::string file_name;
};

struct GeometryStorage
{
    ComPtr<ID3D12Resource> vertices = nullptr;
    ComPtr<ID3D12Resource> normals = nullptr;
    ComPtr<ID3D12Resource> texcoords = nullptr;
    ComPtr<ID3D12Resource> indices = nullptr;
    ComPtr<ID3D12Resource> mesh_descs = nullptr;

    uint32_t mesh_count = 0;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
};

struct MeshComponent
{
    uint32_t vertex_count = 0;
    uint32_t first_vertex_offset = 0;
    uint32_t index_count = 0;
    uint32_t first_index_offset = 0;

    uint32_t index = 0;
    uint32_t material_index = ~0u;
    uint32_t padding[2];
};

class AssetLoadSystem : public System
{
public:
    static constexpr uint32_t kVertexPoolSize = 40000000;
    static constexpr uint32_t kIndexPoolSize = 40000000;
    static constexpr uint32_t kMeshPoolSize = 30000;
    AssetLoadSystem();
    ~AssetLoadSystem() override = default;

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

    GeometryStorage& geometry_storage() { return storage_; }

private:
    ComPtr<ID3D12GraphicsCommandList> upload_command_list_ = nullptr;
    GeometryStorage storage_;
};
}  // namespace capsaicins