#include "image_dx12.h"

#include "src/dx12/format_conversion.h"
#include "src/dx12/d3dx12.h"

namespace calc2
{

ImageDX12::ImageDX12(const ImageDesc& desc) : Image(desc)
{
    auto format = CalcFormatToDXGI(desc.format);
    auto flags  = GetDXGIFlags(desc.type);

    auto resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D(format, desc.width, desc.height, 1, 0, 1, 0, flags);

    resource_ = Dx12::instance().CreateResource(resource_desc,
                                                CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                                D3D12_RESOURCE_STATE_COMMON);
}

D3D12_UNORDERED_ACCESS_VIEW_DESC ImageDX12::uav_desc() const noexcept
{
    const auto& img_desc = desc();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.Format        = CalcFormatToDXGI(img_desc.format);
    uav_desc.ViewDimension = CalcDimToUAV(img_desc.dim);

    switch (img_desc.dim)
    {
    case ImageDim::k1D:
        uav_desc.Texture1D.MipSlice = 0;
    case ImageDim::k2D:
        uav_desc.Texture2D.MipSlice   = 0;
        uav_desc.Texture2D.PlaneSlice = 0;
    case ImageDim::k3D:
        uav_desc.Texture3D.FirstWSlice = 0;
        uav_desc.Texture3D.MipSlice    = 0;
        uav_desc.Texture3D.WSize       = 0;
    }

    return uav_desc;
}

D3D12_SHADER_RESOURCE_VIEW_DESC ImageDX12::srv_desc() const noexcept
{
    const auto& img_desc = desc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    srv_desc.Format                  = CalcFormatToDXGI(img_desc.format);
    srv_desc.ViewDimension           = CalcDimToSRV(img_desc.dim);
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    switch (img_desc.dim)
    {
    case ImageDim::k1D:
        srv_desc.Texture1D.MostDetailedMip     = 0;
        srv_desc.Texture1D.MipLevels           = 1;
        srv_desc.Texture1D.ResourceMinLODClamp = 0.f;
    case ImageDim::k2D:
        srv_desc.Texture2D.MostDetailedMip     = 0;
        srv_desc.Texture2D.MipLevels           = 1;
        srv_desc.Texture2D.ResourceMinLODClamp = 0.f;
        srv_desc.Texture2D.PlaneSlice          = 0;
    case ImageDim::k3D:
        srv_desc.Texture3D.MostDetailedMip     = 0;
        srv_desc.Texture3D.MipLevels           = 1;
        srv_desc.Texture3D.ResourceMinLODClamp = 0.f;
    }

    return srv_desc;
}
}  // namespace calc2
