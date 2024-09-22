#!/usr/bin/env bash

g++ -shared -g -Wall -Wextra --std=c++20 -Wno-unused-parameter -o ~/.clap/HelloCLAP.clap plugin.cpp
