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
void vk_ps5_device_retain_wsi_ownership(VkDevice device);
void vk_ps5_device_release_wsi_ownership(VkDevice device);
uint32_t vk_ps5_device_wsi_ownership_count(VkDevice device);
#if !defined(__PROSPERO__)
void vk_ps5_debug_set_device_teardown_failure_stage(uint32_t stage);
#endif
uint32_t vk_ps5_device_address32_hi(VkDevice device);
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
VkBool32 vk_ps5_image_format_list_supports_usage(
    VkPhysicalDevice physical_device, VkFormat base_format,
    VkImageTiling tiling, VkImageUsageFlags usage,
    const VkImageFormatListCreateInfo *format_list);
VkPipeline vk_ps5_device_meta_clear_pipeline(VkDevice device);
VkResult vk_ps5_device_meta_attachment_pipeline(VkDevice device,
    VkRenderPass render_pass, uint32_t subpass, uint32_t color_attachment,
    VkFormat format, VkImageAspectFlags aspects,
    VkPipelineLayout *layout_out, VkPipeline *pipeline_out);
uint64_t vk_ps5_render_pass_meta_cache_id(VkRenderPass render_pass);
VkResult vk_ps5_device_meta_blit_resources(VkDevice device, VkFormat format,
    VkFilter filter, VkBool32 source_3d, VkPipeline *pipeline_out,
    VkSampler *sampler_out);
VkResult vk_ps5_device_meta_resolve_pipeline(VkDevice device, VkFormat format,
    VkPipeline *pipeline_out);
VkResult vk_ps5_initialize_meta_clear(VkDevice device,
    VkPipelineLayout *layout_out, VkPipeline *pipeline_out);
VkResult vk_ps5_initialize_meta_attachment_clear(VkDevice device,
    VkRenderPass render_pass, uint32_t subpass, uint32_t color_attachment,
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
/* Internal diagnostic only: directly replay a legacy graphics pipeline against
 * one active color attachment. This deliberately bypasses normal descriptor,
 * vertex, and attachment replay so probes can isolate that native sequence. */
VkResult vk_ps5_command_buffer_native_color_target_control(
    VkCommandBuffer command_buffer, VkPipeline pipeline,
    uint32_t color_attachment, const VkRect2D *rect);
/* Internal diagnostic only: replay the cached color-clear meta pipeline
 * through the ordinary Vulkan draw path. */
VkResult vk_ps5_command_buffer_meta_color_pipeline_control(
    VkCommandBuffer command_buffer, uint32_t color_attachment,
    const VkRect2D *rect, const float color[4]);
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
uint64_t vk_ps5_native_push_constant_required_mask(
    const AgcShaderReflection *reflection,
    const AgcShaderPushConstantRange *range);
VkBool32 vk_ps5_descriptor_set_buffer_info(
    VkDescriptorSet descriptor_set, uint32_t binding, uint32_t array_element,
    VkDescriptorBufferInfo *info);
VkBool32 vk_ps5_pipeline_has_native_shaders(VkPipeline pipeline);
VkBool32 vk_ps5_pipeline_has_native_compute_pipeline(VkPipeline pipeline);
VkBool32 vk_ps5_pipeline_has_native_graphics_pipeline(VkPipeline pipeline);
AgcRasterizationStateFlags vk_ps5_pipeline_native_rasterization_flags(
    VkPipeline pipeline);
VkResult vk_ps5_enable_image_scanout(VkImage image);
VkFormat vk_ps5_image_blit_format(VkImage image);
AgcImage vk_ps5_native_image(VkImage image);
VkBool32 vk_ps5_image_view_has_native(VkImageView image_view);
void vk_ps5_set_image_native_usage(VkImage image, AgcResourceUsage usage);
VkBool32 vk_ps5_swapchain_has_native_present_chain(VkSwapchainKHR swapchain);

#endif
