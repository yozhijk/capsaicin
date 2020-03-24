#include "capsaicin.h"

#include "src/common.h"
#include "src/dx12/shader_compiler.h"
#include "systems/render_system.h"
#include "utils/singleton.h"
#include "yecs/yecs.h"


namespace capsaicin
{
void Init() { info("capsaicin::Init()"); }
void InitRenderSession(void* data)
{
    auto params = reinterpret_cast<RenderSessionParams*>(data);
    info("capsaicin::InitRenderSession()");
    world().RegisterSystem<RenderSystem>(params->hwnd);
}
void LoadSceneFromOBJ(const std::string& file_name) { info("capsaicin::LoadSceneFromOBJ({})", file_name); }
void SetInputState(const InputState&) { info("capsaicin::SetInputState()"); }
void Update(float time_ms) { info("capsaicin::Update({})", time_ms); }
void Render()
{
    info("capsaicin::Render()");
    world().Run();
}
void SetOption() { info("capsaicin::SetOption()"); }
void ShutdownRenderSession() { info("capsaicin::ShutdownRenderSession()"); }
void Shutdown() { info("capsaicin::Shutdown()"); }
}  // namespace capsaicin