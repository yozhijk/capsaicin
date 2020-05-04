#pragma once

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
// TODO: remove from here later
struct SettingsComponent
{
    bool vsync = false;
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