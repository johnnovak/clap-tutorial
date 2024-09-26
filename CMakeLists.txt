cmake_minimum_required(VERSION 3.29)

project(
	ClapTutorial
	DESCRIPTION "CLAP tutorial"
	VERSION 1.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(
	ClapTutorial SHARED
	src/plugin.cpp
	src/my_plugin.cpp
)


#configure_file(config.h.in config.h)

#add_executable(ClapTutorial tutorial.cpp)

#target_include_directories(ClapTutorial PUBLIC "${PROJECT_BINARY_DIR}")

