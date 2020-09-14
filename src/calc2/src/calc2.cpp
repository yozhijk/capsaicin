#include "calc2.h"

#include "src/dx12/device_dx12.h"

namespace calc2
{
unique_ptr<Device> CreateDevice()
{
    return std::make_unique<DeviceDX12>();
}
}  // namespace calc2
