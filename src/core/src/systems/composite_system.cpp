#include "composite_system.h"

#include "src/common.h"
#include "src/systems/render_system.h"
#include "src/systems/raytracing_system.h"

namespace capsaicin
{
namespace
{
namespace RootSignature
{
enum
{
    kConstants = 0,
    kRaytracedTexture,
    kNumEntries
};
}

// Root constants for raster blit.
struct Constants
{
    uint32_t width;
    uint32_t height;
    float rotation;
    uint32_t padding;
};
}  // namespace

CompositeSystem::CompositeSystem()
{
    info("ComposisteSystem: Initializing");

    auto command_allocator = world().GetSystem<RenderSystem>().current_frame_command_allocator();

    // Create command list for raster blit.
    command_list_ = dx12api().CreateCommandList(command_allocator);
    command_list_->Close();

    // Initialize raster pipeline.
    InitPipeline();
}

CompositeSystem::~CompositeSystem() = default;

void CompositeSystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    auto output_srv_index = PopulateDescriptorTable();

    Render(0.f, output_srv_index);
}

void CompositeSystem::InitPipeline()
{
    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER root_entries[RootSignature::kNumEntries] = {};
    root_entries[RootSignature::kConstants].InitAsConstants(sizeof(Constants), 0);
    root_entries[RootSignature::kRaytracedTexture].InitAsDescriptorTable(1, &range);

    CD3DX12_ROOT_SIGNATURE_DESC desc = {};
    desc.Init(RootSignature::kNumEntries, root_entries);
    root_signature_ = dx12api().CreateRootSignature(desc);

    ShaderCompiler& shader_compiler{ShaderCompiler::instance()};
    auto vertex_shader = shader_compiler.CompileFromFile("../../../src/core/shaders/simple.hlsl", "vs_6_0", "VsMain");
    auto pixel_shader = shader_compiler.CompileFromFile("../../../src/core/shaders/simple.hlsl", "ps_6_0", "PsMain");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    pso_desc.InputLayout = {nullptr, 0};
    pso_desc.pRootSignature = root_signature_.Get();
    pso_desc.VS = vertex_shader;
    pso_desc.PS = pixel_shader;
    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;

    pipeline_state_ = dx12api().CreatePipelineState(pso_desc);
}

uint32_t CompositeSystem::PopulateDescriptorTable()
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto& raytracing_system = world().GetSystem<RaytracingSystem>();
    auto base_index = render_system.AllocateDescriptorRange(1);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.PlaneSlice = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dx12api().device()->CreateShaderResourceView(
        raytracing_system.current_frame_output(), &srv_desc, render_system.GetDescriptorHandleCPU(base_index));

    return base_index;
}

void CompositeSystem::Render(float time, uint32_t output_srv_index)
{
    auto& render_system = world().GetSystem<RenderSystem>();
    auto& raytracing_system = world().GetSystem<RaytracingSystem>();

    auto window_width = render_system.window_width();
    auto window_height = render_system.window_height();

    Constants constants{window_width, window_height, time, 0};

    auto command_allocator = world().GetSystem<RenderSystem>().current_frame_command_allocator();

    ID3D12GraphicsCommandList* command_list = command_list_.Get();

    command_list->Reset(command_allocator, nullptr);

    auto rtv_handle = render_system.current_frame_output_descriptor_handle();
    auto backbuffer = render_system.current_frame_output();
    auto raytracing_output = raytracing_system.current_frame_output();

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(backbuffer,
                                                 D3D12_RESOURCE_STATE_PRESENT,
                                                 D3D12_RESOURCE_STATE_RENDER_TARGET),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(raytracing_output,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)

        };

        command_list->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    ID3D12DescriptorHeap* descriptor_heaps[] = {render_system.current_frame_descriptor_heap()};

    command_list->SetGraphicsRootSignature(root_signature_.Get());
    command_list->SetDescriptorHeaps(ARRAYSIZE(descriptor_heaps), descriptor_heaps);
    command_list->SetGraphicsRoot32BitConstants(RootSignature::kConstants, sizeof(Constants) >> 2, &constants, 0);
    command_list->SetPipelineState(pipeline_state_.Get());
    command_list->SetGraphicsRootDescriptorTable(RootSignature::kRaytracedTexture,
                                                 render_system.GetDescriptorHandleGPU(output_srv_index));

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(window_width), static_cast<float>(window_height)};
    D3D12_RECT scissor_rect{0, 0, static_cast<LONG>(window_width), static_cast<LONG>(window_height)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor_rect);
    command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

    FLOAT shitty_red[] = {0.77f, 0.15f, 0.1f, 1.f};
    command_list->ClearRenderTargetView(rtv_handle, shitty_red, 0, nullptr);

    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(3, 1, 0, 0);

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[2] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(backbuffer,
                                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                 D3D12_RESOURCE_STATE_PRESENT),
            // Raytraced image transition UAV to SRV.
            CD3DX12_RESOURCE_BARRIER::Transition(raytracing_output,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};

        command_list->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    command_list->Close();
    render_system.PushCommandList(command_list);
}
}  // namespace capsaicin
