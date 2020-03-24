#include "capsaicin.h"

#include "src/common.h"
#include "src/dx12/shader_compiler.h"
#include "systems/asset_load_system.h"
#include "systems/blas_system.h"
#include "systems/render_system.h"
#include "systems/tlas_system.h"
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

    world().RegisterSystem<AssetLoadSystem>();
    world().RegisterSystem<BLASSystem>();
    world().RegisterSystem<TLASSystem>();
    world().Precede<AssetLoadSystem, BLASSystem>();
    world().Precede<BLASSystem, TLASSystem>();
}

void InitRenderSession(void* data)
{
    auto params = reinterpret_cast<RenderSessionParams*>(data);
    info("capsaicin::InitRenderSession()");
    world().RegisterSystem<RenderSystem>(params->hwnd);
    world().Precede<TLASSystem, RenderSystem>();
}

void LoadSceneFromOBJ(const std::string& file_name)
{
    info("capsaicin::LoadSceneFromOBJ({})", file_name);

    auto entity = world().CreateEntity().AddComponent<AssetComponent>().Build();
    auto& asset = world().GetComponent<AssetComponent>(entity);

    asset.file_name = file_name;
}

void SetInputState(const InputState&) { info("capsaicin::SetInputState()"); }
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