#!/usr/bin/env bash

g++ -shared -g -Wall -Wextra --std=c++20 -Wno-unused-parameter -o ~/Library/Audio/Plug-Ins/CLAP/HelloCLAP.clap plugin.cpp
