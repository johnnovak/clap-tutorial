#!/usr/bin/env bash

COMPILE_FLAGS="-g -Wall -Wextra --std=c++20 -Wno-unused-parameter"

g++ -c $COMPILE_FLAGS -o my_plugin.o my_plugin.cpp
g++ -c $COMPILE_FLAGS -o plugin.o plugin.cpp

g++ -shared my_plugin.o plugin.o -o ~/Library/Audio/Plug-Ins/CLAP/HelloCLAP.clap
