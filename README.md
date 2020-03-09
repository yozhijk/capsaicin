# capsaicin
Experimental game rendering engine fully based on ray tracing / path tracing.

## Build status
[![Build Status](https://travis-ci.org/yozhijk/capsaicin.svg?branch=master)](https://travis-ci.org/yozhijk/capsaicin)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/fb3240c085bb4916bbd27cd3b426c0ac)](https://www.codacy.com/manual/yozhijk/capsaicin?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=yozhijk/capsaicin&amp;utm_campaign=Badge_Grade)

## System requirements
### Windows

- CMake 13.2 or later
- Visual Studio 2019 or later
- DirectX 12
- vcpkg

### OSX

- Not supported yet

### Linux

- Not supported yet

## Build steps (Windows)

Build process relies on vcpkg on Windows to fulfill neccessary dependencies.

Clone and install vcpkg:

```sh
mkdir vcpkg
git clone --recursive https://github.com/microsoft/vcpkg.git vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
```

Install dependencies with vcpkg:

```sh
vcpkg install --triplet x64-windows spdlog
cd ..
```

Clone and build capsaicin:

```sh
git clone --recursive https://github.com/yozhijk/capsaicin.git capsaicin
mkdir build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
cd build
make -j4
```
## Usage

## Known issues
