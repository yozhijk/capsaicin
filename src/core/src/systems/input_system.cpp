#include "input_system.h"

#include "capsaicin.h"
#include "src/common.h"
#include "src/systems/camera_system.h"

namespace capsaicin
{
void InputSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    static auto time = std::chrono::high_resolution_clock::now();
    static auto prev_time = std::chrono::high_resolution_clock::now();
    static float rotation = 0.f;

    time = std::chrono::high_resolution_clock::now();
    auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time - prev_time).count();

    auto& cameras = access.Write<CameraComponent>();
    auto entities = entity_query().Filter([&cameras](Entity e) { return cameras.HasComponent(e); }).entities();

    if (entities.size() != 1)
    {
        error("CameraSystem: no cameras found");
    }

    auto& camera_data = cameras.GetComponent(entities[0]);
    HandleMouse(camera_data.camera_data, delta_ms);
    HandleKeyboard(camera_data.camera_data, delta_ms);
    prev_time = time;
}

void InputSystem::ProcessInput(void* input)
{
    Input* w32input = reinterpret_cast<Input*>(input);
    Keyboard::ProcessMessage(w32input->message, w32input->wparam, w32input->lparam);
    Mouse::ProcessMessage(w32input->message, w32input->wparam, w32input->lparam);
}

void InputSystem::HandleKeyboard(CameraData& camera_data, float dt)
{
    auto keyboard_state = keyboard_.GetState();

    XMFLOAT3 movement{0.f, 0.f, 0.f};
    constexpr float kMovementSpeed = 0.0125f;

    if (keyboard_state.IsKeyDown(Keyboard::A))
    {
        movement.x -= camera_data.right.x * kMovementSpeed * dt;
        movement.y -= camera_data.right.y * kMovementSpeed * dt;
        movement.z -= camera_data.right.z * kMovementSpeed * dt;
    }

    if (keyboard_state.IsKeyDown(Keyboard::D))
    {
        movement.x += camera_data.right.x * kMovementSpeed * dt;
        movement.y += camera_data.right.y * kMovementSpeed * dt;
        movement.z += camera_data.right.z * kMovementSpeed * dt;
    }

    if (keyboard_state.IsKeyDown(Keyboard::S))
    {
        movement.x -= camera_data.forward.x * kMovementSpeed * dt;
        movement.y -= camera_data.forward.y * kMovementSpeed * dt;
        movement.z -= camera_data.forward.z * kMovementSpeed * dt;
    }

    if (keyboard_state.IsKeyDown(Keyboard::W))
    {
        movement.x += camera_data.forward.x * kMovementSpeed * dt;
        movement.y += camera_data.forward.y * kMovementSpeed * dt;
        movement.z += camera_data.forward.z * kMovementSpeed * dt;
    }

    if (keyboard_state.IsKeyDown(Keyboard::Q))
    {
        movement.x -= camera_data.up.x * kMovementSpeed * dt;
        movement.y -= camera_data.up.y * kMovementSpeed * dt;
        movement.z -= camera_data.up.z * kMovementSpeed * dt;
    }

    if (keyboard_state.IsKeyDown(Keyboard::E))
    {
        movement.x += camera_data.up.x * kMovementSpeed * dt;
        movement.y += camera_data.up.y * kMovementSpeed * dt;
        movement.z += camera_data.up.z * kMovementSpeed * dt;
    }

    camera_data.position.x += movement.x;
    camera_data.position.y += movement.y;
    camera_data.position.z += movement.z;

    keyboard_tracker_.Update(keyboard_state);
}
void InputSystem::HandleMouse(CameraData& camera_data, float dt)
{
    Mouse::State mouse = mouse_.GetState();

    if (mouse.leftButton)
    {
        if (mouse_x_ == INT_MAX && mouse_y_ == INT_MAX)
        {
            mouse_x_ = mouse.x;
            mouse_y_ = mouse.y;
            return;
        }

        constexpr float kMouseSensitivity = 0.025f;
        yaw_ += (float)(mouse.x - mouse_x_) * kMouseSensitivity * dt;
        pitch_ += (float)(mouse.y - mouse_y_) * kMouseSensitivity * dt;

        if (abs(yaw_) >= 360.f)
            yaw_ = 0.f;
        if (abs(pitch_) >= 360.f)
            pitch_ = 0.f;

        mouse_x_ = mouse.x;
        mouse_y_ = mouse.y;

        XMFLOAT3 up = XMFLOAT3(0.f, 1.f, 0.f);
        XMFLOAT3 forward = XMFLOAT3(0.f, 0.f, 1.f);
        XMMATRIX rotation = XMMatrixRotationRollPitchYaw(pitch_ * (XM_PI / 180.f), yaw_ * (XM_PI / 180.f), 0.f);

        XMStoreFloat3(&camera_data.forward, XMVector3Normalize(XMVector3Transform(XMLoadFloat3(&forward), rotation)));
        XMStoreFloat3(&camera_data.right,
                      XMVector3Normalize(-XMVector3Cross(XMLoadFloat3(&camera_data.forward), XMLoadFloat3(&up))));
        XMStoreFloat3(&camera_data.up,
                      XMVector3Cross(XMLoadFloat3(&camera_data.forward), XMLoadFloat3(&camera_data.right)));

        return;
    }

    mouse_x_ = INT_MAX;
    mouse_y_ = INT_MAX;
}
}  // namespace capsaicin