#pragma once

#include "common.h"
#include "device.h"
#include "buffer.h"
#include "image.h"
#include "fence.h"
#include "command_allocator.h"
#include "command_buffer.h"
#include "program.h"

namespace calc2
{
unique_ptr<Device> CreateDevice();
}  // namespace calc2
