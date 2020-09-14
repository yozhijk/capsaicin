#pragma once

#include "src/dx12/common_dx12.h"

namespace calc2
{
inline DXGI_FORMAT CalcFormatToDXGI(ImageFormat format)
{
    switch (format)
    {
    case ImageFormat::kRGBA8Unorm:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::kRGBA16Float:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case ImageFormat::kRGBA32Float:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }

    return DXGI_FORMAT_UNKNOWN;
}

inline D3D12_RESOURCE_FLAGS GetDXGIFlags(ImageType type)
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

    if (type == ImageType::kUnorderedAccess)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return flags;
}

inline D3D12_UAV_DIMENSION CalcDimToUAV(ImageDim dim)
{
    switch (dim)
    {
    case ImageDim::k1D:
        return D3D12_UAV_DIMENSION_TEXTURE1D;
    case ImageDim::k2D:
        return D3D12_UAV_DIMENSION_TEXTURE2D;
    case ImageDim::k3D:
        return D3D12_UAV_DIMENSION_TEXTURE3D;
    }

    return D3D12_UAV_DIMENSION_UNKNOWN;
}

inline D3D12_SRV_DIMENSION CalcDimToSRV(ImageDim dim)
{
    switch (dim)
    {
    case ImageDim::k1D:
        return D3D12_SRV_DIMENSION_TEXTURE1D;
    case ImageDim::k2D:
        return D3D12_SRV_DIMENSION_TEXTURE2D;
    case ImageDim::k3D:
        return D3D12_SRV_DIMENSION_TEXTURE3D;
    }

    return D3D12_SRV_DIMENSION_UNKNOWN;
}
}  // namespace calc2