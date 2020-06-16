find_package(spdlog REQUIRED)

add_library(core src/capsaicin.cpp
                 src/dx12/dx12.cpp
                 src/dx12/shader_compiler.cpp
                 src/utils/singleton.h
                 src/utils/stb_image.h
                 src/utils/tri_box_test.h
                 src/utils/voxel.h
                 src/systems/render_system.h
                 src/systems/render_system.cpp
                 src/systems/composite_system.h
                 src/systems/composite_system.cpp
                 src/systems/raytracing_system.h
                 src/systems/raytracing_system.cpp
                 src/systems/blas_system.h
                 src/systems/blas_system.cpp
                 src/systems/tlas_system.h
                 src/systems/tlas_system.cpp
                 src/systems/asset_load_system.h
                 src/systems/asset_load_system.cpp
                 src/systems/camera_system.h
                 src/systems/camera_system.cpp
                 src/systems/input_system.h
                 src/systems/input_system.cpp
                 src/systems/texture_system.h
                 src/systems/texture_system.cpp
                 src/systems/gui_system.h
                 src/systems/gui_system.cpp
                 src/systems/voxel_system.h
                 src/systems/voxel_system.cpp
                 src/systems/voxel_visualizer_system.h
                 src/systems/voxel_visualizer_system.cpp
                 ${PROJECT_SOURCE_DIR}/third_party/directxtk/Mouse.cpp
                 ${PROJECT_SOURCE_DIR}/third_party/directxtk/Keyboard.cpp)

target_include_directories(core
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/dxc
        ${PROJECT_SOURCE_DIR}/third_party/directxtk
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Copy DXC dlls
add_custom_command(
        TARGET core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_SOURCE_DIR}/third_party/dxc/dxcompiler.dll
        "\$\(OutDir\)/dxcompiler.dll")
add_custom_command(
        TARGET core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_SOURCE_DIR}/third_party/dxc/dxil.dll
        "\$\(OutDir\)/dxil.dll")

target_link_libraries(core PRIVATE project_options project_warnings spdlog::spdlog yecs-lib d3d12 dxgi dxguid tinyobjloader imgui)