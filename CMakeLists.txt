cmake_minimum_required(VERSION 3.20)
project(VulkanPS5 VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(VULKAN_HEADERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Vulkan-Headers" CACHE PATH
    "Path to a Vulkan-Headers checkout")
set(OPENAGC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../OpenAGC" CACHE PATH
    "Path to an OpenAGC checkout (required by Prospero consumers)")
set(OPENAGC_PSBC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../openagc-psbc" CACHE PATH
    "Path to openagc-psbc")
option(VULKAN_PS5_BUILD_SHARED "Build the loader-compatible host ICD" ON)
option(VULKAN_PS5_BUILD_TESTS "Build host lifecycle tests" ON)

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
target_link_libraries(vulkan_ps5_objects PRIVATE OpenAGC::openagc)

add_library(vulkan_ps5_static STATIC $<TARGET_OBJECTS:vulkan_ps5_objects>)
add_library(VulkanPS5::ICD ALIAS vulkan_ps5_static)
set_target_properties(vulkan_ps5_static PROPERTIES
  OUTPUT_NAME vulkan_ps5 EXPORT_NAME ICD)
target_include_directories(vulkan_ps5_static
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${VULKAN_HEADERS_ROOT}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(vulkan_ps5_static PUBLIC Vulkan::Headers OpenAGC::openagc)

if(VULKAN_PS5_BUILD_SHARED)
  add_library(vulkan_ps5_icd SHARED $<TARGET_OBJECTS:vulkan_ps5_objects>)
  set_target_properties(vulkan_ps5_icd PROPERTIES OUTPUT_NAME vulkan_ps5)
  target_include_directories(vulkan_ps5_icd
    PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
      "$<BUILD_INTERFACE:${VULKAN_HEADERS_ROOT}/include>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
  target_link_libraries(vulkan_ps5_icd PRIVATE OpenAGC::openagc)
  set(VULKAN_PS5_ICD_LIBRARY_PATH
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}vulkan_ps5${CMAKE_SHARED_LIBRARY_SUFFIX}")
  configure_file(vulkan_ps5_icd.json.in vulkan_ps5_icd.json @ONLY)
endif()

if(VULKAN_PS5_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)
  add_executable(vulkan_ps5_lifecycle_test tests/lifecycle.c)
  target_link_libraries(vulkan_ps5_lifecycle_test PRIVATE vulkan_ps5_static)
  add_test(NAME vulkan_ps5.lifecycle COMMAND vulkan_ps5_lifecycle_test)
  add_executable(vulkan_ps5_robustness_test tests/robustness.c)
  target_link_libraries(vulkan_ps5_robustness_test
    PRIVATE vulkan_ps5_static Threads::Threads)
  add_test(NAME vulkan_ps5.robustness COMMAND vulkan_ps5_robustness_test)
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

install(TARGETS vulkan_ps5_static EXPORT VulkanPS5Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(VULKAN_PS5_BUILD_SHARED)
  install(TARGETS vulkan_ps5_icd LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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
