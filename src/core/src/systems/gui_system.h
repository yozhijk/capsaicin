#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
enum OutputType
{
    kCombined,
    kDirect,
    kIndirect,
    kVariance
};

// TODO: remove from here later
struct SettingsComponent
{
    bool vsync   = false;
    bool denoise = true;
    bool gather  = true;

    float normal_sigma = 128.f;
    float depth_sigma  = 0.01f;
    float luma_sigma   = 1.f;

    float temporal_upscale_feedback = 0.95f;
    float taa_feedback              = 0.9f;

    int output = kCombined;
};

class GUISystem : public System
{
public:
    GUISystem(HWND hwnd);
    ~GUISystem();

    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

private:
    void RenderGUI(SettingsComponent& settings);

    // Descriptor heap for ImGUI library.
    ComPtr<ID3D12DescriptorHeap> imgui_descriptor_heap_ = nullptr;
    // GUI rendering command list.
    ComPtr<ID3D12GraphicsCommandList> gui_command_list_ = nullptr;
};
}  // namespace capsaicin