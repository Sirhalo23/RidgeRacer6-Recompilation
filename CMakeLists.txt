# rr6_recomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(rr6_recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(RR6RECOMP_SOURCES
    src/main.cpp
    src/dispatch_fixes.cpp
    src/host_thread_fixes.cpp
    src/fp_guard.cpp
    src/save_fixes.cpp
    src/input_fixes.cpp
    src/widescreen.cpp
    src/depth_bias_fix.cpp
)

if(WIN32)
    add_executable(rr6_recomp WIN32 ${RR6RECOMP_SOURCES})
else()
    add_executable(rr6_recomp ${RR6RECOMP_SOURCES})
endif()

# The Xenos GPU plugin is loaded at runtime (--gpu_plugin=xenos); stage it next to the exe.
rexglue_setup_target(rr6_recomp GPU_PLUGINS xenos)

# Linker map: lets a crash address inside rr6_recomp.exe be traced back to the
# recompiled guest function (sub_XXXXXXXX) it belongs to.
if(WIN32)
    target_link_options(rr6_recomp PRIVATE "LINKER:/MAP")
endif()
