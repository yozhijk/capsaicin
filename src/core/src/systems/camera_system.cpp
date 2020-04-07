#include "camera_system.h"

#include "src/common.h"
#include "src/systems/render_system.h"

namespace capsaicin
{
namespace
{
void AdjustCameraAspectBasedOnWindow(CameraData& data)
{
    // info("CameraSystem: Adjusting camera sensor height to match window apsect (disable if not needed)");
    auto& render_system = world().GetSystem<RenderSystem>();
    float aspect = float(render_system.window_height()) / render_system.window_width();
    data.sensor_size.y = data.sensor_size.x * aspect;
}
}  // namespace

CameraSystem::CameraSystem()
{
    auto e = world().CreateEntity().AddComponent<CameraComponent>().Build();
    auto& camera_component = world().GetComponent<CameraComponent>(e);

    camera_component.camera_data.position = XMFLOAT3{0.f, 15.f, 0.f};
    camera_component.camera_data.right = XMFLOAT3{1.f, 0.f, 0.f};
    camera_component.camera_data.forward = XMFLOAT3{0.f, 0.f, 1.f};
    camera_component.camera_data.up = XMFLOAT3{0.f, 1.f, 0.f};

    camera_component.camera_data.sensor_size.x = 0.036f;
    camera_component.camera_data.sensor_size.y = 0.024f;
    camera_component.camera_data.focal_length = 0.024f;

    auto structure_size = align(sizeof(CameraData), RenderSystem::constant_buffer_alignment());

    camera_component.camera_buffer =
        dx12api().CreateConstantBuffer(structure_size, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    camera_component.prev_camera_buffer =
        dx12api().CreateConstantBuffer(structure_size, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    camera_staging_buffer_ = dx12api().CreateUploadBuffer(RenderSystem::num_gpu_frames_in_flight() * structure_size);
}

void CameraSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto& render_system = world().GetSystem<RenderSystem>();

    if (!upload_command_list_)
    {
        upload_command_list_ = dx12api().CreateCommandList(render_system.current_frame_command_allocator());
        upload_command_list_->Close();
    }

    upload_command_list_->Reset(render_system.current_frame_command_allocator(), nullptr);

    auto& cameras = access.Read<CameraComponent>();
    auto entities = entity_query().Filter([&cameras](Entity e) { return cameras.HasComponent(e); }).entities();

    if (entities.size() != 1)
    {
        error("CameraSystem: no cameras found");
    }

    auto camera = cameras.GetComponent(entities[0]);

    // Adjust aspec ration for the camera if needed.
    AdjustCameraAspectBasedOnWindow(camera.camera_data);

    auto idx = render_system.current_gpu_frame_index();
    auto staging_buffer = camera_staging_buffer_.Get();
    auto structure_size = align(sizeof(CameraData), RenderSystem::constant_buffer_alignment());

    // Update staging buffer data.
    char* data;
    D3D12_RANGE range{idx * structure_size, (idx + 1) * structure_size};
    staging_buffer->Map(0, &range, (void**)&data);
    memcpy(data + idx * structure_size, &camera.camera_data, sizeof(camera.camera_data));
    staging_buffer->Unmap(0, &range);

    // Copy current camera to prev buffer.
    {
        D3D12_RESOURCE_BARRIER transitions[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(camera.camera_buffer.Get(),
                                                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                                                 D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(camera.prev_camera_buffer.Get(),
                                                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                                                 D3D12_RESOURCE_STATE_COPY_DEST)};

        upload_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    // Copy old camera.
    upload_command_list_->CopyBufferRegion(
        camera.prev_camera_buffer.Get(), 0, camera.camera_buffer.Get(), 0, sizeof(CameraData));

    // Resource transition.
    {
        D3D12_RESOURCE_BARRIER transitions[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                camera.camera_buffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(camera.prev_camera_buffer.Get(),
                                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)};

        upload_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    // Copy staging to constant.
    upload_command_list_->CopyBufferRegion(
        camera.camera_buffer.Get(), 0, staging_buffer, idx * structure_size, sizeof(CameraData));

    // Resource transition.
    {
        D3D12_RESOURCE_BARRIER transition =
            CD3DX12_RESOURCE_BARRIER::Transition(camera.camera_buffer.Get(),
                                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

        upload_command_list_->ResourceBarrier(1, &transition);
    }

    upload_command_list_->Close();
    render_system.PushCommandList(upload_command_list_);
}
}  // namespace capsaicin