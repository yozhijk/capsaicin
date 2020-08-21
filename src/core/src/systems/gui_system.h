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
    bool eaw5    = true;

    float eaw_normal_sigma = 128.f;
    float eaw_depth_sigma  = 1.f;
    float eaw_luma_sigma   = 1.f;

    float gather_normal_sigma = 64.f;
    float gather_depth_sigma  = 2.f;
    float gather_luma_sigma   = 3.f;

    float temporal_upscale_feedback = 0.975f;
    float taa_feedback              = 0.9f;

    int output = kCombined;
    int num_diffuse_bounces = 1u;
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