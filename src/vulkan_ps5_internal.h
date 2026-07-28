#ifndef VULKAN_PS5_INTERNAL_H
#define VULKAN_PS5_INTERNAL_H

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vk_layer.h>
#include <agc_graphics.h>

#include <stddef.h>
#include <stdint.h>

#define VK_PS5_DCB_SIZE (64u * 1024u)
#define VK_PS5_PRESENT_TIMEOUT_US 2000000u

#if defined(_WIN32)
#define VK_PS5_EXPORT __declspec(dllexport)
#else
#define VK_PS5_EXPORT __attribute__((visibility("default")))
#endif

void *vk_ps5_device_alloc(VkDevice device, const VkAllocationCallbacks *allocator,
                          size_t size, size_t alignment, VkSystemAllocationScope scope);
void vk_ps5_device_free(VkDevice device, const VkAllocationCallbacks *allocator, void *ptr);
void *vk_ps5_instance_alloc(VkInstance instance,
    const VkAllocationCallbacks *allocator, size_t size, size_t alignment,
    VkSystemAllocationScope scope);
void vk_ps5_instance_free(VkInstance instance,
    const VkAllocationCallbacks *allocator, void *ptr);
VkResult vk_ps5_set_device_loader_data(VkDevice device, void *object);
VkDevice vk_ps5_queue_device(VkQueue queue);
VkDeviceSize vk_ps5_memory_size(VkDeviceMemory memory);
uint64_t vk_ps5_memory_gpu_address(VkDeviceMemory memory, VkDeviceSize offset);
VkResult vk_ps5_queue_submit_dcb(
    VkQueue queue, const uint32_t *commands, uint32_t dword_count);
VkResult vk_ps5_signal_acquire(VkSemaphore semaphore, VkFence fence);
VkResult vk_ps5_consume_semaphores(
    uint32_t semaphore_count, const VkSemaphore *semaphores);
VkResult vk_ps5_device_prepare_tessellation(
    VkDevice device, const AgcGfx1013TessellationState **state,
    uint64_t *ring_descriptor_address);
uint32_t vk_ps5_command_buffer_dwords(
    VkCommandBuffer command_buffer, const uint32_t **commands);
uint32_t vk_ps5_command_buffer_indirect_descriptor_table(
    VkCommandBuffer command_buffer);
uint32_t vk_ps5_command_buffer_indirect_descriptor_entry(
    VkCommandBuffer command_buffer, uint32_t set);
uint32_t vk_ps5_command_buffer_indirect_descriptor_register(
    VkCommandBuffer command_buffer);

#endif
