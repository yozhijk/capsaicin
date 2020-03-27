#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
// Bottom level acceleration structure for the mesh.
struct BLASComponent
{
    ComPtr<ID3D12Resource> blas = nullptr;
};

class BLASSystem : public System
{
public:
    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

private:
    ComPtr<ID3D12GraphicsCommandList> build_command_list_ = nullptr;
};
}  // namespace capsaicin