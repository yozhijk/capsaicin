#pragma once

#include <cstdint>
#include <string>

// Backend specific stuff
#ifdef WIN32
#define NOMINMAX
#include <Windows.h>
struct RenderSessionParams
{
    HWND hwnd;
};

struct Input
{
    UINT message;
    LPARAM lparam;
    WPARAM wparam;
};
#endif

using std::uint32_t;

namespace capsaicin
{
void Init();
void InitRenderSession(void* params);
void LoadSceneFromOBJ(const std::string& file_name);
void ProcessInput(void* input);
void Update(float time_ms);
void Render();
void SetOption();
void ShutdownRenderSession();
void Shutdown();
}  // namespace capsaicin