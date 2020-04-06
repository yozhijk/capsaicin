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

struct MeshComponent
{
    ComPtr<ID3D12Resource> vertices;
    ComPtr<ID3D12Resource> normals;
    ComPtr<ID3D12Resource> texcoords;
    ComPtr<ID3D12Resource> indices;

    uint32_t triangle_count;
};

class AssetLoadSystem : public System
{
public:
    ~AssetLoadSystem() override = default;

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

private:
    ComPtr<ID3D12GraphicsCommandList> upload_command_list_ = nullptr;
};
}  // namespace capsaicins