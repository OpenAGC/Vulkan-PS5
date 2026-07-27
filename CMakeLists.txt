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
  src/vulkan_ps5_core.c)
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
    foreach(shader command.comp command.vert command.frag)
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
    endif()
  endif()
endif()

if(VULKAN_PS5_BUILD_EXAMPLES)
  find_program(VULKAN_PS5_GLSLANG_VALIDATOR glslangValidator REQUIRED)
  set(VULKAN_PS5_COMPUTE_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_compute_spv.h")
  add_custom_command(OUTPUT "${VULKAN_PS5_COMPUTE_HEADER}"
    COMMAND "${VULKAN_PS5_GLSLANG_VALIDATOR}" -V
      --vn vulkan_ps5_compute_spv
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/compute.comp"
      -o "${VULKAN_PS5_COMPUTE_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/compute/compute.comp"
    VERBATIM)
  add_executable(vulkan_ps5_compute_example
    examples/compute/main.c "${VULKAN_PS5_COMPUTE_HEADER}")
  set(VULKAN_PS5_TRIANGLE_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_triangle_vert_spv.h")
  set(VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_triangle_frag_spv.h")
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
  add_executable(vulkan_ps5_triangle_example
    examples/triangle/main.c
    "${VULKAN_PS5_TRIANGLE_VERTEX_HEADER}"
    "${VULKAN_PS5_TRIANGLE_FRAGMENT_HEADER}")
  set(VULKAN_PS5_DEPTH_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_depth_vert_spv.h")
  set(VULKAN_PS5_DEPTH_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_depth_frag_spv.h")
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
  add_executable(vulkan_ps5_depth_example
    examples/depth/main.c
    "${VULKAN_PS5_DEPTH_VERTEX_HEADER}"
    "${VULKAN_PS5_DEPTH_FRAGMENT_HEADER}")
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
  set(VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_indexed_textured_vert_spv.h")
  set(VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/vulkan_ps5_indexed_textured_frag_spv.h")
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
  add_executable(vulkan_ps5_indexed_textured_example
    examples/indexed_textured/main.c
    "${VULKAN_PS5_INDEXED_TEXTURED_VERTEX_HEADER}"
    "${VULKAN_PS5_INDEXED_TEXTURED_FRAGMENT_HEADER}")
  if(CMAKE_CROSSCOMPILING)
    set_target_properties(vulkan_ps5_compute_example
      vulkan_ps5_triangle_example vulkan_ps5_depth_example
      vulkan_ps5_mrt_example vulkan_ps5_indexed_textured_example
      PROPERTIES SUFFIX ".elf")
  endif()
  foreach(example_target IN ITEMS
      vulkan_ps5_compute_example vulkan_ps5_triangle_example
      vulkan_ps5_depth_example vulkan_ps5_mrt_example
      vulkan_ps5_indexed_textured_example)
    target_compile_features(${example_target} PRIVATE c_std_11)
    target_compile_options(${example_target} PRIVATE -Wall -Wextra -Wpedantic)
    target_include_directories(${example_target} PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(${example_target} PRIVATE vulkan_ps5_static)
  endforeach()
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
