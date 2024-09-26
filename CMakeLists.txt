cmake_minimum_required(VERSION 3.29)

project(ClapTutorial
	DESCRIPTION "CLAP tutorial"
	VERSION 1.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	# TODO
else ()
	add_compile_options(
		-Wall
		-Wextra
		-Wno-unused-parameter
    )
endif ()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(ClapTutorial MODULE
	src/plugin.cpp
	src/my_plugin.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	# TODO

elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	# TODO

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# TODO

endif ()

#configure_file(config.h.in config.h)

#add_executable(ClapTutorial tutorial.cpp)

#target_include_directories(ClapTutorial PUBLIC "${PROJECT_BINARY_DIR}")

