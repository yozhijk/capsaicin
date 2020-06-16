#pragma once

#include "src/common.h"
#include "src/dx12/common.h"
#include "src/dx12/d3dx12.h"
#include "src/dx12/dx12.h"

#include "src/systems/render_system.h"

using namespace capsaicin::dx12;

namespace capsaicin
{
// Camera data.
struct  CameraData
{
    XMFLOAT3 position;
    float focal_length;

    XMFLOAT3 right;
    float znear;

    XMFLOAT3 forward;
    float focus_distance;

    XMFLOAT3 up;
    float aperture;

    XMFLOAT2 sensor_size;
};

// Camera component.
struct CameraComponent
{
    CameraData camera_data;
    ComPtr<ID3D12Resource> camera_buffer = nullptr;
    ComPtr<ID3D12Resource> prev_camera_buffer = nullptr;
};

class CameraSystem : public System
{
public:
    CameraSystem();
    void Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow) override;

private:
    ComPtr<ID3D12GraphicsCommandList> upload_command_list_ = nullptr;
    ComPtr<ID3D12Resource> camera_staging_buffer_ = nullptr;
};
}  // namespace capsaicin