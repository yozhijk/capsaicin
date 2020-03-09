#include "capsaicin.h"

#include <iostream>

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

using namespace std;
using namespace spdlog;

namespace capsaicin
{
void Init() { info("capsaicin::Init()"); }
void LoadSceneFromOBJ(const std::string& file_name) { info("capsaicin::LoadSceneFromOBJ({})", file_name); }
void SetSurface(uint32_t width, uint32_t height) { info("capsaicin::SetSurface({}, {})", width, height); }
void SetInputState(const InputState&) { info("capsaicin::SetInputState()"); }
void Update(float time_ms) { info("capsaicin::Update({})", time_ms); }
void Render() { info("capsaicin::Render()"); }
void SetOption() { info("capsaicin::SetOption()"); }
void Shutdown() { info("capsaicin::Shutdown()"); }
}  // namespace capsaicin