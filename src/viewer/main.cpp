#include <windows.h>

#include <cassert>
#include <iostream>
#include <type_traits>
#include <vector>

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
    case WM_ACTIVATEAPP:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_INPUT:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEHOVER:
    {
        Input input{msg, lp, wp};
        ProcessInput(&input);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(window, msg, wp, lp);
    }
}

int main()
{
    constexpr const char* kWindowClassName = "Viewer";
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
                                   "Viewer test",
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
            //LoadSceneFromOBJ("../../../assets/sponza.obj");
            LoadSceneFromOBJ("../../../assets/ScifiEnv.obj");

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