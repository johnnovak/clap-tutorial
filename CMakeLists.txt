cmake_minimum_required(VERSION 3.29)

project(ClapTutorial
    VERSION     1.0
    DESCRIPTION "CLAP Tutorial"
    LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules/")


if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # TODO

else ()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif ()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# TODO
#configure_file(config.h.in config.h)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # TODO

elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_library(ClapTutorial MODULE src/plugin.cpp src/my_plugin.cpp)

    set_target_properties(ClapTutorial PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION clap

        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ClapTutorial.clap

        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.plist.in

        MACOSX_BUNDLE_GUI_IDENTIFIER        org.nakst.clap-tutorial.HelloClap
        MACOSX_BUNDLE_BUNDLE_NAME           "HelloClap"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING   "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT             nakst
    )

    file(TOUCH_NOCREATE ${CMAKE_BINARY_DIR}/ClapTutorial.clap/Contents/PkgInfo)
    file(WRITE          ${CMAKE_BINARY_DIR}/ClapTutorial.clap/Contents/PkgInfo "BNDL????")

    add_custom_command(TARGET ClapTutorial POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/ClapTutorial.clap to ~/Library/Audio/Plug-Ins/CLAP/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory  "${CMAKE_BINARY_DIR}/ClapTutorial.clap" "~/Library/Audio/Plug-Ins/CLAP/ClapTutorial.clap"
    )

elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # TODO

endif ()


find_package(SpeexDSP REQUIRED)

target_link_libraries(ClapTutorial PRIVATE Speex::SpeexDSP)

