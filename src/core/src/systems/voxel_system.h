#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"
#include "src/utils/vector_math.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
class VoxelSystem : public System
{
public:
    VoxelSystem();

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

    Aabb scene_aabb() const { return scene_aabb_; }
    ID3D12Resource* grid_buffer() const { return grid_buffer_.Get(); }
    ID3D12Resource* octree_buffer() const { return octree_buffer_.Get(); }

private:
    ComPtr<ID3D12GraphicsCommandList> upload_command_list_ = nullptr;
    ComPtr<ID3D12Resource> grid_buffer_ = nullptr;
    ComPtr<ID3D12Resource> octree_buffer_ = nullptr;
    Aabb scene_aabb_;
};
}  // namespace capsaicin