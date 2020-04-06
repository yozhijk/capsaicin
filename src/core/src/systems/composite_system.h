#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"
#include "src/dx12/shader_compiler.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
class CompositeSystem : public System
{
public:
    CompositeSystem();
    ~CompositeSystem();

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

private:
    void InitPipeline();
    uint32_t PopulateDescriptorTable();

    void Render(float time, uint32_t output_srv_index);

    ComPtr<ID3D12GraphicsCommandList> command_list_ = nullptr;

    //
    ComPtr<ID3D12RootSignature> root_signature_ = nullptr;
    ComPtr<ID3D12PipelineState> pipeline_state_ = nullptr;
};
}  // namespace capsaicin