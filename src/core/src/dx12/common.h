#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl.h>
#include <DirectXMath.h>

#include "src/common.h"

using namespace DirectX;

namespace capsaicin::dx12
{
using Microsoft::WRL::ComPtr;
// Helper function to throw runtime_error.
template <typename T>
inline void Throw(T&& data)
{
    error(data);
    throw std::runtime_error(std::forward<T>(data));
}

// Helper function to throw runtime_error.
template <typename T>
inline void ThrowIfFailed(HRESULT hr, T&& data)
{
    if (FAILED(hr))
    {
        Throw(std::forward<T>(data));
    }
}

template <typename S>
inline std::wstring StringToWideString(S&& s)
{
    std::wstring temp(s.length(), L' ');
    std::copy(s.begin(), s.end(), temp.begin());
    return temp;
}

inline std::string WideStringToString(const std::wstring& s)
{
    std::string temp(s.length(), ' ');
    // generates warning
    // std::copy(s.begin(), s.end(), temp.begin());
    for (size_t i = 0; i < temp.size(); ++i) { temp[i] = static_cast<char>(s[i]); }
    return temp;
}

}  // namespace capsaicin::dx12