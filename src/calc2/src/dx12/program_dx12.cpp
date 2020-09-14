#include "program_dx12.h"

namespace calc2
{
ProgramDX12::ProgramDX12(const Shader& shader)
{
    pipeline_state_ = dx12api().CreateComputePipelineState(shader, nullptr);
}
void ProgramDX12::SetConstantBuffer(uint32_t slot, Buffer& buffer)
{
    constant_buffers_[slot] = &buffer;
}
void ProgramDX12::SetConstants(void* data, size_t size)
{
    constant_data_.resize(size);
    auto src = static_cast<uint8_t*>(data);
    std::copy(src, src + size, constant_data_.data());
}
void ProgramDX12::SetBuffer(uint32_t slot, Buffer& buffer)
{
    uav_buffers_[slot] = &buffer;
}
void ProgramDX12::SetImage(uint32_t slot, Image& image)
{
    uav_images_[slot] = &image;
}
void ProgramDX12::SetSampledImage(uint32_t slot, Image& image)
{
    srv_images_[slot] = &image;
}
}  // namespace calc2