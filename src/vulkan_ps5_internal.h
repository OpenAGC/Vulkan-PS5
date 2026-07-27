#ifndef VULKAN_PS5_INTERNAL_H
#define VULKAN_PS5_INTERNAL_H

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vk_layer.h>

#include <stddef.h>

#if defined(_WIN32)
#define VK_PS5_EXPORT __declspec(dllexport)
#else
#define VK_PS5_EXPORT __attribute__((visibility("default")))
#endif

void *vk_ps5_device_alloc(VkDevice device, const VkAllocationCallbacks *allocator,
                          size_t size, size_t alignment, VkSystemAllocationScope scope);
void vk_ps5_device_free(VkDevice device, const VkAllocationCallbacks *allocator, void *ptr);
VkResult vk_ps5_set_device_loader_data(VkDevice device, void *object);
VkDevice vk_ps5_queue_device(VkQueue queue);
VkDeviceSize vk_ps5_memory_size(VkDeviceMemory memory);

#endif
