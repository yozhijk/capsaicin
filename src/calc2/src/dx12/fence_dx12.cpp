#include "fence_dx12.h"

namespace calc2
{
FenceDX12::FenceDX12(ComPtr<ID3D12Fence> fence) : fence_(fence)
{
    win32_event_ = CreateEvent(nullptr, FALSE, FALSE, "Calc2 event");
}

FenceDX12::~FenceDX12()
{
    CloseHandle(win32_event_);
}

void FenceDX12::Wait(uint32_t min_value)
{
    auto fence_value = fence_->GetCompletedValue();

    if (fence_value < min_value)
    {
        fence_->SetEventOnCompletion(min_value, win32_event_);
        WaitForSingleObject(win32_event_, INFINITE);
    }
}
}  // namespace calc2