#include "gui_system.h"

#include "capsaicin.h"
#include "src/common.h"
#include "src/systems/render_system.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_dx12.h"
#include "third_party/imgui/imgui_impl_win32.h"

namespace capsaicin
{
GUISystem::GUISystem(HWND hwnd)
{
    // Add settings components.
    world().CreateEntity().AddComponent<SettingsComponent>().Build();

    auto& render_system = world().GetSystem<RenderSystem>();

    // Setup Dear ImGui context.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Dear ImGui style.
    ImGui::StyleColorsDark();

    // Create descriptor heap for ImGUI.
    imgui_descriptor_heap_ = dx12api().CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(dx12api().device(),
                        RenderSystem::num_gpu_frames_in_flight(),
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        imgui_descriptor_heap_.Get(),
                        imgui_descriptor_heap_->GetCPUDescriptorHandleForHeapStart(),
                        imgui_descriptor_heap_->GetGPUDescriptorHandleForHeapStart());

    // Create command list.
    auto command_allocator = render_system.current_frame_command_allocator();
    gui_command_list_      = dx12api().CreateCommandList(command_allocator);
    gui_command_list_->Close();
}

GUISystem::~GUISystem()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void GUISystem::RenderGUI(SettingsComponent& settings)
{
    ImGui::Begin("Frame statistics");
    ImGui::SetWindowSize(ImVec2(280.f, 500.f));
    ImGui::SetWindowPos(ImVec2(20.f, 20.f));

    // ImGui::Text("This is some useful text.");  // Display some text (you can use a format strings
    // too)

    // ImGui::SliderFloat("float", &f, 0.0f, 1.0f);  // Edit 1 float using a slider from 0.0f
    // to 1.0f

    // if (ImGui::Button("Button"))  // Buttons return true when clicked (most widgets return true
    // when edited/activated)
    //    counter++;
    // ImGui::SameLine();
    // ImGui::Text("counter = %d", counter);

    const char* outputs[] = {"Combined", "Direct", "Indirect", "Variance"};

    ImGui::Checkbox("Vsync", &settings.vsync);
    ImGui::Separator();
    ImGui::SliderInt("Diffuse bounces", &settings.num_diffuse_bounces, 0, 5);
    ImGui::Separator();
    ImGui::Checkbox("Enable SVGF", &settings.denoise);
    ImGui::SliderFloat("Normal sigma", &settings.normal_sigma, 32.f, 256.f);
    ImGui::SliderFloat("Depth sigma", &settings.depth_sigma, 0.1f, 10.f);
    ImGui::SliderFloat("Luminance sigma", &settings.luma_sigma, 0.1f, 5.f);
    ImGui::Separator();
    ImGui::Checkbox("Enable spatial gather", &settings.gather);
    ImGui::Separator();
    ImGui::SliderFloat("TU feedback", &settings.temporal_upscale_feedback, 0.f, 1.f);
    ImGui::SliderFloat("TAA feedback", &settings.taa_feedback, 0.f, 1.f);
    ImGui::Separator();
    ImGui::Combo("Output", &settings.output, outputs, ARRAYSIZE(outputs), 5);
    ImGui::Separator();

    auto& render_system = world().GetSystem<RenderSystem>();
    auto& gpu_timings   = render_system.gpu_timings();

    for (auto& p : gpu_timings)
    {
        ImGui::Text(std::string(p.first + ": %f").c_str(), p.second * 1000);
    }

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);
    ImGui::End();
}

void GUISystem::Run(ComponentAccess& access, EntityQuery& entity_query, tf::Subflow& subflow)
{
    // Get settings.
    auto& settings = access.Write<SettingsComponent>()[0];

    auto& render_system     = world().GetSystem<RenderSystem>();
    auto  command_allocator = render_system.current_frame_command_allocator();

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    gui_command_list_->Reset(command_allocator, nullptr);

    RenderGUI(settings);

    auto rtv_handle = render_system.current_frame_output_descriptor_handle();
    auto backbuffer = render_system.current_frame_output();

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(
                backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
        };

        gui_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    ID3D12DescriptorHeap* descriptor_heaps[] = {imgui_descriptor_heap_.Get()};

    gui_command_list_->SetDescriptorHeaps(ARRAYSIZE(descriptor_heaps), descriptor_heaps);
    gui_command_list_->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);

    ImGui::Render();

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gui_command_list_.Get());

    // Resource transitions.
    {
        D3D12_RESOURCE_BARRIER transitions[] = {
            // Backbuffer transition to render target.
            CD3DX12_RESOURCE_BARRIER::Transition(
                backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)};

        gui_command_list_->ResourceBarrier(ARRAYSIZE(transitions), transitions);
    }

    gui_command_list_->Close();

    render_system.PushCommandList(gui_command_list_);
}
}  // namespace capsaicin