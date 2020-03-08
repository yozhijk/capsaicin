add_executable(viewer main.cpp)
target_link_libraries(viewer PRIVATE project_options project_warnings)
target_link_libraries(viewer PRIVATE core)