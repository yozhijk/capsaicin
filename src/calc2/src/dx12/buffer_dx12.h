#pragma once

#include "buffer.h"
#include "src/dx12/dx12.h"

namespace calc2
{
class BufferDX12 : public Buffer
{
public:
    BufferDX12(const BufferDesc& desc);
    ~BufferDX12() noexcept override = default;

    BufferDX12(const BufferDX12&) = delete;
    BufferDX12& operator=(const BufferDX12&) = delete;

    operator ID3D12Resource*() const noexcept { return resource_.Get(); }

private:
    ComPtr<ID3D12Resource> resource_;
};

}  // namespace calc2