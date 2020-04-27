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
    ID3D12Resource* mesh_desc_buffer;
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
    void InitTemporalAccumulatePipelines();
    void InitRenderStructures();
    void InitEAWDenoisePipeline();
    void InitCombinePipeline();

    void CopyGBuffer();

    void Raytrace(ID3D12Resource* scene,
                  ID3D12Resource* camera,
                  uint32_t scene_data_descriptor_table,
                  uint32_t scene_textures_descriptor_table,
                  uint32_t internal_descriptor_table,
                  uint32_t output_descriptor_table);

    void IntegrateTemporally(ID3D12Resource* camera,
                             ID3D12Resource* prev_camera,
                             uint32_t internal_descriptor_table,
                             uint32_t output_descriptor_table,
                             uint32_t history_descriptor_table);

    void ApplyTAA(ID3D12Resource* camera,
                  ID3D12Resource* prev_camera,
                  uint32_t internal_descriptor_table,
                  uint32_t output_descriptor_table,
                  uint32_t history_descriptor_table);

    void CombineIllumination(uint32_t output_descriptor_table);

    void Denoise(uint32_t descriptor_table);

    uint32_t PopulateSceneDataDescriptorTable(GPUSceneData& scene_data);
    uint32_t PopulateOutputDescriptorTable();
    uint32_t PopulateInternalDataDescritptorTable();
    uint32_t PopulateIndirectHistoryDescritorTable();
    uint32_t PopulateCombinedHistoryDescritorTable();
    uint32_t PopulateEAWOutputDescritorTable();
    uint32_t PopulateIndirectTAInputDescritorTable();
    uint32_t PopulateDirectTAInputDescritorTable();
    uint32_t PopulateSceneTexturesDescriptorTable();

    ComPtr<ID3D12GraphicsCommandList> upload_command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> raytracing_command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> copy_gbuffer_command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> indirect_ta_command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> taa_command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> eaw_command_list_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> ci_command_list_ = nullptr;

    ComPtr<ID3D12Resource> output_direct_ = nullptr;
    ComPtr<ID3D12Resource> output_indirect_ = nullptr;
    ComPtr<ID3D12Resource> output_temp_ = nullptr;

    // Shader tables.
    ComPtr<ID3D12Resource> raygen_shader_table = nullptr;
    ComPtr<ID3D12Resource> hitgroup_shader_table = nullptr;
    ComPtr<ID3D12Resource> miss_shader_table = nullptr;

    ComPtr<ID3D12RootSignature> raytracing_root_signature_ = nullptr;
    ComPtr<ID3D12StateObject> raytracing_pipeline_state_ = nullptr;

    // Temporal accumulation for irradiance buffer.
    ComPtr<ID3D12RootSignature> ta_root_signature_ = nullptr;
    ComPtr<ID3D12PipelineState> ta_pipeline_state_ = nullptr;

    // Temporal antialiasing.
    ComPtr<ID3D12PipelineState> taa_pipeline_state_ = nullptr;

    ComPtr<ID3D12RootSignature> eaw_root_signature_ = nullptr;
    ComPtr<ID3D12PipelineState> eaw_pipeline_state_ = nullptr;

    ComPtr<ID3D12RootSignature> ci_root_signature_ = nullptr;
    ComPtr<ID3D12PipelineState> ci_pipeline_state_ = nullptr;

    // Render outputs and textures.
    ComPtr<ID3D12Resource> blue_noise_texture_ = nullptr;
    // Temporal history is ping-ponged.
    ComPtr<ID3D12Resource> indirect_history_[2] = {nullptr};
    ComPtr<ID3D12Resource> combined_history_[2] = {nullptr};
    ComPtr<ID3D12Resource> moments_history_[2] = {nullptr};
    // GBuffer data is used for TAA and ping-ponged as well.
    ComPtr<ID3D12Resource> gbuffer_normal_depth_ = nullptr;
    ComPtr<ID3D12Resource> gbuffer_albedo_ = nullptr;
    ComPtr<ID3D12Resource> prev_gbuffer_normal_depth_ = nullptr;
    ComPtr<ID3D12Resource> temp_moments_[2] = {nullptr};
};
}  // namespace capsaicin