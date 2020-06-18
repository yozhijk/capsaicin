#pragma once

#define NOMINMAX
#include <DirectXMath.h>
#include <Keyboard.h>
#include <Mouse.h>

#include "src/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"
#include "src/systems/camera_system.h"

using namespace capsaicin::dx12;
using namespace DirectX;

namespace capsaicin
{
class InputSystem : public System
{
public:
    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

    void ProcessInput(void* input);

private:
    void HandleKeyboard(CameraData& camera_data, float dt);
    void HandleMouse(CameraData& camera_data, float dt);

    Keyboard                       keyboard_;
    Keyboard::KeyboardStateTracker keyboard_tracker_;
    Mouse                          mouse_;

    float   pitch_   = 0.f;
    float   yaw_     = 0.f;
    int32_t mouse_x_ = INT_MAX;
    int32_t mouse_y_ = INT_MAX;
};
}  // namespace capsaicin