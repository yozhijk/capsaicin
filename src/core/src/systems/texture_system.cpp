#include "texture_system.h"

#include "src/systems/render_system.h"

#define STB_IMAGE_IMPLEMENTATION
#include "src/utils/stb_image.h"

namespace capsaicin
{
ComPtr<ID3D12Resource> TextureSystem::GetTexture(const std::string& name)
{
    auto it = cache_.find(name);
    if (it == cache_.cend())
    {
        return GetTexture(LoadTexture(name));
    }

    return GetTexture(it->second);
}

ComPtr<ID3D12Resource> TextureSystem::GetTexture(uint32_t index){ return textures_[index]; }

uint32_t TextureSystem::GetTextureIndex(const std::string& name)
{
    auto it = cache_.find(name);
    if (it == cache_.cend())
    {
        auto index = LoadTexture(name);
        return index;
    }

    return it->second;
}

uint32_t TextureSystem::LoadTexture(const std::string& name)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto full_name = std::string("../../../assets/textures/") + name;

    int res_x, res_y;
    int channels;
    auto* data = stbi_load(full_name.c_str(), &res_x, &res_y, &channels, 4);

    // Create texture in default heap.
    CD3DX12_RESOURCE_DESC texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UINT, res_x, res_y);
    auto texture = dx12api().CreateResource(
        texture_desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_SUBRESOURCE_FOOTPRINT pitched_desc = {};
    pitched_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    pitched_desc.Width = res_x;
    pitched_desc.Height = res_y;
    pitched_desc.Depth = 1;
    pitched_desc.RowPitch = align(res_x * sizeof(DWORD), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

    auto upload_buffer = dx12api().CreateUploadBuffer(pitched_desc.Width * pitched_desc.RowPitch);
    render_system.AddAutoreleaseResource(upload_buffer);

    char* mapped_data = nullptr;
    upload_buffer->Map(0, nullptr, (void**)&mapped_data);
    for (auto row = 0; row < res_y; ++row)
    {
        memcpy(mapped_data, data, res_x * sizeof(DWORD));
        mapped_data += pitched_desc.RowPitch;
        data += res_x * sizeof(DWORD);
    }
    upload_buffer->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION src_texture_loc;
    src_texture_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_texture_loc.PlacedFootprint.Offset = 0;
    src_texture_loc.PlacedFootprint.Footprint = pitched_desc;
    src_texture_loc.pResource = upload_buffer.Get();

    D3D12_TEXTURE_COPY_LOCATION dst_texture_loc;
    dst_texture_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_texture_loc.pResource = texture.Get();
    dst_texture_loc.SubresourceIndex = 0;

    D3D12_BOX copy_box{0, 0, 0, res_x, res_y, 1};

    auto command_allocator = render_system.current_frame_command_allocator();
    auto command_list = dx12api().CreateCommandList(command_allocator);

    command_list->CopyTextureRegion(&dst_texture_loc, 0, 0, 0, &src_texture_loc, &copy_box);
    D3D12_RESOURCE_BARRIER transitions[] = {
        // Backbuffer transition to render target.
        CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
    command_list->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    command_list->Close();

    render_system.PushCommandList(command_list.Get());

    textures_.push_back(texture);

    return textures_.size() - 1;
}
}  // namespace capsaicin