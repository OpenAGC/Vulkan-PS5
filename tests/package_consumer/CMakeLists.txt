cmake_minimum_required(VERSION 3.20)
project(VulkanPS5PackageConsumer LANGUAGES C)

find_package(VulkanPS5 CONFIG REQUIRED)
add_executable(vulkan_ps5_package_consumer main.c)
target_link_libraries(vulkan_ps5_package_consumer PRIVATE VulkanPS5::ICD)
