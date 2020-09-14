add_executable(calc2_tests
    main.cpp
    tests.h
)

target_include_directories(calc2_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(calc2_tests PRIVATE
    project_options
    project_warnings
    spdlog::spdlog
    calc2
    gtest
)

