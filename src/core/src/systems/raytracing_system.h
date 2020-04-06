#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"
#include "src/dx12/shader_compiler.h"
#include "src/systems/render_system.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
struct GPUSceneData
{
    ID3D12Resource* index_buffer;
    ID3D12Resource* vertex_buffer;
    ID3D12Resource* normal_buffer;
    ID3D12Resource* texcoord_buffer;
};

class RaytracingSystem : public System
{
public:
    RaytracingSystem();
    ~RaytracingSystem();

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

    ID3D12Resource* current_frame_output();

private:
    void InitPipeline();

    void Raytrace(ID3D12Resource* scene,
                  ID3D12Resource* camera,
                  uint32_t scene_data_base_index,
                  uint32_t output_uav_index);

    uint32_t PopulateSceneDataDescriptorTable(GPUSceneData& scene_data);
    uint32_t PopulateOutputDescriptorTable();

    ComPtr<ID3D12GraphicsCommandList> raytracing_command_list_ = nullptr;
    // Use one UAV per render target for better interleaving.
    std::array<ComPtr<ID3D12Resource>, RenderSystem::num_gpu_frames_in_flight()> raytracing_outputs_ = {nullptr};

    // Shader tables.
    ComPtr<ID3D12Resource> raygen_shader_table = nullptr;
    ComPtr<ID3D12Resource> hitgroup_shader_table = nullptr;
    ComPtr<ID3D12Resource> miss_shader_table = nullptr;

    ComPtr<ID3D12RootSignature> raytracing_root_signature_ = nullptr;
    ComPtr<ID3D12StateObject> raytracing_pipeline_state_ = nullptr;
};
}  // namespace capsaicin