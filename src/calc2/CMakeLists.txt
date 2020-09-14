find_package(spdlog REQUIRED)

add_library(calc2 src/calc2.cpp
                  src/dx12/device_dx12.h
                  src/dx12/device_dx12.cpp
                  src/dx12/buffer_dx12.h
                  src/dx12/buffer_dx12.cpp
                  src/dx12/image_dx12.h
                  src/dx12/image_dx12.cpp
                  src/dx12/program_dx12.h
                  src/dx12/program_dx12.cpp
                  src/dx12/command_allocator_dx12.h
                  src/dx12/command_allocator_dx12.cpp
                  src/dx12/command_buffer_dx12.h
                  src/dx12/command_buffer_dx12.cpp
                  src/dx12/command_batch_dx12.h
                  src/dx12/command_batch_dx12.cpp
                  src/dx12/fence_dx12.h
                  src/dx12/fence_dx12.cpp
                  src/dx12/dx12.h
                  src/dx12/dx12.cpp
                  src/dx12/shader_compiler.h
                  src/dx12/shader_compiler.cpp
                  src/dx12/format_conversion.h
                  include/calc2.h
                  include/common.h
                  include/device.h
                  include/buffer.h
                  include/image.h
                  include/program.h
                  include/command_buffer.h
                  include/command_allocator.h
                  include/fence.h)

target_include_directories(calc2
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/third_party/dxc
)

# Copy DXC dlls
add_custom_command(
        TARGET calc2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_SOURCE_DIR}/third_party/dxc/dxcompiler.dll
        "\$\(OutDir\)/dxcompiler.dll")
add_custom_command(
        TARGET calc2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${PROJECT_SOURCE_DIR}/third_party/dxc/dxil.dll
        "\$\(OutDir\)/dxil.dll")

target_link_libraries(calc2 PRIVATE project_options project_warnings spdlog::spdlog d3d12 dxgi dxguid)

if (ENABLE_TESTING)
        add_subdirectory(tests)
endif()