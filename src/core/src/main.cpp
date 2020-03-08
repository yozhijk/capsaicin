#include <iostream>

#include "capsaicin.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

using namespace std;

int foo()
{
    spdlog::info("core::foo() called");
    return 42;
}