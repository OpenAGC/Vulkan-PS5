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
#define VK_PS5_IDLE_TIMEOUT_NS UINT64_C(2000000000)

#if defined(_WIN32)
#define VK_PS5_EXPORT __declspec(dllexport)
#else
#define VK_PS5_EXPORT __attribute__((visibility("default")))
#endif

void *vk_ps5_device_alloc(VkDevice device, const VkAllocationCallbacks *allocator,
                          size_t size, size_t alignment, VkSystemAllocationScope scope);
void vk_ps5_device_free(VkDevice device, const VkAllocationCallbacks *allocator, void *ptr);
VkBool32 vk_ps5_device_robust_buffer_access(VkDevice device);
VkBool32 vk_ps5_device_depth_clip_enable(VkDevice device);
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
typedef enum VkPs5NativeObjectType {
    VK_PS5_NATIVE_BUFFER,
    VK_PS5_NATIVE_IMAGE,
    VK_PS5_NATIVE_IMAGE_VIEW,
    VK_PS5_NATIVE_SAMPLER,
    VK_PS5_NATIVE_SHADER,
    VK_PS5_NATIVE_GRAPHICS_PIPELINE,
    VK_PS5_NATIVE_COMPUTE_PIPELINE,
    VK_PS5_NATIVE_MEMORY,
} VkPs5NativeObjectType;
void vk_ps5_destroy_or_defer_native(VkDevice device,
    VkPs5NativeObjectType type, void *object);
void vk_ps5_collect_deferred_native(VkDevice device);
uint32_t vk_ps5_deferred_native_count(VkDevice device);
VkBool32 vk_ps5_device_null_descriptor(VkDevice device);
VkPipeline vk_ps5_device_meta_clear_pipeline(VkDevice device);
VkResult vk_ps5_device_meta_attachment_pipeline(VkDevice device,
    VkFormat format, VkImageAspectFlags aspects, VkPipeline *pipeline_out);
VkResult vk_ps5_device_meta_blit_resources(VkDevice device, VkFormat format,
    VkFilter filter, VkBool32 source_3d, VkPipeline *pipeline_out,
    VkSampler *sampler_out);
VkResult vk_ps5_device_meta_resolve_pipeline(VkDevice device, VkFormat format,
    VkPipeline *pipeline_out);
VkResult vk_ps5_initialize_meta_clear(VkDevice device,
    VkPipelineLayout *layout_out, VkPipeline *pipeline_out);
VkResult vk_ps5_initialize_meta_attachment_clear(VkDevice device,
    VkFormat format, VkImageAspectFlags aspects,
    VkPipelineLayout *layout_out, VkPipeline *pipeline_out);
VkResult vk_ps5_initialize_meta_blit(VkDevice device, VkFormat format,
    VkBool32 source_3d, VkPipelineLayout *layout_out,
    VkPipeline *pipeline_out);
VkResult vk_ps5_initialize_meta_resolve(VkDevice device, VkFormat format,
    VkPipelineLayout *layout_out, VkPipeline *pipeline_out);
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
VkBool32 vk_ps5_command_buffer_has_native(VkCommandBuffer command_buffer);
uint32_t vk_ps5_command_buffer_native_state(VkCommandBuffer command_buffer);
uint32_t vk_ps5_command_buffer_native_dispatch_count(
    VkCommandBuffer command_buffer);
uint32_t vk_ps5_command_buffer_native_draw_count(
    VkCommandBuffer command_buffer);
VkBool32 vk_ps5_command_buffer_native_stream_complete(
    VkCommandBuffer command_buffer);
VkResult vk_ps5_command_buffer_record_error(VkCommandBuffer command_buffer);
VkBool32 vk_ps5_pack_clear_color(VkFormat format,
    const VkClearColorValue *clear, uint32_t pattern[4],
    uint32_t *pattern_word_count);
VkBool32 vk_ps5_pack_depth_stencil_clear(VkFormat format,
    VkImageAspectFlagBits aspect, const VkClearDepthStencilValue *clear,
    uint32_t pattern[4], uint32_t *pattern_word_count,
    uint32_t *plane);
VkBool32 vk_ps5_command_buffer_push_constant_word(
    VkCommandBuffer command_buffer, uint32_t stage, uint32_t offset,
    uint32_t *value);
VkBool32 vk_ps5_descriptor_set_buffer_info(
    VkDescriptorSet descriptor_set, uint32_t binding, uint32_t array_element,
    VkDescriptorBufferInfo *info);
VkBool32 vk_ps5_pipeline_has_native_shaders(VkPipeline pipeline);
VkBool32 vk_ps5_pipeline_has_native_compute_pipeline(VkPipeline pipeline);
VkBool32 vk_ps5_pipeline_has_native_graphics_pipeline(VkPipeline pipeline);
AgcRasterizationStateFlags vk_ps5_pipeline_native_rasterization_flags(
    VkPipeline pipeline);
VkResult vk_ps5_enable_image_scanout(VkImage image);
AgcImage vk_ps5_native_image(VkImage image);
void vk_ps5_set_image_native_usage(VkImage image, AgcResourceUsage usage);
VkBool32 vk_ps5_swapchain_has_native_present_chain(VkSwapchainKHR swapchain);

#endif
