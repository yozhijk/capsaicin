#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
class BLASSystem : public System
{
public:
    BLASSystem();
    ~BLASSystem() override = default;

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;
};
}  // namespace capsaicin