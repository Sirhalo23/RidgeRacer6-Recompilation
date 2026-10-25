# Pinned source-copy integration; no SDK or generated checkout edits.
if(NOT UNIX OR NOT TARGET rexgpu-xenos OR NOT TARGET rexruntime)
    message(FATAL_ERROR "RR6_NATIVE_GPU_PROGRESS_WAIT requires a POSIX SDK source build")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
get_target_property(native_progress_gpu_dir rexgpu-xenos SOURCE_DIR)
get_filename_component(native_progress_sdk_root "${native_progress_gpu_dir}/../.." ABSOLUTE)
set(native_progress_script "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_native_gpu_progress.py")
set(native_progress_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tools/native_gpu_progress_functions.json")
set(native_progress_output "${CMAKE_CURRENT_BINARY_DIR}/native_progress")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${native_progress_script}" "${native_progress_sdk_root}"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/default" "${native_progress_output}"
    RESULT_VARIABLE native_progress_result ERROR_VARIABLE native_progress_error)
if(NOT native_progress_result EQUAL 0)
    message(FATAL_ERROR "Native GPU progress integration refused: ${native_progress_error}")
endif()
include("${native_progress_output}/sources.cmake")
set(native_progress_original_generated)
get_target_property(native_progress_recomp_sources rr6_recomp_recomp SOURCES)
foreach(native_progress_copy IN LISTS native_progress_generated)
    get_filename_component(native_progress_filename "${native_progress_copy}" NAME)
    set(native_progress_original ${native_progress_recomp_sources})
    list(FILTER native_progress_original INCLUDE REGEX "(^|/)${native_progress_filename}$")
    list(LENGTH native_progress_original native_progress_count)
    if(NOT native_progress_count EQUAL 1)
        message(FATAL_ERROR "Cannot identify translated source ${native_progress_filename}")
    endif()
    list(REMOVE_ITEM native_progress_recomp_sources "${native_progress_original}")
    list(APPEND native_progress_recomp_sources "${native_progress_copy}")
    list(APPEND native_progress_original_generated "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/${native_progress_filename}")
    set_source_files_properties("${native_progress_copy}" PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
endforeach()
set_property(TARGET rr6_recomp_recomp PROPERTY SOURCES "${native_progress_recomp_sources}")
target_include_directories(rr6_recomp_recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
get_target_property(native_progress_gpu_sources rexgpu-xenos SOURCES)
set(native_progress_common ${native_progress_gpu_sources})
list(FILTER native_progress_common INCLUDE REGEX "(^|/)command_processor\\.cpp$")
list(FILTER native_progress_common EXCLUDE REGEX "(^|/)(vulkan|d3d12)/")
list(LENGTH native_progress_common native_progress_count)
if(NOT native_progress_count EQUAL 1)
    message(FATAL_ERROR "Cannot identify common SDK command processor")
endif()
list(REMOVE_ITEM native_progress_gpu_sources "${native_progress_common}")
list(APPEND native_progress_gpu_sources "${native_progress_output}/command_processor.cpp")
set_property(TARGET rexgpu-xenos PROPERTY SOURCES "${native_progress_gpu_sources}")
set_source_files_properties("${native_progress_output}/command_processor.cpp" TARGET_DIRECTORY rexgpu-xenos
    PROPERTIES INCLUDE_DIRECTORIES "${native_progress_sdk_root}/src/graphics")
target_include_directories(rexgpu-xenos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_sources(rexruntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/native_gpu_progress_bridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/native_gpu_progress_runtime.cpp")
target_include_directories(rexruntime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rexruntime PRIVATE recomp::native)
# Revalidate copies after codegen, before compiling translated game bodies.
# The common SDK copy is unchanged by codegen and is validated at configure.
add_custom_target(rr6_native_progress_sources
    COMMAND "${Python3_EXECUTABLE}" "${native_progress_script}" "${native_progress_sdk_root}"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/default" "${native_progress_output}"
    BYPRODUCTS ${native_progress_generated}
    VERBATIM)
add_dependencies(rr6_native_progress_sources rr6_recomp_codegen)
add_dependencies(rr6_recomp_recomp rr6_native_progress_sources)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${native_progress_script}"
    "${native_progress_manifest}" "${native_progress_sdk_root}/src/graphics/command_processor.cpp"
    ${native_progress_original_generated})
option(RR6_NATIVE_GPU_PROGRESS_TESTS "Test pinned original/native guest progress bodies" ON)
if(RR6_NATIVE_GPU_PROGRESS_TESTS)
    enable_testing()
    add_executable(rr6_native_gpu_progress_guest_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/native/tests/native_gpu_progress_guest_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/native_gpu_progress_bridge.cpp"
        "${native_progress_output}/gpu_progress_fixture.cpp")
    target_include_directories(rr6_native_gpu_progress_guest_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/native/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")
    target_link_libraries(rr6_native_gpu_progress_guest_tests PRIVATE rex::runtime recomp::native)
    add_dependencies(rr6_native_gpu_progress_guest_tests rr6_native_progress_sources)
    add_test(NAME rr6_native_gpu_progress_guest COMMAND rr6_native_gpu_progress_guest_tests)
    set_tests_properties(rr6_native_gpu_progress_guest PROPERTIES TIMEOUT 30)
    add_test(NAME rr6_native_gpu_progress_seam
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_prepare_native_gpu_progress.py")
    set_tests_properties(rr6_native_gpu_progress_seam PROPERTIES
        ENVIRONMENT "RR6_NATIVE_TEST_SDK=${native_progress_sdk_root}")
endif()
