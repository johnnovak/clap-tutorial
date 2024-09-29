cmake_minimum_required(VERSION 3.29)

project(NukedSc55Clap
    VERSION     1.0
    DESCRIPTION "Nuked-SC55 CLAP plugin"
    LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules/")


if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # TODO

else ()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -O3)
endif ()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# TODO
#configure_file(config.h.in config.h)

add_library(NukedSc55Clap MODULE
	src/nuked-sc55/emu.cpp
	src/nuked-sc55/lcd.cpp
	src/nuked-sc55/mcu.cpp
	src/nuked-sc55/mcu_interrupt.cpp
	src/nuked-sc55/mcu_opcodes.cpp
	src/nuked-sc55/mcu_timer.cpp
	src/nuked-sc55/pcm.cpp
	src/nuked-sc55/submcu.cpp

	src/nuked_sc55.cpp
	src/plugin.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # TODO

elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	# TODO add a dedicated test target for this; for now, you'll need to
	# comment the rest of this branch out to compile the test

    set_target_properties(NukedSc55Clap PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION clap

        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/NukedSc55Clap.clap

        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.plist.in

        MACOSX_BUNDLE_GUI_IDENTIFIER        org.nakst.clap-tutorial.HelloClap
        MACOSX_BUNDLE_BUNDLE_NAME           "HelloClap"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING   "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT             nakst
    )

    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/NukedSc55Clap.clap/Contents/PkgInfo)
    file(WRITE          ${CMAKE_BINARY_DIR}/NukedSc55Clap.clap/Contents/PkgInfo "BNDL????")

    add_custom_command(TARGET NukedSc55Clap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/NukedSc55Clap.clap to ~/Library/Audio/Plug-Ins/CLAP/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/NukedSc55Clap.clap" "~/Library/Audio/Plug-Ins/CLAP/NukedSc55Clap.clap"
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # TODO

endif ()


find_package(SpeexDSP REQUIRED)

target_link_libraries(NukedSc55Clap  PRIVATE Speex::SpeexDSP)
