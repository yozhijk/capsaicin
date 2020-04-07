#pragma once

#include <DirectXMath.h>

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;
using namespace DirectX;

namespace capsaicin
{
class TextureSystem : public System
{
public:
    TextureSystem() = default;
    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override {}

    ComPtr<ID3D12Resource> GetTexture(const std::string& name);

private:
    ComPtr<ID3D12Resource> LoadTexture(const std::string& name);
    std::unordered_map<std::string, ComPtr<ID3D12Resource>> textures_;
};
}  // namespace capsaicin