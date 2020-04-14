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
    ComPtr<ID3D12Resource> GetTexture(uint32_t index);
    uint32_t GetTextureIndex(const std::string& name);

private:
    uint32_t LoadTexture(const std::string& name);
    std::vector<ComPtr<ID3D12Resource>> textures_;
    std::unordered_map<std::string, uint32_t> cache_;
};
}  // namespace capsaicin