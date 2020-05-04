#include "capsaicin.h"

#include "src/common.h"
#include "src/dx12/shader_compiler.h"
#include "systems/asset_load_system.h"
#include "systems/blas_system.h"
#include "systems/render_system.h"
#include "systems/composite_system.h"
#include "systems/gui_system.h"
#include "systems/raytracing_system.h"
#include "systems/tlas_system.h"
#include "systems/camera_system.h"
#include "systems/input_system.h"
#include "systems/texture_system.h"
#include "utils/singleton.h"
#include "yecs/yecs.h"


namespace capsaicin
{
void Init()
{
    info("capsaicin::Init()");

    world().RegisterComponent<AssetComponent>();
    world().RegisterComponent<MeshComponent>();
    world().RegisterComponent<BLASComponent>();
    world().RegisterComponent<TLASComponent>();
    world().RegisterComponent<CameraComponent>();
    world().RegisterComponent<SettingsComponent>();

    world().RegisterSystem<AssetLoadSystem>();
    world().RegisterSystem<BLASSystem>();
    world().RegisterSystem<TLASSystem>();
    world().RegisterSystem<CameraSystem>();
    world().RegisterSystem<InputSystem>();
    world().RegisterSystem<TextureSystem>();

    // TODO: enable parallel execution.
    // Currently each system is taking care of work submission, 
    // so sumbitting it in parallel would be dangerous.
    world().Precede<AssetLoadSystem, BLASSystem>();
    world().Precede<BLASSystem, TLASSystem>();
    world().Precede<TLASSystem, CameraSystem>();
    world().Precede<InputSystem, CameraSystem>();
    world().Precede<InputSystem, TextureSystem>();
}

void InitRenderSession(void* data)
{
    info("capsaicin::InitRenderSession()");

    auto params = reinterpret_cast<RenderSessionParams*>(data);
    world().RegisterSystem<RenderSystem>(params->hwnd);
    world().RegisterSystem<RaytracingSystem>();
    world().RegisterSystem<CompositeSystem>();
    world().RegisterSystem<GUISystem>(params->hwnd);

    world().Precede<TextureSystem, CameraSystem>();
    world().Precede<CameraSystem, RaytracingSystem>();
    world().Precede<RaytracingSystem, CompositeSystem>();
    world().Precede<CompositeSystem, GUISystem>();
    world().Precede<GUISystem, RenderSystem>();
}

void LoadSceneFromOBJ(const std::string& file_name)
{
    info("capsaicin::LoadSceneFromOBJ({})", file_name);

    auto entity = world().CreateEntity().AddComponent<AssetComponent>().Build();
    auto& asset = world().GetComponent<AssetComponent>(entity);

    asset.file_name = file_name;
}

void ProcessInput(void* input)
{
    // info("capsaicin::ProcessInput()");
    world().GetSystem<InputSystem>().ProcessInput(input);
}

void Update(float time_ms) { info("capsaicin::Update({})", time_ms); }
void Render() { world().Run(); }
void SetOption() { info("capsaicin::SetOption()"); }

void ShutdownRenderSession()
{
    info("capsaicin::ShutdownRenderSession()");
    world().Reset();
}

void Shutdown() { info("capsaicin::Shutdown()"); }
}  // namespace capsaicin