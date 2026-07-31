#ifndef VULKAN_PS5_INTERNAL_H
#define VULKAN_PS5_INTERNAL_H

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>
#include <vulkan/vk_layer.h>
#include <agc_graphics.h>
#include <openagc/runtime.h>

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
VkBool32 vk_ps5_device_robust_buffer_access(VkDevice device);
VkResult vk_ps5_device_allocate_border_color(
    VkDevice device, const VkClearColorValue *color, uint32_t *index);
void vk_ps5_device_free_border_color(VkDevice device, uint32_t index);
uint64_t vk_ps5_device_border_color_table(VkDevice device);
void *vk_ps5_instance_alloc(VkInstance instance,
    const VkAllocationCallbacks *allocator, size_t size, size_t alignment,
    VkSystemAllocationScope scope);
void vk_ps5_instance_free(VkInstance instance,
    const VkAllocationCallbacks *allocator, void *ptr);
VkResult vk_ps5_set_device_loader_data(VkDevice device, void *object);
VkDevice vk_ps5_queue_device(VkQueue queue);
VkDeviceSize vk_ps5_memory_size(VkDeviceMemory memory);
uint64_t vk_ps5_memory_gpu_address(VkDeviceMemory memory, VkDeviceSize offset);
AgcDevice vk_ps5_native_device(VkDevice device);
AgcMemory vk_ps5_native_memory(VkDeviceMemory memory);
uint32_t vk_ps5_memory_type_index(VkDeviceMemory memory);
VkResult vk_ps5_queue_submit_dcb(
    VkQueue queue, const uint32_t *commands, uint32_t dword_count);
VkResult vk_ps5_queue_submit_native(VkQueue queue,
    uint32_t command_buffer_count,
    const AgcCommandBuffer *command_buffers);
VkResult vk_ps5_queue_present_native(VkQueue queue,
    AgcPresentChain present_chain, uint32_t image_index, uint64_t frame_id,
    uint64_t timeout_ns);
VkResult vk_ps5_device_initialize_present_images(VkDevice device,
    uint32_t image_count, const AgcImage *images);
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
VkBool32 vk_ps5_command_buffer_has_native(VkCommandBuffer command_buffer);
uint32_t vk_ps5_command_buffer_native_state(VkCommandBuffer command_buffer);
uint32_t vk_ps5_command_buffer_native_dispatch_count(
    VkCommandBuffer command_buffer);
uint32_t vk_ps5_command_buffer_native_draw_count(
    VkCommandBuffer command_buffer);
VkBool32 vk_ps5_command_buffer_native_stream_complete(
    VkCommandBuffer command_buffer);
VkResult vk_ps5_command_buffer_record_error(VkCommandBuffer command_buffer);
VkBool32 vk_ps5_pipeline_has_native_shaders(VkPipeline pipeline);
VkBool32 vk_ps5_pipeline_has_native_compute_pipeline(VkPipeline pipeline);
VkBool32 vk_ps5_pipeline_has_native_graphics_pipeline(VkPipeline pipeline);
VkResult vk_ps5_enable_image_scanout(VkImage image);
AgcImage vk_ps5_native_image(VkImage image);
void vk_ps5_set_image_native_usage(VkImage image, AgcResourceUsage usage);
VkBool32 vk_ps5_swapchain_has_native_present_chain(VkSwapchainKHR swapchain);

#endif
