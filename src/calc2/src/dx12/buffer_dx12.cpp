#include "buffer_dx12.h"

namespace calc2
{
BufferDX12::BufferDX12(const BufferDesc& desc) : Buffer(desc)
{
    switch (desc.type)
    {
    case BufferType::kConstant:
        resource_ = Dx12::instance().CreateConstantBuffer(desc.size, D3D12_RESOURCE_STATE_COMMON);
        break;
    case BufferType::kUnorderedAccess:
        resource_ = Dx12::instance().CreateUAVBuffer(desc.size, D3D12_RESOURCE_STATE_COMMON);
        break;
    case BufferType::kUpload:
        resource_ = Dx12::instance().CreateUploadBuffer(desc.size);
        break;
    case BufferType::kReadback:
        resource_ = Dx12::instance().CreateReadbackBuffer(desc.size);
        break;
    }
}
}  // namespace calc2
