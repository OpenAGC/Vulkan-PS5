cmake_minimum_required(VERSION 3.20)
project(VulkanPS5 VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
if(PROSPERO)
  set(OPENAGC_PSBC_RUNTIME_LIBRARIES "unwind;c++abi;c++;Threads::Threads;m")
elseif(APPLE)
  set(OPENAGC_PSBC_RUNTIME_LIBRARIES "c++;Threads::Threads;m")
else()
  set(OPENAGC_PSBC_RUNTIME_LIBRARIES "stdc++;Threads::Threads;m")
endif()

set(VULKAN_HEADERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Vulkan-Headers" CACHE PATH
    "Path to a Vulkan-Headers checkout")
set(OPENAGC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../OpenAGC" CACHE PATH
    "Path to an OpenAGC checkout (required by Prospero consumers)")
set(OPENAGC_PSBC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../openagc-psbc" CACHE PATH
    "Path to openagc-psbc")
if(CMAKE_CROSSCOMPILING)
  set(_OPENAGC_PSBC_DEFAULT_LIBRARY
    "${OPENAGC_PSBC_ROOT}/libopenagc_psbc.prospero.a")
else()
  set(_OPENAGC_PSBC_DEFAULT_LIBRARY "${OPENAGC_PSBC_ROOT}/libopenagc_psbc.a")
endif()
set(OPENAGC_PSBC_LIBRARY "${_OPENAGC_PSBC_DEFAULT_LIBRARY}" CACHE FILEPATH
    "Path to the openagc-psbc runtime compiler archive")
set(VULKAN_MEMORY_ALLOCATOR_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../VulkanMemoryAllocator" CACHE PATH
    "Path to VulkanMemoryAllocator (used by the Eden compatibility test only)")
option(VULKAN_PS5_BUILD_SHARED "Build the loader-compatible host ICD" ON)
option(VULKAN_PS5_BUILD_TESTS "Build host lifecycle tests" ON)
option(VULKAN_PS5_BUILD_EXAMPLES "Build standalone Vulkan examples" ON)

if(NOT EXISTS "${VULKAN_HEADERS_ROOT}/include/vulkan/vulkan_core.h")
  message(FATAL_ERROR "Vulkan-Headers not found; set VULKAN_HEADERS_ROOT")
endif()
if(NOT TARGET Vulkan::Headers)
  add_library(Vulkan::Headers INTERFACE IMPORTED)
  set_target_properties(Vulkan::Headers PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_HEADERS_ROOT}/include")
endif()

if(NOT TARGET OpenAGC::openagc)
  if(NOT EXISTS "${OPENAGC_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "OpenAGC not found; set OPENAGC_ROOT")
  endif()
  set(OPENAGC_BUILD_TESTS OFF CACHE BOOL "Build OpenAGC tests" FORCE)
  set(OPENAGC_BUILD_EXAMPLES OFF CACHE BOOL "Build OpenAGC examples" FORCE)
  set(OPENAGC_PACKAGE_PSBC OFF CACHE BOOL "Package openagc-psbc" FORCE)
  add_subdirectory("${OPENAGC_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/openagc"
    EXCLUDE_FROM_ALL)
endif()
if(NOT EXISTS "${OPENAGC_PSBC_ROOT}/libopenagc_psbc/openagc_psbc.h")
  message(FATAL_ERROR "openagc-psbc headers not found; set OPENAGC_PSBC_ROOT")
endif()
if(NOT EXISTS "${OPENAGC_PSBC_LIBRARY}")
  message(FATAL_ERROR
    "openagc-psbc library not found; build it or set OPENAGC_PSBC_LIBRARY")
endif()
add_library(OpenAgcPsbc::Compiler STATIC IMPORTED)
set_target_properties(OpenAgcPsbc::Compiler PROPERTIES
  IMPORTED_LOCATION "${OPENAGC_PSBC_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${OPENAGC_PSBC_ROOT}/libopenagc_psbc"
  INTERFACE_LINK_LIBRARIES "${OPENAGC_PSBC_RUNTIME_LIBRARIES}")

add_library(vulkan_ps5_objects OBJECT
  src/vulkan_ps5.c
  src/vulkan_ps5_core.c
  src/vulkan_ps5_wsi.c)
set_target_properties(vulkan_ps5_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(vulkan_ps5_objects PRIVATE c_std_11)
target_include_directories(vulkan_ps5_objects
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${VULKAN_HEADERS_ROOT}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(vulkan_ps5_objects PRIVATE
  OpenAGC::openagc OpenAgcPsbc::Compiler)

add_library(vulkan_ps5_static STATIC $<TARGET_OBJECTS:vulkan_ps5_objects>)
add_library(VulkanPS5::ICD ALIAS vulkan_ps5_static)
set_target_properties(vulkan_ps5_static PROPERTIES
  OUTPUT_NAME vulkan_ps5 EXPORT_NAME ICD)
target_include_directories(vulkan_ps5_static
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${VULKAN_HEADERS_ROOT}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(vulkan_ps5_static PUBLIC
  Vulkan::Headers OpenAGC::openagc OpenAgcPsbc::Compiler)

if(VULKAN_PS5_BUILD_SHARED)
  add_library(vulkan_ps5_icd SHARED $<TARGET_OBJECTS:vulkan_ps5_objects>)
  set_target_properties(vulkan_ps5_icd PROPERTIES OUTPUT_NAME vulkan_ps5)
  target_include_directories(vulkan_ps5_icd
    PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
      "$<BUILD_INTERFACE:${VULKAN_HEADERS_ROOT}/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
  target_link_libraries(vulkan_ps5_icd PRIVATE
    OpenAGC::openagc OpenAgcPsbc::Compiler)
  set(VULKAN_PS5_ICD_LIBRARY_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}vulkan_ps5${CMAKE_SHARED_LIBRARY_SUFFIX}")
  configure_file(vulkan_ps5_icd.json.in vulkan_ps5_icd.json @ONLY)
endif()

if(VULKAN_PS5_BUILD_TESTS)
  enable_testing()
  add_executable(vulkan_ps5_lifecycle_test tests/lifecycle.c)
  target_link_libraries(vulkan_ps5_lifecycle_test PRIVATE vulkan_ps5_static)
  add_test(NAME vulkan_ps5.lifecycle COMMAND vulkan_ps5_lifecycle_test)
  add_executable(vulkan_ps5_robustness_test tests/robustness.c)
  target_link_libraries(vulkan_ps5_robustness_test
    PRIVATE vulkan_ps5_static Threads::Threads)
  add_test(NAME vulkan_ps5.robustness COMMAND vulkan_ps5_robustness_test)
  add_executable(vulkan_ps5_wsi_test tests/wsi.c)
  target_link_libraries(vulkan_ps5_wsi_test
    PRIVATE vulkan_ps5_static Threads::Threads)
  add_test(NAME vulkan_ps5.wsi COMMAND vulkan_ps5_wsi_test)
  add_executable(vulkan_ps5_eden_profile_test tests/eden_profile.c)
  target_link_libraries(vulkan_ps5_eden_profile_test PRIVATE vulkan_ps5_static)
  add_test(NAME vulkan_ps5.eden_profile_report
    COMMAND vulkan_ps5_eden_profile_test)
  if(EXISTS "${VULKAN_MEMORY_ALLOCATOR_ROOT}/include/vk_mem_alloc.h")
    enable_language(CXX)
    add_executable(vulkan_ps5_eden_vma_test tests/eden_vma.cpp)
    target_compile_features(vulkan_ps5_eden_vma_test PRIVATE cxx_std_20)
    target_include_directories(vulkan_ps5_eden_vma_test PRIVATE
      "${VULKAN_MEMORY_ALLOCATOR_ROOT}/include")
    if(APPLE)
      target_compile_options(vulkan_ps5_eden_vma_test PRIVATE
        -Wno-nullability-completeness)
    endif()
    target_link_libraries(vulkan_ps5_eden_vma_test PRIVATE vulkan_ps5_static)
    add_test(NAME vulkan_ps5.eden_vma COMMAND vulkan_ps5_eden_vma_test)
  else()
    message(WARNING
      "VulkanMemoryAllocator not found; vulkan_ps5.eden_vma is disabled")
  endif()
  if(UNIX)
    add_test(NAME vulkan_ps5.swapchain_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/swapchain_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.swapchain_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.system_exit_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/system_exit_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.system_exit_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.mirror_clamp_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/mirror_clamp_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.mirror_clamp_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.vertex_divisor_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/vertex_divisor_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.vertex_divisor_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.sampler_anisotropy_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/sampler_anisotropy_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.sampler_anisotropy_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.indirect_draw_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/indirect_draw_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.indirect_draw_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.independent_blend_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/independent_blend_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.independent_blend_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.dual_src_blend_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/dual_src_blend_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.dual_src_blend_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.depth_bias_clamp_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/depth_bias_clamp_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.depth_bias_clamp_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.depth_clamp_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/depth_clamp_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.depth_clamp_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.logic_op_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/logic_op_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.logic_op_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.fill_mode_non_solid_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/fill_mode_non_solid_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.fill_mode_non_solid_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.large_points_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/large_points_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.large_points_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.wide_lines_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/wide_lines_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.wide_lines_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.shader_demote_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_demote_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.shader_demote_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.shader_clip_distance_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_clip_distance_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.shader_clip_distance_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.shader_cull_distance_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_cull_distance_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.shader_cull_distance_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.shader_image_gather_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_image_gather_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.shader_image_gather_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.fragment_stores_atomics_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/fragment_stores_atomics_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.fragment_stores_atomics_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.vertex_pipeline_stores_atomics_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/vertex_pipeline_stores_atomics_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.vertex_pipeline_stores_atomics_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.variable_pointers_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/variable_pointers_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.variable_pointers_runner PROPERTIES
      TIMEOUT 15)
    add_test(NAME vulkan_ps5.ps5debug_process_identity
      COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tests/ps5debug_kill_process_test.py")
    add_test(NAME vulkan_ps5.eden_vma_runner
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/eden_vma_runner.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(vulkan_ps5.eden_vma_runner PROPERTIES TIMEOUT 15)
    add_test(NAME vulkan_ps5.package_relocation
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/package_relocation.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
        "${VULKAN_HEADERS_ROOT}")
    set_tests_properties(vulkan_ps5.package_relocation PROPERTIES TIMEOUT 90)
  endif()
  find_program(GLSLANG_VALIDATOR glslangValidator)
  if(GLSLANG_VALIDATOR)
    set(PIPELINE_SHADER_OUTPUTS)
    foreach(shader pipeline.vert pipeline.frag pipeline.comp pipeline.geom
                   pipeline.tesc pipeline.tese)
      set(output "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv")
      add_custom_command(OUTPUT "${output}"
        COMMAND "${GLSLANG_VALIDATOR}" -V
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/${shader}" -o "${output}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${shader}"
        VERBATIM)
      list(APPEND PIPELINE_SHADER_OUTPUTS "${output}")
    endforeach()
    add_executable(vulkan_ps5_pipeline_test
      tests/pipeline.c ${PIPELINE_SHADER_OUTPUTS})
    target_link_libraries(vulkan_ps5_pipeline_test PRIVATE vulkan_ps5_static)
    add_test(NAME vulkan_ps5.pipeline COMMAND vulkan_ps5_pipeline_test
      ${PIPELINE_SHADER_OUTPUTS})
    set(COMMAND_SHADER_OUTPUTS)
    foreach(shader command.comp command.vert command.frag command.geom
                   command.tesc command.tese)
      set(output "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv")
      add_custom_command(OUTPUT "${output}"
        COMMAND "${GLSLANG_VALIDATOR}" -V
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/${shader}" -o "${output}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${shader}"
        VERBATIM)
      list(APPEND COMMAND_SHADER_OUTPUTS "${output}")
    endforeach()
    add_executable(vulkan_ps5_command_recording_test tests/command_recording.c)
    target_sources(vulkan_ps5_command_recording_test PRIVATE ${COMMAND_SHADER_OUTPUTS})
    target_link_libraries(vulkan_ps5_command_recording_test PRIVATE vulkan_ps5_static)
    add_test(NAME vulkan_ps5.command_recording
      COMMAND vulkan_ps5_command_recording_test
        ${COMMAND_SHADER_OUTPUTS})
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND AND VULKAN_PS5_BUILD_SHARED)
    add_test(NAME vulkan_ps5.entrypoints
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_entrypoints.py"
        "${VULKAN_HEADERS_ROOT}/registry/vk.xml"
        "$<TARGET_FILE:vulkan_ps5_icd>"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan_ps5.c")
  endif()
  find_library(VULKAN_LOADER_LIBRARY NAMES vulkan)
  if(VULKAN_LOADER_LIBRARY AND VULKAN_PS5_BUILD_SHARED)
    add_executable(vulkan_ps5_loader_test tests/loader.c)
    target_include_directories(vulkan_ps5_loader_test PRIVATE "${VULKAN_HEADERS_ROOT}/include")
    target_link_libraries(vulkan_ps5_loader_test PRIVATE "${VULKAN_LOADER_LIBRARY}")
    add_test(NAME vulkan_ps5.loader COMMAND vulkan_ps5_loader_test)
    set_tests_properties(vulkan_ps5.loader PROPERTIES
      ENVIRONMENT "VK_ICD_FILENAMES=${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_icd.json")
    find_library(VULKAN_VALIDATION_LAYER_LIBRARY NAMES VkLayer_khronos_validation)
    find_path(VULKAN_VALIDATION_LAYER_MANIFEST_DIR VkLayer_khronos_validation.json
      PATH_SUFFIXES share/vulkan/explicit_layer.d)
    if(VULKAN_VALIDATION_LAYER_LIBRARY AND VULKAN_VALIDATION_LAYER_MANIFEST_DIR)
      add_executable(vulkan_ps5_validation_test tests/validation.c)
      target_include_directories(vulkan_ps5_validation_test
        PRIVATE "${VULKAN_HEADERS_ROOT}/include")
      target_link_libraries(vulkan_ps5_validation_test PRIVATE "${VULKAN_LOADER_LIBRARY}")
      add_test(NAME vulkan_ps5.validation COMMAND vulkan_ps5_validation_test)
      get_filename_component(VULKAN_VALIDATION_LAYER_LIBRARY_DIR
        "${VULKAN_VALIDATION_LAYER_LIBRARY}" DIRECTORY)
      set_tests_properties(vulkan_ps5.validation PROPERTIES ENVIRONMENT
        "VK_ICD_FILENAMES=${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_icd.json;VK_LAYER_PATH=${VULKAN_VALIDATION_LAYER_MANIFEST_DIR};DYLD_LIBRARY_PATH=${VULKAN_VALIDATION_LAYER_LIBRARY_DIR}")
      if(EXISTS "${VULKAN_MEMORY_ALLOCATOR_ROOT}/include/vk_mem_alloc.h")
        add_executable(vulkan_ps5_eden_vma_validation_test tests/eden_vma.cpp)
        target_compile_features(vulkan_ps5_eden_vma_validation_test
          PRIVATE cxx_std_20)
        target_compile_definitions(vulkan_ps5_eden_vma_validation_test
          PRIVATE VULKAN_PS5_ENABLE_VALIDATION=1)
        target_include_directories(vulkan_ps5_eden_vma_validation_test PRIVATE
          "${VULKAN_HEADERS_ROOT}/include"
          "${VULKAN_MEMORY_ALLOCATOR_ROOT}/include")
        if(APPLE)
          target_compile_options(vulkan_ps5_eden_vma_validation_test PRIVATE
            -Wno-nullability-completeness)
        endif()
        target_link_libraries(vulkan_ps5_eden_vma_validation_test PRIVATE
          "${VULKAN_LOADER_LIBRARY}")
        add_test(NAME vulkan_ps5.eden_vma_validation
          COMMAND vulkan_ps5_eden_vma_validation_test)
        set_tests_properties(vulkan_ps5.eden_vma_validation PROPERTIES
          ENVIRONMENT
            "VK_ICD_FILENAMES=${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_icd.json;VK_LAYER_PATH=${VULKAN_VALIDATION_LAYER_MANIFEST_DIR};DYLD_LIBRARY_PATH=${VULKAN_VALIDATION_LAYER_LIBRARY_DIR}")
      endif()
    endif()
  endif()
endif()

if(VULKAN_PS5_BUILD_EXAMPLES)
  find_program(VULKAN_PS5_GLSLANG_VALIDATOR glslangValidator REQUIRED)
  find_program(VULKAN_PS5_SPIRV_AS spirv-as REQUIRED)
  find_program(VULKAN_PS5_SPIRV_VAL spirv-val REQUIRED)
  set(VULKAN_PS5_COMPUTE_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_compute_spv.h")
  set(VULKAN_PS5_STORAGE_IMAGE_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_storage_image_spv.h")
  set(VULKAN_PS5_ROBUST_BUFFER_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_robust_buffer_spv.h")
  set(VULKAN_PS5_VARIABLE_POINTERS_BINARY
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_variable_pointers.spv")
  set(VULKAN_PS5_VARIABLE_POINTERS_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_variable_pointers_spv.h")
  add_custom_command(OUTPUT "${VULKAN_PS5_COMPUTE_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_compute_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/compute.comp"
      -o "${VULKAN_PS5_COMPUTE_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/compute.comp"
    VERBATIM)
  add_executable(vulkan_ps5_compute_example
    examples/compute/main.c "${VULKAN_PS5_COMPUTE_HEADER}")
  add_custom_command(OUTPUT "${VULKAN_PS5_STORAGE_IMAGE_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
      --vn vulkan_ps5_storage_image_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/storage_image.comp"
      -o "${VULKAN_PS5_STORAGE_IMAGE_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/storage_image.comp"
    VERBATIM)
  add_executable(vulkan_ps5_storage_image_probe
    examples/compute/main.c "${VULKAN_PS5_STORAGE_IMAGE_HEADER}")
  target_compile_definitions(vulkan_ps5_storage_image_probe PRIVATE
    VULKAN_PS5_STORAGE_IMAGE_PROBE=1)
  add_custom_command(OUTPUT "${VULKAN_PS5_ROBUST_BUFFER_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
      --vn vulkan_ps5_robust_buffer_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/robust_buffer_access.comp"
      -o "${VULKAN_PS5_ROBUST_BUFFER_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/robust_buffer_access.comp"
    VERBATIM)
  add_executable(vulkan_ps5_robust_buffer_probe
    examples/compute/main.c "${VULKAN_PS5_ROBUST_BUFFER_HEADER}")
  target_compile_definitions(vulkan_ps5_robust_buffer_probe PRIVATE
    VULKAN_PS5_ROBUST_BUFFER_PROBE=1)
  if(VULKAN_PS5_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME vulkan_ps5.storage_image_command_recording
      COMMAND vulkan_ps5_storage_image_probe)
    add_test(NAME vulkan_ps5.robust_buffer_command_recording
      COMMAND vulkan_ps5_robust_buffer_probe)
    add_test(NAME vulkan_ps5.robust_vertex_command_recording
      COMMAND vulkan_ps5_robust_vertex_probe)
  endif()
  add_custom_command(
    OUTPUT "${VULKAN_PS5_VARIABLE_POINTERS_BINARY}"
      "${VULKAN_PS5_VARIABLE_POINTERS_HEADER}"
    COMMAND "${VULKAN_PS5_SPIRV_AS}" --target-env spv1.3
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/variable_pointers/variable_pointers.spvasm"
      -o "${VULKAN_PS5_VARIABLE_POINTERS_BINARY}"
    COMMAND "${VULKAN_PS5_SPIRV_VAL}" --target-env vulkan1.1
      "${VULKAN_PS5_VARIABLE_POINTERS_BINARY}"
    COMMAND "${CMAKE_COMMAND}"
      -DINPUT=${VULKAN_PS5_VARIABLE_POINTERS_BINARY}
      -DOUTPUT=${VULKAN_PS5_VARIABLE_POINTERS_HEADER}
      -DSYMBOL=vulkan_ps5_variable_pointers_spv
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/variable_pointers/variable_pointers.spvasm"
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spirv.cmake"
    VERBATIM)
  add_executable(vulkan_ps5_variable_pointers_probe
    examples/compute/main.c "${VULKAN_PS5_VARIABLE_POINTERS_HEADER}")
  target_compile_definitions(vulkan_ps5_variable_pointers_probe PRIVATE
    VULKAN_PS5_VARIABLE_POINTERS_PROBE=1)
  add_executable(vulkan_ps5_swapchain_example examples/swapchain/main.c)
  set(VULKAN_PS5_TRIANGLE_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_triangle_vert_spv.h")
  set(VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_triangle_frag_spv.h")
  set(VULKAN_PS5_ROBUST_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_robust_vertex_spv.h")
  set(VULKAN_PS5_ROBUST_VERTEX_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_robust_vertex_frag_spv.h")
  set(VULKAN_PS5_NON_SOLID_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_non_solid_vert_spv.h")
  set(VULKAN_PS5_NON_SOLID_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_non_solid_frag_spv.h")
  set(VULKAN_PS5_LARGE_POINTS_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_large_points_vert_spv.h")
  set(VULKAN_PS5_WIDE_LINES_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_wide_lines_vert_spv.h")
  set(VULKAN_PS5_DEMOTE_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_demote_vert_spv.h")
  set(VULKAN_PS5_DEMOTE_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_demote_frag_spv.h")
  set(VULKAN_PS5_CLIP_DISTANCE_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_clip_distance_vert_spv.h")
  set(VULKAN_PS5_CULL_DISTANCE_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_cull_distance_vert_spv.h")
  set(VULKAN_PS5_GEOMETRY_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_geometry_spv.h")
  set(VULKAN_PS5_TESS_CONTROL_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_tess_control_spv.h")
  set(VULKAN_PS5_TESS_EVALUATION_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_tess_evaluation_spv.h")
  set(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_vertex_pipeline_stores_atomics_vert_spv.h")
  set(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_CONTROL_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_vertex_pipeline_stores_atomics_tesc_spv.h")
  set(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_EVALUATION_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_vertex_pipeline_stores_atomics_tese_spv.h")
  set(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_GEOMETRY_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_vertex_pipeline_stores_atomics_geom_spv.h")
  add_custom_command(OUTPUT "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_triangle_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/triangle.vert"
      -o "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/triangle.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_triangle_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/triangle.frag"
      -o "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/triangle.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_ROBUST_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
      --vn vulkan_ps5_robust_vertex_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/robust_vertex.vert"
      -o "${VULKAN_PS5_ROBUST_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/robust_vertex.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_ROBUST_VERTEX_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
      --vn vulkan_ps5_robust_vertex_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/robust_vertex.frag"
      -o "${VULKAN_PS5_ROBUST_VERTEX_FRAGMENT_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/robust_vertex.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_NON_SOLID_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_non_solid_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/non_solid.vert"
      -o "${VULKAN_PS5_NON_SOLID_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/non_solid.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_NON_SOLID_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_non_solid_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/non_solid.frag"
      -o "${VULKAN_PS5_NON_SOLID_FRAGMENT_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/non_solid.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_LARGE_POINTS_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_large_points_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/large_points.vert"
      -o "${VULKAN_PS5_LARGE_POINTS_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/large_points.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_WIDE_LINES_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_wide_lines_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/wide_lines.vert"
      -o "${VULKAN_PS5_WIDE_LINES_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/wide_lines.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_DEMOTE_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_demote_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/demote.vert"
      -o "${VULKAN_PS5_DEMOTE_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/demote.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_DEMOTE_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_demote_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/demote.frag"
      -o "${VULKAN_PS5_DEMOTE_FRAGMENT_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/demote.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_CLIP_DISTANCE_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_clip_distance_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/clip_distance.vert"
      -o "${VULKAN_PS5_CLIP_DISTANCE_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/clip_distance.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_CULL_DISTANCE_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_cull_distance_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/cull_distance.vert"
      -o "${VULKAN_PS5_CULL_DISTANCE_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/triangle/cull_distance.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_GEOMETRY_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_geometry_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/geometry/geometry.geom"
      -o "${VULKAN_PS5_GEOMETRY_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/geometry/geometry.geom"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_TESS_CONTROL_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_tess_control_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/tessellation/tessellation.tesc"
      -o "${VULKAN_PS5_TESS_CONTROL_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/tessellation/tessellation.tesc"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_TESS_EVALUATION_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_tess_evaluation_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/tessellation/tessellation.tese"
      -o "${VULKAN_PS5_TESS_EVALUATION_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/tessellation/tessellation.tese"
    VERBATIM)
  add_custom_command(
    OUTPUT "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_vertex_pipeline_stores_atomics_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.vert"
      -o "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_VERTEX_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.vert"
    VERBATIM)
  add_custom_command(
    OUTPUT "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_CONTROL_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_vertex_pipeline_stores_atomics_tesc_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.tesc"
      -o "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_CONTROL_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.tesc"
    VERBATIM)
  add_custom_command(
    OUTPUT "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_EVALUATION_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_vertex_pipeline_stores_atomics_tese_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.tese"
      -o "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_EVALUATION_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.tese"
    VERBATIM)
  add_custom_command(
    OUTPUT "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_GEOMETRY_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_vertex_pipeline_stores_atomics_geom_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.geom"
      -o "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_GEOMETRY_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/vertex_pipeline_stores_atomics/stores_atomics.geom"
    VERBATIM)
  add_executable(vulkan_ps5_triangle_example
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  add_executable(vulkan_ps5_robust_vertex_probe
    examples/triangle/main.c
    "${VULKAN_PS5_ROBUST_VERTEX_HEADER}"
    "${VULKAN_PS5_ROBUST_VERTEX_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_robust_vertex_probe PRIVATE
    VULKAN_PS5_ROBUST_VERTEX_PROBE=1)
  add_executable(vulkan_ps5_logic_op_probe
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_logic_op_probe PRIVATE
    VULKAN_PS5_LOGIC_OP_PROBE=1)
  add_executable(vulkan_ps5_fill_mode_non_solid_probe
    examples/triangle/main.c
    "${VULKAN_PS5_NON_SOLID_VERTEX_HEADER}"
    "${VULKAN_PS5_NON_SOLID_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_fill_mode_non_solid_probe PRIVATE
    VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE=1)
  add_executable(vulkan_ps5_large_points_probe
    examples/triangle/main.c
    "${VULKAN_PS5_LARGE_POINTS_VERTEX_HEADER}"
    "${VULKAN_PS5_NON_SOLID_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_large_points_probe PRIVATE
    VULKAN_PS5_LARGE_POINTS_PROBE=1)
  add_executable(vulkan_ps5_wide_lines_probe
    examples/triangle/main.c
    "${VULKAN_PS5_WIDE_LINES_VERTEX_HEADER}"
    "${VULKAN_PS5_NON_SOLID_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_wide_lines_probe PRIVATE
    VULKAN_PS5_WIDE_LINES_PROBE=1)
  add_executable(vulkan_ps5_shader_demote_probe
    examples/triangle/main.c
    "${VULKAN_PS5_DEMOTE_VERTEX_HEADER}"
    "${VULKAN_PS5_DEMOTE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_shader_demote_probe PRIVATE
    VULKAN_PS5_DEMOTE_PROBE=1)
  add_executable(vulkan_ps5_shader_clip_distance_probe
    examples/triangle/main.c
    "${VULKAN_PS5_CLIP_DISTANCE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_shader_clip_distance_probe PRIVATE
    VULKAN_PS5_CLIP_DISTANCE_PROBE=1)
  add_executable(vulkan_ps5_shader_cull_distance_probe
    examples/triangle/main.c
    "${VULKAN_PS5_CULL_DISTANCE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_shader_cull_distance_probe PRIVATE
    VULKAN_PS5_CULL_DISTANCE_PROBE=1)
  add_executable(vulkan_ps5_geometry_example
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}"
    "${VULKAN_PS5_GEOMETRY_HEADER}")
  target_compile_definitions(vulkan_ps5_geometry_example
    PRIVATE VULKAN_PS5_GEOMETRY_SAMPLE=1)
  add_executable(vulkan_ps5_tessellation_example
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}"
    "${VULKAN_PS5_TESS_CONTROL_HEADER}"
    "${VULKAN_PS5_TESS_EVALUATION_HEADER}")
  target_compile_definitions(vulkan_ps5_tessellation_example
    PRIVATE VULKAN_PS5_TESSELLATION_SAMPLE=1)
  add_executable(vulkan_ps5_vertex_pipeline_stores_atomics_probe
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}"
    "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_VERTEX_HEADER}"
    "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_CONTROL_HEADER}"
    "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_EVALUATION_HEADER}"
    "${VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_GEOMETRY_HEADER}")
  target_compile_definitions(vulkan_ps5_vertex_pipeline_stores_atomics_probe
    PRIVATE VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE=1)
  add_executable(vulkan_ps5_query_example
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_query_example
    PRIVATE VULKAN_PS5_QUERY_SAMPLE=1)
  add_executable(vulkan_ps5_query_lifecycle_probe
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_query_lifecycle_probe PRIVATE
    VULKAN_PS5_QUERY_SAMPLE=1 VULKAN_PS5_QUERY_LIFECYCLE_ONLY=1)
  add_executable(vulkan_ps5_query_reset_probe
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_query_reset_probe PRIVATE
    VULKAN_PS5_QUERY_SAMPLE=1 VULKAN_PS5_QUERY_COMMAND_RESET_ONLY=1)
  add_executable(vulkan_ps5_query_idle_probe
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_query_idle_probe PRIVATE
    VULKAN_PS5_QUERY_SAMPLE=1 VULKAN_PS5_QUERY_IDLE_ONLY=1)
  set(VULKAN_PS5_DEPTH_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_depth_vert_spv.h")
  set(VULKAN_PS5_DEPTH_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_depth_frag_spv.h")
  set(VULKAN_PS5_DEPTH_CLAMP_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_depth_clamp_vert_spv.h")
  add_custom_command(OUTPUT "${VULKAN_PS5_DEPTH_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_depth_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/depth/depth.vert"
      -o "${VULKAN_PS5_DEPTH_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/depth/depth.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_DEPTH_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_depth_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/depth/depth.frag"
      -o "${VULKAN_PS5_DEPTH_FRAGMENT_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/depth/depth.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_DEPTH_CLAMP_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_depth_clamp_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/depth/depth_clamp.vert"
      -o "${VULKAN_PS5_DEPTH_CLAMP_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/depth/depth_clamp.vert"
    VERBATIM)
  add_executable(vulkan_ps5_depth_example
    examples/depth/main.c
    "${VULKAN_PS5_DEPTH_VERTEX_HEADER}"
    "${VULKAN_PS5_DEPTH_FRAGMENT_HEADER}")
  add_executable(vulkan_ps5_depth_bias_clamp_probe
    examples/depth/main.c
    "${VULKAN_PS5_DEPTH_VERTEX_HEADER}"
    "${VULKAN_PS5_DEPTH_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_depth_bias_clamp_probe PRIVATE
    VULKAN_PS5_DEPTH_BIAS_CLAMP_PROBE=1)
  add_executable(vulkan_ps5_depth_clamp_probe
    examples/depth/main.c
    "${VULKAN_PS5_DEPTH_CLAMP_VERTEX_HEADER}"
    "${VULKAN_PS5_DEPTH_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_depth_clamp_probe PRIVATE
    VULKAN_PS5_DEPTH_CLAMP_PROBE=1)
  set(VULKAN_PS5_MRT_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_mrt_vert_spv.h")
  set(VULKAN_PS5_MRT_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_mrt_frag_spv.h")
  add_custom_command(OUTPUT "${VULKAN_PS5_MRT_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_mrt_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/mrt/mrt.vert"
      -o "${VULKAN_PS5_MRT_VERTEX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/mrt/mrt.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_MRT_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_mrt_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/mrt/mrt.frag"
      -o "${VULKAN_PS5_MRT_FRAGMENT_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/mrt/mrt.frag"
    VERBATIM)
  add_executable(vulkan_ps5_mrt_example
    examples/mrt/main.c
    "${VULKAN_PS5_MRT_VERTEX_HEADER}"
    "${VULKAN_PS5_MRT_FRAGMENT_HEADER}")
  add_executable(vulkan_ps5_independent_blend_probe
    examples/mrt/main.c
    "${VULKAN_PS5_MRT_VERTEX_HEADER}"
    "${VULKAN_PS5_MRT_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_independent_blend_probe PRIVATE
    VULKAN_PS5_INDEPENDENT_BLEND_PROBE=1)
  set(VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_indexed_textured_vert_spv.h")
  set(VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_indexed_textured_frag_spv.h")
  set(VULKAN_PS5_IMAGE_GATHER_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_image_gather_frag_spv.h")
  set(VULKAN_PS5_FRAGMENT_STORES_ATOMICS_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_fragment_stores_atomics_frag_spv.h")
  set(VULKAN_PS5_DUAL_SRC_BLEND_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_dual_src_blend_frag_spv.h")
  set(VULKAN_PS5_INDIRECT_DRAW_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_indirect_draw_vert_spv.h")
  set(VULKAN_PS5_INDIRECT_PARAMETERS_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_indirect_parameters_vert_spv.h")
  add_custom_command(OUTPUT "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_indexed_textured_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indexed_textured.vert"
      -o "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indexed_textured.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_indexed_textured_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indexed_textured.frag"
      -o "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indexed_textured.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_IMAGE_GATHER_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_image_gather_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/image_gather.frag"
      -o "${VULKAN_PS5_IMAGE_GATHER_FRAGMENT_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/image_gather.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_FRAGMENT_STORES_ATOMICS_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_fragment_stores_atomics_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/fragment_stores_atomics.frag"
      -o "${VULKAN_PS5_FRAGMENT_STORES_ATOMICS_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/fragment_stores_atomics.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_DUAL_SRC_BLEND_FRAGMENT_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_dual_src_blend_frag_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/dual_src_blend.frag"
      -o "${VULKAN_PS5_DUAL_SRC_BLEND_FRAGMENT_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/dual_src_blend.frag"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_INDIRECT_DRAW_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_indirect_draw_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indirect_draw.vert"
      -o "${VULKAN_PS5_INDIRECT_DRAW_VERTEX_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indirect_draw.vert"
    VERBATIM)
  add_custom_command(OUTPUT "${VULKAN_PS5_INDIRECT_PARAMETERS_VERTEX_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_indirect_parameters_vert_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indirect_parameters.vert"
      -o "${VULKAN_PS5_INDIRECT_PARAMETERS_VERTEX_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/indexed_textured/indirect_parameters.vert"
    VERBATIM)
  add_executable(vulkan_ps5_indexed_textured_example
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}")
  add_executable(vulkan_ps5_shader_image_gather_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_IMAGE_GATHER_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_shader_image_gather_probe PRIVATE
    VULKAN_PS5_IMAGE_GATHER_PROBE=1)
  add_executable(vulkan_ps5_fragment_stores_atomics_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_FRAGMENT_STORES_ATOMICS_HEADER}")
  target_compile_definitions(vulkan_ps5_fragment_stores_atomics_probe PRIVATE
    VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE=1)
  add_executable(vulkan_ps5_dual_src_blend_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_DUAL_SRC_BLEND_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_dual_src_blend_probe PRIVATE
    VULKAN_PS5_DUAL_SRC_BLEND_PROBE=1)
  add_executable(vulkan_ps5_mirror_clamp_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_mirror_clamp_probe PRIVATE
    VULKAN_PS5_MIRROR_CLAMP_PROBE=1)
  add_executable(vulkan_ps5_vertex_divisor_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_vertex_divisor_probe PRIVATE
    VULKAN_PS5_VERTEX_DIVISOR_PROBE=1)
  add_executable(vulkan_ps5_sampler_anisotropy_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_sampler_anisotropy_probe PRIVATE
    VULKAN_PS5_ANISOTROPY_PROBE=1)
  add_executable(vulkan_ps5_indirect_draw_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDIRECT_DRAW_VERTEX_HEADER}"
    "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_indirect_draw_probe PRIVATE
    VULKAN_PS5_INDIRECT_DRAW_PROBE=1)
  add_executable(vulkan_ps5_indirect_parameters_probe
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDIRECT_PARAMETERS_VERTEX_HEADER}"
    "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}")
  target_compile_definitions(vulkan_ps5_indirect_parameters_probe PRIVATE
    VULKAN_PS5_INDIRECT_PARAMETERS_PROBE=1)
  add_executable(vulkan_ps5_buffer_copy_example
    examples/buffer_copy/main.c)
  if(CMAKE_CROSSCOMPILING)
    set_target_properties(vulkan_ps5_compute_example
      vulkan_ps5_storage_image_probe
      vulkan_ps5_robust_buffer_probe
      vulkan_ps5_variable_pointers_probe
      vulkan_ps5_robust_vertex_probe
      vulkan_ps5_triangle_example vulkan_ps5_geometry_example
      vulkan_ps5_logic_op_probe vulkan_ps5_fill_mode_non_solid_probe
      vulkan_ps5_large_points_probe
      vulkan_ps5_wide_lines_probe
      vulkan_ps5_shader_demote_probe
      vulkan_ps5_shader_clip_distance_probe
      vulkan_ps5_shader_cull_distance_probe
      vulkan_ps5_tessellation_example
      vulkan_ps5_vertex_pipeline_stores_atomics_probe
      vulkan_ps5_depth_example vulkan_ps5_depth_bias_clamp_probe
      vulkan_ps5_depth_clamp_probe
      vulkan_ps5_swapchain_example
      vulkan_ps5_mrt_example vulkan_ps5_independent_blend_probe
      vulkan_ps5_query_example
      vulkan_ps5_query_lifecycle_probe vulkan_ps5_query_reset_probe
      vulkan_ps5_query_idle_probe
      vulkan_ps5_indexed_textured_example vulkan_ps5_mirror_clamp_probe
      vulkan_ps5_shader_image_gather_probe
      vulkan_ps5_fragment_stores_atomics_probe
      vulkan_ps5_dual_src_blend_probe
      vulkan_ps5_vertex_divisor_probe vulkan_ps5_sampler_anisotropy_probe
      vulkan_ps5_indirect_draw_probe vulkan_ps5_indirect_parameters_probe
      vulkan_ps5_buffer_copy_example
      PROPERTIES SUFFIX ".elf")
  endif()
  foreach(example_target IN ITEMS
      vulkan_ps5_compute_example vulkan_ps5_storage_image_probe
      vulkan_ps5_robust_buffer_probe
      vulkan_ps5_variable_pointers_probe
      vulkan_ps5_robust_vertex_probe
      vulkan_ps5_triangle_example
      vulkan_ps5_logic_op_probe vulkan_ps5_fill_mode_non_solid_probe
      vulkan_ps5_large_points_probe
      vulkan_ps5_wide_lines_probe
      vulkan_ps5_shader_demote_probe
      vulkan_ps5_shader_clip_distance_probe
      vulkan_ps5_shader_cull_distance_probe
      vulkan_ps5_geometry_example
      vulkan_ps5_tessellation_example
      vulkan_ps5_vertex_pipeline_stores_atomics_probe
      vulkan_ps5_depth_example vulkan_ps5_depth_bias_clamp_probe
      vulkan_ps5_depth_clamp_probe
      vulkan_ps5_mrt_example
      vulkan_ps5_independent_blend_probe
      vulkan_ps5_swapchain_example
      vulkan_ps5_query_example vulkan_ps5_query_lifecycle_probe
      vulkan_ps5_query_reset_probe vulkan_ps5_query_idle_probe
      vulkan_ps5_indexed_textured_example vulkan_ps5_mirror_clamp_probe
      vulkan_ps5_shader_image_gather_probe
      vulkan_ps5_fragment_stores_atomics_probe
      vulkan_ps5_dual_src_blend_probe
      vulkan_ps5_vertex_divisor_probe vulkan_ps5_sampler_anisotropy_probe
      vulkan_ps5_indirect_draw_probe vulkan_ps5_indirect_parameters_probe
      vulkan_ps5_buffer_copy_example)
    target_compile_features(${example_target} PRIVATE c_std_11)
    target_compile_options(${example_target} PRIVATE -Wall -Wextra -Wpedantic)
    target_include_directories(${example_target} PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(${example_target} PRIVATE vulkan_ps5_static)
  endforeach()
  if(PROSPERO)
    target_link_libraries(vulkan_ps5_storage_image_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_robust_buffer_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_variable_pointers_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_robust_vertex_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_swapchain_example PRIVATE SceSystemService)
    target_link_libraries(vulkan_ps5_depth_bias_clamp_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_depth_clamp_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_logic_op_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_fill_mode_non_solid_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_large_points_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_wide_lines_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_shader_demote_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_shader_clip_distance_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_shader_cull_distance_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_independent_blend_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_indexed_textured_example PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_shader_image_gather_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_fragment_stores_atomics_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_dual_src_blend_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_vertex_pipeline_stores_atomics_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_mirror_clamp_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_vertex_divisor_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_sampler_anisotropy_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_indirect_draw_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_indirect_parameters_probe PRIVATE
      SceSystemService)
    target_link_libraries(vulkan_ps5_buffer_copy_example PRIVATE
      SceSystemService)
    add_executable(vulkan_ps5_process_cleanup examples/process_cleanup/main.c)
    set_target_properties(vulkan_ps5_process_cleanup PROPERTIES SUFFIX ".elf")
    target_compile_features(vulkan_ps5_process_cleanup PRIVATE c_std_11)
    target_compile_options(vulkan_ps5_process_cleanup PRIVATE
      -Wall -Wextra -Wpedantic)
    target_link_libraries(vulkan_ps5_process_cleanup PRIVATE
      kernel_sys SceSystemService unwind c++abi c++ m)
    add_executable(vulkan_ps5_system_exit_probe
      examples/system_exit_probe/main.c)
    set_target_properties(vulkan_ps5_system_exit_probe PROPERTIES SUFFIX ".elf")
    target_compile_features(vulkan_ps5_system_exit_probe PRIVATE c_std_11)
    target_compile_options(vulkan_ps5_system_exit_probe PRIVATE
      -Wall -Wextra -Wpedantic)
    target_link_libraries(vulkan_ps5_system_exit_probe PRIVATE
      kernel_sys SceSystemService unwind c++abi c++ m)
    if(EXISTS "${VULKAN_MEMORY_ALLOCATOR_ROOT}/include/vk_mem_alloc.h")
      enable_language(CXX)
      add_executable(vulkan_ps5_eden_vma_probe tests/eden_vma.cpp)
      set_target_properties(vulkan_ps5_eden_vma_probe PROPERTIES SUFFIX ".elf")
      target_compile_features(vulkan_ps5_eden_vma_probe PRIVATE cxx_std_20)
      target_compile_options(vulkan_ps5_eden_vma_probe PRIVATE
        -Wno-nullability-completeness)
      target_include_directories(vulkan_ps5_eden_vma_probe PRIVATE
        "${VULKAN_MEMORY_ALLOCATOR_ROOT}/include")
      target_link_libraries(vulkan_ps5_eden_vma_probe PRIVATE
        vulkan_ps5_static SceSystemService)
    endif()
  endif()
endif()

install(TARGETS vulkan_ps5_static EXPORT VulkanPS5Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(VULKAN_PS5_BUILD_SHARED)
  install(TARGETS vulkan_ps5_icd LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${OPENAGC_PSBC_LIBRARY}"
  DESTINATION ${CMAKE_INSTALL_LIBDIR} RENAME libopenagc_psbc.a)
install(FILES "${OPENAGC_PSBC_ROOT}/libopenagc_psbc/openagc_psbc.h"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(cmake/VulkanPS5Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/VulkanPS5Config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/VulkanPS5")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/VulkanPS5ConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT VulkanPS5Targets NAMESPACE VulkanPS5::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/VulkanPS5")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/VulkanPS5Config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/VulkanPS5ConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/VulkanPS5")
