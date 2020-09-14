#pragma once

#include <vector>

#include "calc2.h"
using namespace calc2;

class Test : public testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(Test, CreateDevice)
{
    auto device = calc2::CreateDevice();
    ASSERT_NE(device, nullptr);
}

TEST_F(Test, CreateBuffer)
{
    auto device = calc2::CreateDevice();
    ASSERT_NE(device, nullptr);

    auto buffer = device->CreateBuffer(BufferDesc{BufferType::kConstant, 256});
    ASSERT_NE(buffer, nullptr);

    buffer = device->CreateBuffer(BufferDesc{BufferType::kUnorderedAccess, 256});
    ASSERT_NE(buffer, nullptr);

    buffer = device->CreateBuffer(BufferDesc{BufferType::kUpload, 256});
    ASSERT_NE(buffer, nullptr);

    buffer = device->CreateBuffer(BufferDesc{BufferType::kReadback, 256});
    ASSERT_NE(buffer, nullptr);
}

TEST_F(Test, CreateImage)
{
    auto device = calc2::CreateDevice();
    ASSERT_NE(device, nullptr);

    auto image = device->CreateImage(
        ImageDesc{ImageDim::k2D, ImageType::kSampled, ImageFormat::kRGBA32Float, 256, 256, 1});
    ASSERT_NE(image, nullptr);

    image = device->CreateImage(ImageDesc{
        ImageDim::k2D, ImageType::kUnorderedAccess, ImageFormat::kRGBA8Unorm, 256, 256, 1});
    ASSERT_NE(image, nullptr);
}

TEST_F(Test, AllocateCommandBuffer)
{
    auto device = calc2::CreateDevice();
    ASSERT_NE(device, nullptr);

    auto command_allocator = device->CreateCommandAllocator();
    ASSERT_NE(command_allocator, nullptr);

    auto command_buffer = device->CreateCommandBuffer(*command_allocator);
    ASSERT_NE(command_buffer, nullptr);

    ASSERT_NO_THROW(command_allocator->AllocateCommandBuffer(*command_buffer));
}

TEST_F(Test, CreateFence)
{
    auto device = calc2::CreateDevice();
    ASSERT_NE(device, nullptr);

    auto fence = device->CreateFence();
    ASSERT_NE(fence, nullptr);
}

TEST_F(Test, CreateProgram)
{
    auto device = calc2::CreateDevice();
    ASSERT_NE(device, nullptr);

    ProgramDesc desc{"C:/dev/capsaicin/src/calc2/tests/nBodyGravityCS.hlsl", "CSMain", "cs_6_3"};

    auto program = device->CreateProgram(desc);
    ASSERT_NE(program, nullptr);
}
