#include <cassert>
#include <iostream>
#include <type_traits>
#include <vector>

#include <windows.h>

#include "capsaicin.h"

using namespace std;
using namespace capsaicin;

LRESULT __stdcall WndProc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        Render();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(window, msg, wp, lp);
    }
}

int main()
{
    constexpr const char* kWindowClassName = "Capsaicin viewer";
    constexpr std::uint32_t kWindowWidth = 800;
    constexpr std::uint32_t kWindowHeight = 600;

    WNDCLASSEX window_class{};
    window_class.cbSize = sizeof(WNDCLASSEX);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WndProc;
    window_class.hInstance = GetModuleHandle(0);
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.lpszClassName = kWindowClassName;

    if (RegisterClassEx(&window_class))
    {
        RECT rect = {0, 0, static_cast<LONG>(kWindowWidth), static_cast<LONG>(kWindowHeight)};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        HWND hwnd = CreateWindowEx(0,
                                   kWindowClassName,
                                   "Capsaicin test",
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   rect.right - rect.left,
                                   rect.bottom - rect.top,
                                   0,
                                   0,
                                   GetModuleHandle(0),
                                   0);

        if (hwnd)
        {
            Init();

            RenderSessionParams params{hwnd};
            InitRenderSession(&params);
            LoadSceneFromOBJ("../../../assets/cornell_box.obj");

            ShowWindow(hwnd, SW_SHOWDEFAULT);

            MSG msg{};
            while (msg.message != WM_QUIT)
            {
                // Process any messages in the queue.
                if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }

            ShutdownRenderSession();
            Shutdown();
        }
    }
}