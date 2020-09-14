#pragma once

#include "fence.h"
#include "src/dx12/dx12.h"

namespace calc2
{
class FenceDX12 : public Fence
{
public:
    FenceDX12(ComPtr<ID3D12Fence> fence);

    FenceDX12(const FenceDX12&) = delete;
    FenceDX12& operator=(const FenceDX12&) = delete;

    ~FenceDX12() noexcept override;

    void Wait(uint32_t min_value);

    operator ID3D12Fence*() const noexcept { return fence_.Get(); }

private:
    ComPtr<ID3D12Fence> fence_;
    HANDLE              win32_event_ = INVALID_HANDLE_VALUE;
};

}  // namespace calc2