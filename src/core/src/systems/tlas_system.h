#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
// Top level acceleration structure for the scene.
struct TLASComponent
{
    // TLAS resource.
    ComPtr<ID3D12Resource> tlas  = nullptr;
    bool                   built = false;
};

class TLASSystem : public System
{
public:
    TLASSystem();
    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

private:
    ComPtr<ID3D12GraphicsCommandList> build_command_list_ = nullptr;
};
}  // namespace capsaicin