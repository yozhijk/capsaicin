#pragma once

#include "program.h"
#include "src/dx12/dx12.h"
#include "src/dx12/shader_compiler.h"

namespace calc2
{
class Shader;
class ProgramDX12 : public Program
{
public:
    ProgramDX12(const Shader& shader);
    ~ProgramDX12() noexcept override = default;

    ProgramDX12(const ProgramDX12&) = delete;
    ProgramDX12& operator=(const ProgramDX12&) = delete;

    void SetConstantBuffer(uint32_t slot, Buffer& buffer) override;
    void SetConstants(void* data, size_t size) override;
    void SetBuffer(uint32_t slot, Buffer& buffer) override;
    void SetImage(uint32_t slot, Image& image) override;
    void SetSampledImage(uint32_t slot, Image& image) override;

    ID3D12PipelineState* pipeline_state() const { return pipeline_state_.Get(); }

private:
    ComPtr<ID3D12PipelineState>      pipeline_state_;
    unordered_map<uint32_t, Buffer*> constant_buffers_;
    unordered_map<uint32_t, Buffer*> uav_buffers_;
    unordered_map<uint32_t, Image*>  uav_images_;
    unordered_map<uint32_t, Image*>  srv_images_;
    vector<uint8_t>                  constant_data_;
};

}  // namespace calc2