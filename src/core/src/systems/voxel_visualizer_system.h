#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
class VoxelVisualizerSystem : public System
{
public:
    VoxelVisualizerSystem();

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

    ID3D12Resource* output() { return output_.Get(); }

private:
    void InitPipeline();
    void InitOutput();
    uint32_t PopulateOutputDescriptorTable();

    ComPtr<ID3D12GraphicsCommandList> render_command_list_ = nullptr;

    ComPtr<ID3D12Resource> output_ = nullptr;

    ComPtr<ID3D12RootSignature> root_signature_ = nullptr;
    ComPtr<ID3D12PipelineState> pipeline_state_ = nullptr;
};

}  // namespace capsaicin