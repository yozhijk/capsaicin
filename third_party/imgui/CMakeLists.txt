add_library(imgui STATIC
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_internal.h
    imgui_widgets.cpp
    imgui.cpp
    imgui.h
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
    imgui_impl_dx12.cpp
    imgui_impl_dx12.h
    imgui_impl_win32.cpp
    imgui_impl_win32.h
)

target_compile_features(imgui PRIVATE cxx_std_17)
target_include_directories(imgui PUBLIC ${PROJECT_SOURCE_DIR})

if(WIN32)
    target_compile_options(imgui PRIVATE /WX)
elseif(UNIX)
    target_compile_options(imgui PRIVATE -Wall -Werror)
endif(WIN32)

