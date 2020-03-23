find_package(spdlog REQUIRED)

add_library(core src/capsaicin.cpp
                 src/dx12/dx12.cpp
                 src/dx12/shader_compiler.cpp)

target_include_directories(core
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${PROJECT_SOURCE_DIR}/third_party/dxc
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

target_link_libraries(core PRIVATE project_options project_warnings spdlog::spdlog yecs-lib)