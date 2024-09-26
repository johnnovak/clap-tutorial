# CLAP plugin tutorial

This is an adaptation for [nakst's excellent CLAP plugin
tutorial](https://nakst.gitlab.io/tutorial/clap-part-1.html) for C++20, CMake
and vcpkg.


## Prerequisites

- vcpkg

### Install vcpkg

    git clone https://github.com/microsoft/vcpkg.git
    cd vcpkg && bootstrap-vcpkg.sh

    export VCPKG_ROOT=<vcpkg_repo_location>
    export PATH=$VCPKG_ROOT:$PATH

These instructions are for Linux and macOS; run `bootstrap-vcpkg.bat` on
Windows and set the `PATH` Windows enviroment variable accordingly.


## Building

Configure the project:

    cmake --preset=default

Build the project (output will be in the `build/` subdirectory):

    cmake --build build


To clean the `build` directory:

    cmake --build build --target clean


To nuke all local files on the `.gitignore` list:

    git clean -dfX


## Useful references

- [Official CMake Tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/index.html)
- [Microsofts's CMake & vcpks tutorial](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-cmd)
- [Tremus's CPLUG – C wrapper for VST3, AUv2, CLAP audio plugin formats](https://github.com/Tremus/CPLUG)
- [tobanteAudio's C++ CLAP examples](https://github.com/tobanteAudio/clap-examples/tree/main)


