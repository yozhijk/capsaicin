find_package(spdlog REQUIRED)

add_library(core src/capsaicin.cpp)

target_include_directories(core
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(core PRIVATE project_options project_warnings spdlog::spdlog)