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
    src/overlay_input.cpp
    src/quit_prompt.cpp
    src/achievements.cpp
    src/unlock_sound.cpp
    src/widescreen.cpp
    src/depth_bias_fix.cpp
    src/lod_bias_fix.cpp
    src/dlc_install.cpp
    src/language.cpp
    src/frame_stats.cpp
    src/xma_loop_fix.cpp
    src/timer_resolution.cpp
    src/thread_stats.cpp
    src/gpu_choice.cpp
)

if(WIN32)
    add_executable(rr6_recomp WIN32 ${RR6RECOMP_SOURCES})
else()
    add_executable(rr6_recomp ${RR6RECOMP_SOURCES})
endif()

if(APPLE)
    set_target_properties(rr6_recomp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Ridge Racer 6"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.rr6recomp.game"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
    )
endif()

# The Xenos GPU plugin is loaded at runtime (--gpu_plugin=xenos); stage it next to the exe.
rexglue_setup_target(rr6_recomp GPU_PLUGINS xenos)

# SDL resolves extra controller mappings at runtime. Keep the database beside
# the game, including in native macOS builds.
add_custom_command(TARGET rr6_recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/gamecontrollerdb.txt"
        "$<TARGET_FILE_DIR:rr6_recomp>/gamecontrollerdb.txt"
    VERBATIM
)

# The Windows multimedia library: the achievement sound (kept next to the
# program as sounds/achievement.wav everywhere) and the 1 ms timer. DXGI and
# Direct3D 12: choosing the graphics adapter (src/gpu_choice.cpp).
if(WIN32)
    target_link_libraries(rr6_recomp PRIVATE winmm dxgi d3d12)
endif()
add_custom_command(TARGET rr6_recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rr6_recomp>/sounds"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/achievement.wav"
            "$<TARGET_FILE_DIR:rr6_recomp>/sounds/achievement.wav")

# Linker map: lets a crash address inside rr6_recomp.exe be traced back to the
# recompiled guest function (sub_XXXXXXXX) it belongs to.
if(WIN32)
    target_link_options(rr6_recomp PRIVATE "LINKER:/MAP")
endif()
