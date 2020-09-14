#pragma once

#include "image.h"
#include "src/dx12/dx12.h"

namespace calc2
{
class ImageDX12 : public Image
{
public:
    ImageDX12(const ImageDesc& desc);
    ~ImageDX12() noexcept override = default;

    ImageDX12(const ImageDX12&) = delete;
    ImageDX12& operator=(const ImageDX12&) = delete;

    operator ID3D12Resource*() const noexcept { return resource_.Get(); }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc() const noexcept;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc() const noexcept;

private:
    ComPtr<ID3D12Resource> resource_;
};

}  // namespace calc2
