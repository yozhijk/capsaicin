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
    ID3D12Resource* blue_noise_texture() { return blue_noise_texture_.Get(); }

private:
    void InitPipeline();
    void LoadBlueNoiseTextures();

    void Raytrace(ID3D12Resource* scene,
                  ID3D12Resource* camera,
                  uint32_t scene_data_base_index,
                  uint32_t internal_descriptor_table,
                  uint32_t output_uav_index);

    uint32_t PopulateSceneDataDescriptorTable(GPUSceneData& scene_data);
    uint32_t PopulateOutputDescriptorTable();
    uint32_t PopulateInternalDataDescritptorTable();

    ComPtr<ID3D12GraphicsCommandList> upload_command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> raytracing_command_list_ = nullptr;
    // Use one UAV per render target for better interleaving.
    PerGPUFrameResource<ComPtr<ID3D12Resource>> raytracing_outputs_;
    // Shader tables.
    ComPtr<ID3D12Resource> raygen_shader_table = nullptr;
    ComPtr<ID3D12Resource> hitgroup_shader_table = nullptr;
    ComPtr<ID3D12Resource> miss_shader_table = nullptr;

    ComPtr<ID3D12RootSignature> raytracing_root_signature_ = nullptr;
    ComPtr<ID3D12StateObject> raytracing_pipeline_state_ = nullptr;

    // Render outputs and textures.
    ComPtr<ID3D12Resource> blue_noise_texture_ = nullptr;
    ComPtr<ID3D12Resource> temporal_history_ = nullptr;
};
}  // namespace capsaicin