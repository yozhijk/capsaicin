#pragma once

#include <cstdint>
#include <string>

using std::uint32_t;

namespace capsaicin
{
struct InputState
{
    struct
    {
        bool fwd = false;
        bool back = false;
        bool left = false;
        bool right = false;
        bool up = false;
        bool down = false;
    } keys;

    struct
    {
        bool tracking = false;
        float delta_x = 0.f;
        float delta_y = 0.f;
    } mouse;
};

void Init();
void LoadSceneFromOBJ(const std::string& file_name);
void SetSurface(uint32_t width, uint32_t height);
void SetInputState(const InputState& input);
void Update(float time_ms);
void Render();
void SetOption();
void Shutdown();
}  // namespace capsaicin