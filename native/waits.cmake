# Automatically use the validated audio path only on macOS ARM64 source builds
# with the reviewed SDK. Other platforms/revisions keep their existing route.
set(rr6_audio_default OFF)
if(APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" AND REXSDK_DIR
        AND TARGET rexcore AND TARGET rexaudio AND TARGET rexruntime)
    file(SHA256 "${REXSDK_DIR}/src/core/threading_posix.cpp" rr6_threading_digest)
    file(SHA256 "${REXSDK_DIR}/src/audio/audio_system.cpp" rr6_audio_digest)
    if(rr6_threading_digest STREQUAL "f60dfd4dc854f61c989bb68ba9dbc3c4cc6d3f58a6f2f00a19b2019538447fa9"
            AND rr6_audio_digest STREQUAL "7e129af6ea359947d4cd148d59f92c26984666c422e8d616b80fda005ffe7a07")
        set(rr6_audio_default ON)
    endif()
endif()
option(RR6_NATIVE_AUDIO_WAIT "Use reviewed native POSIX audio waits" ${rr6_audio_default})
option(RR6_NATIVE_GPU_PROGRESS_WAIT "Build experimental native GPU progress waits" OFF)
message(STATUS "RR6 native audio waits: ${RR6_NATIVE_AUDIO_WAIT}; GPU progress waits: ${RR6_NATIVE_GPU_PROGRESS_WAIT}")
if(RR6_NATIVE_AUDIO_WAIT OR RR6_NATIVE_GPU_PROGRESS_WAIT OR RR6_NATIVE_TESTS)
    add_subdirectory(libs/recomp_native)
    add_subdirectory(native)
endif()
if(RR6_NATIVE_AUDIO_WAIT)
    include("${CMAKE_CURRENT_LIST_DIR}/audio_wait.cmake")
endif()
