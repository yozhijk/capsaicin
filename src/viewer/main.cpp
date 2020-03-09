#include <iostream>

#include "capsaicin.h"

using namespace std;
using namespace capsaicin;

int main()
{
    Init();
    std::cout << "Hello, world!\n";
    Shutdown();
    return 0;
}