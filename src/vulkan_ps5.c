#include "vulkan_ps5_internal.h"

#include "agc_capabilities.h"
#include "agc_error.h"
#include "agc_graphics.h"
#include "openagc/runtime.h"

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_PS5_API_VERSION VK_MAKE_API_VERSION(0, 1, 2, 0)
#define VK_PS5_VENDOR_ID 0x1002u
#define VK_PS5_DEVICE_ID 0x163fu

typedef struct VkPs5Instance VkPs5Instance;
typedef struct VkPs5PhysicalDevice VkPs5PhysicalDevice;
typedef struct VkPs5Device VkPs5Device;
typedef struct VkPs5Queue VkPs5Queue;
typedef struct VkPs5Memory VkPs5Memory;
typedef struct VkPs5DeferredNative VkPs5DeferredNative;

struct VkPs5DeferredNative {
    VkPs5NativeObjectType type;
    void *object;
    VkPs5DeferredNative *next;
};

struct VkPs5PhysicalDevice {
    VK_LOADER_DATA loader_data;
    VkPs5Instance *instance;
};

struct VkPs5Instance {
    VK_LOADER_DATA loader_data;
    VkAllocationCallbacks allocator;
    VkBool32 has_allocator;
    VkPs5PhysicalDevice physical_device;
};

struct VkPs5Queue {
    VK_LOADER_DATA loader_data;
    VkPs5Device *device;
    atomic_flag submit_lock;
    AgcFence present_ready_fence;
};

#define VK_PS5_META_ATTACHMENT_PIPELINE_COUNT 32u
#define VK_PS5_META_BLIT_PIPELINE_COUNT 16u
#define VK_PS5_META_RESOLVE_PIPELINE_COUNT 16u

typedef struct VkPs5MetaAttachmentPipeline {
    VkFormat format;
    VkImageAspectFlags aspects;
    VkPipelineLayout layout;
    VkPipeline pipeline;
} VkPs5MetaAttachmentPipeline;

typedef struct VkPs5MetaBlitPipeline {
    VkFormat format;
    VkBool32 source_3d;
    VkPipelineLayout layout;
    VkPipeline pipeline;
} VkPs5MetaBlitPipeline;

typedef struct VkPs5MetaResolvePipeline {
    VkFormat format;
    VkPipelineLayout layout;
    VkPipeline pipeline;
} VkPs5MetaResolvePipeline;

struct VkPs5Device {
    VK_LOADER_DATA loader_data;
    VkAllocationCallbacks allocator;
    VkBool32 has_allocator;
    VkPs5PhysicalDevice *physical_device;
    AgcDevice native_device;
    AgcQueue native_graphics_queue;
    AgcQueue native_compute_queue;
    VkPs5Queue queue;
    VkBool32 robust_buffer_access;
    VkBool32 null_descriptor;
    VkBool32 depth_clip_enable;
    VkPipelineLayout meta_clear_layout;
    VkPipeline meta_clear_pipeline;
    atomic_flag meta_attachment_lock;
    uint32_t meta_attachment_count;
    VkPs5MetaAttachmentPipeline meta_attachments[
        VK_PS5_META_ATTACHMENT_PIPELINE_COUNT];
    uint32_t meta_blit_count;
    VkPs5MetaBlitPipeline meta_blits[VK_PS5_META_BLIT_PIPELINE_COUNT];
    VkSampler meta_blit_samplers[2];
    uint32_t meta_resolve_count;
    VkPs5MetaResolvePipeline meta_resolves[
        VK_PS5_META_RESOLVE_PIPELINE_COUNT];
    atomic_uint memory_allocation_count;
    atomic_flag deferred_native_lock;
    VkPs5DeferredNative *deferred_native;
};

struct VkPs5Memory {
    AgcDevice device;
    AgcMemory native_memory;
    void *data;
    VkDeviceSize size;
    uint32_t memory_type_index;
};

static void PS5_SYSV_ABI vk_ps5_native_debug_message(
    void *user_data, const AgcDebugMessage *message)
{
    (void)user_data;
    if (!message)
        return;
    if (message->result == AGC_ERROR_BUSY &&
        (!strcmp(message->function_name, "agcDestroyBuffer") ||
         !strcmp(message->function_name, "agcDestroyImage") ||
         !strcmp(message->function_name, "agcDestroyImageView") ||
         !strcmp(message->function_name, "agcDestroySampler") ||
         !strcmp(message->function_name, "agcDestroyShader") ||
         !strcmp(message->function_name, "agcDestroyGraphicsPipeline") ||
         !strcmp(message->function_name, "agcDestroyComputePipeline") ||
         !strcmp(message->function_name, "agcDestroyMemory")))
        return;
    fprintf(stderr,
        "vulkan-ps5: OpenAGC %s failed: 0x%08x (%s)\n",
        message->function_name[0] ? message->function_name : "runtime",
        (unsigned)message->result,
        message->message[0] ? message->message : "no diagnostic");
}

static AgcDeviceProperties ps5_capabilities(void) {
    AgcDeviceProperties capabilities = AGC_DEVICE_PROPERTIES_INIT;
    int32_t result = agcGetDeviceProperties(NULL, &capabilities);
    if (result != AGC_OK)
        capabilities = (AgcDeviceProperties)AGC_DEVICE_PROPERTIES_INIT;
    return capabilities;
}

static VkMemoryPropertyFlags ps5_memory_flags(
    AgcMemoryPropertyFlags flags) {
    VkMemoryPropertyFlags result = 0;
    if (flags & AGC_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        result |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (flags & AGC_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        result |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (flags & AGC_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        result |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (flags & AGC_MEMORY_PROPERTY_HOST_CACHED_BIT)
        result |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    return result;
}

static int ps5_color_format(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: return AGC_GFX1013_RT_FORMAT_R8_UNORM;
    case VK_FORMAT_R8_SNORM: return AGC_GFX1013_RT_FORMAT_R8_SNORM;
    case VK_FORMAT_R8_UINT: return AGC_GFX1013_RT_FORMAT_R8_UINT;
    case VK_FORMAT_R8_SINT: return AGC_GFX1013_RT_FORMAT_R8_SINT;
    case VK_FORMAT_R8G8_UNORM: return AGC_GFX1013_RT_FORMAT_RG8_UNORM;
    case VK_FORMAT_R8G8_SNORM: return AGC_GFX1013_RT_FORMAT_RG8_SNORM;
    case VK_FORMAT_R8G8_UINT: return AGC_GFX1013_RT_FORMAT_RG8_UINT;
    case VK_FORMAT_R8G8_SINT: return AGC_GFX1013_RT_FORMAT_RG8_SINT;
    case VK_FORMAT_R8G8B8A8_UNORM: return AGC_GFX1013_RT_FORMAT_RGBA8_UNORM;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return AGC_GFX1013_RT_FORMAT_RGBA8_UNORM;
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32: return AGC_GFX1013_RT_FORMAT_RGBA8_SNORM;
    case VK_FORMAT_A8B8G8R8_UINT_PACK32: return AGC_GFX1013_RT_FORMAT_RGBA8_UINT;
    case VK_FORMAT_A8B8G8R8_SINT_PACK32: return AGC_GFX1013_RT_FORMAT_RGBA8_SINT;
    case VK_FORMAT_B8G8R8A8_UNORM: return AGC_GFX1013_RT_FORMAT_BGRA8_UNORM;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM;
    case VK_FORMAT_A2B10G10R10_UINT_PACK32: return AGC_GFX1013_RT_FORMAT_RGB10A2_UINT;
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return AGC_GFX1013_RT_FORMAT_BGR10A2_UNORM;
    case VK_FORMAT_R5G6B5_UNORM_PACK16: return AGC_GFX1013_RT_FORMAT_R5G6B5_UNORM;
    case VK_FORMAT_B5G6R5_UNORM_PACK16: return AGC_GFX1013_RT_FORMAT_B5G6R5_UNORM;
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16: return AGC_GFX1013_RT_FORMAT_R5G5B5A1_UNORM;
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16: return AGC_GFX1013_RT_FORMAT_A1R5G5B5_UNORM;
    case VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT: return AGC_GFX1013_RT_FORMAT_A4B4G4R4_UNORM;
    case VK_FORMAT_R16_UNORM: return AGC_GFX1013_RT_FORMAT_R16_UNORM;
    case VK_FORMAT_R16_SNORM: return AGC_GFX1013_RT_FORMAT_R16_SNORM;
    case VK_FORMAT_R16_UINT: return AGC_GFX1013_RT_FORMAT_R16_UINT;
    case VK_FORMAT_R16_SINT: return AGC_GFX1013_RT_FORMAT_R16_SINT;
    case VK_FORMAT_R16_SFLOAT: return AGC_GFX1013_RT_FORMAT_R16_FLOAT;
    case VK_FORMAT_R16G16_UNORM: return AGC_GFX1013_RT_FORMAT_RG16_UNORM;
    case VK_FORMAT_R16G16_SNORM: return AGC_GFX1013_RT_FORMAT_RG16_SNORM;
    case VK_FORMAT_R16G16_UINT: return AGC_GFX1013_RT_FORMAT_RG16_UINT;
    case VK_FORMAT_R16G16_SINT: return AGC_GFX1013_RT_FORMAT_RG16_SINT;
    case VK_FORMAT_R16G16_SFLOAT: return AGC_GFX1013_RT_FORMAT_RG16_FLOAT;
    case VK_FORMAT_R16G16B16A16_UNORM: return AGC_GFX1013_RT_FORMAT_RGBA16_UNORM;
    case VK_FORMAT_R16G16B16A16_SNORM: return AGC_GFX1013_RT_FORMAT_RGBA16_SNORM;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT;
    case VK_FORMAT_R16G16B16A16_UINT: return AGC_GFX1013_RT_FORMAT_RGBA16_UINT;
    case VK_FORMAT_R16G16B16A16_SINT: return AGC_GFX1013_RT_FORMAT_RGBA16_SINT;
    case VK_FORMAT_R32_SFLOAT: return AGC_GFX1013_RT_FORMAT_R32_FLOAT;
    case VK_FORMAT_R32_UINT: return AGC_GFX1013_RT_FORMAT_R32_UINT;
    case VK_FORMAT_R32_SINT: return AGC_GFX1013_RT_FORMAT_R32_SINT;
    case VK_FORMAT_R32G32_SFLOAT: return AGC_GFX1013_RT_FORMAT_RG32_FLOAT;
    case VK_FORMAT_R32G32_UINT: return AGC_GFX1013_RT_FORMAT_RG32_UINT;
    case VK_FORMAT_R32G32_SINT: return AGC_GFX1013_RT_FORMAT_RG32_SINT;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT;
    case VK_FORMAT_R32G32B32A32_UINT: return AGC_GFX1013_RT_FORMAT_RGBA32_UINT;
    case VK_FORMAT_R32G32B32A32_SINT: return AGC_GFX1013_RT_FORMAT_RGBA32_SINT;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT;
    case VK_FORMAT_R8G8B8A8_SRGB: return AGC_GFX1013_RT_FORMAT_RGBA8_SRGB;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return AGC_GFX1013_RT_FORMAT_RGBA8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB: return AGC_GFX1013_RT_FORMAT_BGRA8_SRGB;
    default: return -1;
    }
}

static int ps5_depth_format(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM: return AGC_GFX1013_DEPTH_FORMAT_D16_UNORM;
    case VK_FORMAT_D32_SFLOAT: return AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT;
    case VK_FORMAT_S8_UINT: return AGC_GFX1013_DEPTH_FORMAT_S8_UINT;
    case VK_FORMAT_D16_UNORM_S8_UINT:
        return AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT;
    default: return -1;
    }
}

static int ps5_bc_format(VkFormat format) {
    switch (format) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return 1;
    default:
        return 0;
    }
}

static void *ps5_alloc(const VkAllocationCallbacks *allocator, size_t size,
                       size_t alignment, VkSystemAllocationScope scope) {
    if (allocator && allocator->pfnAllocation)
        return allocator->pfnAllocation(allocator->pUserData, size, alignment, scope);
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    void *ptr = NULL;
    return posix_memalign(&ptr, alignment, size) == 0 ? ptr : NULL;
}

static void ps5_free(const VkAllocationCallbacks *allocator, void *ptr) {
    if (!ptr) return;
    if (allocator && allocator->pfnFree)
        allocator->pfnFree(allocator->pUserData, ptr);
    else
        free(ptr);
}

static const VkAllocationCallbacks *instance_allocator(VkPs5Instance *instance) {
    return instance->has_allocator ? &instance->allocator : NULL;
}

static const VkAllocationCallbacks *device_allocator(VkPs5Device *device) {
    return device->has_allocator ? &device->allocator : NULL;
}

void *vk_ps5_device_alloc(VkDevice device_handle, const VkAllocationCallbacks *allocator,
                          size_t size, size_t alignment, VkSystemAllocationScope scope) {
    VkPs5Device *device = (VkPs5Device *)device_handle;
    const VkAllocationCallbacks *selected = allocator;
    if (!selected && device) selected = device_allocator(device);
    return ps5_alloc(selected, size, alignment, scope);
}

void vk_ps5_device_free(VkDevice device_handle, const VkAllocationCallbacks *allocator,
                        void *ptr) {
    VkPs5Device *device = (VkPs5Device *)device_handle;
    const VkAllocationCallbacks *selected = allocator;
    if (!selected && device) selected = device_allocator(device);
    ps5_free(selected, ptr);
}

VkBool32 vk_ps5_device_robust_buffer_access(VkDevice device_handle) {
    const VkPs5Device *device = (const VkPs5Device *)device_handle;
    return device ? device->robust_buffer_access : VK_FALSE;
}

VkBool32 vk_ps5_device_depth_clip_enable(VkDevice device_handle) {
    const VkPs5Device *device = (const VkPs5Device *)device_handle;
    return device ? device->depth_clip_enable : VK_FALSE;
}

VkPipeline vk_ps5_device_meta_clear_pipeline(VkDevice device_handle) {
    const VkPs5Device *device = (const VkPs5Device *)device_handle;
    return device ? device->meta_clear_pipeline : VK_NULL_HANDLE;
}

VkResult vk_ps5_device_meta_attachment_pipeline(VkDevice device_handle,
    VkFormat format, VkImageAspectFlags aspects, VkPipeline *pipeline_out)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    if (!device || !pipeline_out)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pipeline_out = VK_NULL_HANDLE;
    while (atomic_flag_test_and_set_explicit(&device->meta_attachment_lock,
            memory_order_acquire)) {}
    for (uint32_t index = 0u; index < device->meta_attachment_count; ++index) {
        VkPs5MetaAttachmentPipeline *entry = &device->meta_attachments[index];
        if (entry->format == format && entry->aspects == aspects) {
            *pipeline_out = entry->pipeline;
            atomic_flag_clear_explicit(&device->meta_attachment_lock,
                memory_order_release);
            return VK_SUCCESS;
        }
    }
    if (device->meta_attachment_count >=
            VK_PS5_META_ATTACHMENT_PIPELINE_COUNT) {
        atomic_flag_clear_explicit(&device->meta_attachment_lock,
            memory_order_release);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    VkPs5MetaAttachmentPipeline *entry =
        &device->meta_attachments[device->meta_attachment_count];
    VkResult result = vk_ps5_initialize_meta_attachment_clear(device_handle,
        format, aspects, &entry->layout, &entry->pipeline);
    if (result == VK_SUCCESS) {
        entry->format = format;
        entry->aspects = aspects;
        *pipeline_out = entry->pipeline;
        device->meta_attachment_count++;
    }
    atomic_flag_clear_explicit(&device->meta_attachment_lock,
        memory_order_release);
    return result;
}

VkResult vk_ps5_device_meta_blit_resources(VkDevice device_handle,
    VkFormat format, VkFilter filter, VkBool32 source_3d,
    VkPipeline *pipeline_out, VkSampler *sampler_out)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    const uint32_t sampler_index = filter == VK_FILTER_NEAREST ? 0u :
        filter == VK_FILTER_LINEAR ? 1u : UINT32_MAX;
    if (!device || !pipeline_out || !sampler_out ||
        sampler_index == UINT32_MAX)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pipeline_out = VK_NULL_HANDLE;
    *sampler_out = VK_NULL_HANDLE;
    while (atomic_flag_test_and_set_explicit(&device->meta_attachment_lock,
            memory_order_acquire)) {}
    VkResult result = VK_SUCCESS;
    if (!device->meta_blit_samplers[sampler_index]) {
        const VkSamplerCreateInfo sampler_create = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = filter,
            .minFilter = filter,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        };
        result = vkCreateSampler(device_handle, &sampler_create, NULL,
            &device->meta_blit_samplers[sampler_index]);
    }
    VkPs5MetaBlitPipeline *entry = NULL;
    for (uint32_t index = 0u; result == VK_SUCCESS &&
         index < device->meta_blit_count; ++index) {
        if (device->meta_blits[index].format == format &&
            device->meta_blits[index].source_3d == source_3d) {
            entry = &device->meta_blits[index];
            break;
        }
    }
    if (result == VK_SUCCESS && !entry) {
        if (device->meta_blit_count >= VK_PS5_META_BLIT_PIPELINE_COUNT) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
        } else {
            entry = &device->meta_blits[device->meta_blit_count];
            result = vk_ps5_initialize_meta_blit(device_handle, format,
                source_3d, &entry->layout, &entry->pipeline);
            if (result == VK_SUCCESS) {
                entry->format = format;
                entry->source_3d = source_3d;
                device->meta_blit_count++;
            }
        }
    }
    if (result == VK_SUCCESS) {
        *pipeline_out = entry->pipeline;
        *sampler_out = device->meta_blit_samplers[sampler_index];
    }
    atomic_flag_clear_explicit(&device->meta_attachment_lock,
        memory_order_release);
    return result;
}

VkResult vk_ps5_device_meta_resolve_pipeline(VkDevice device_handle,
    VkFormat format, VkPipeline *pipeline_out)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    if (!device || !pipeline_out)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pipeline_out = VK_NULL_HANDLE;
    while (atomic_flag_test_and_set_explicit(&device->meta_attachment_lock,
            memory_order_acquire)) {}
    VkPs5MetaResolvePipeline *entry = NULL;
    for (uint32_t index = 0u; index < device->meta_resolve_count; ++index) {
        if (device->meta_resolves[index].format == format) {
            entry = &device->meta_resolves[index];
            break;
        }
    }
    VkResult result = VK_SUCCESS;
    if (!entry) {
        if (device->meta_resolve_count >=
                VK_PS5_META_RESOLVE_PIPELINE_COUNT) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
        } else {
            entry = &device->meta_resolves[device->meta_resolve_count];
            result = vk_ps5_initialize_meta_resolve(device_handle, format,
                &entry->layout, &entry->pipeline);
            if (result == VK_SUCCESS) {
                entry->format = format;
                device->meta_resolve_count++;
            }
        }
    }
    if (result == VK_SUCCESS)
        *pipeline_out = entry->pipeline;
    atomic_flag_clear_explicit(&device->meta_attachment_lock,
        memory_order_release);
    return result;
}

void *vk_ps5_instance_alloc(VkInstance instance_handle,
                            const VkAllocationCallbacks *allocator,
                            size_t size, size_t alignment,
                            VkSystemAllocationScope scope) {
    VkPs5Instance *instance = (VkPs5Instance *)instance_handle;
    const VkAllocationCallbacks *selected = allocator;
    if (!selected && instance) selected = instance_allocator(instance);
    return ps5_alloc(selected, size, alignment, scope);
}

void vk_ps5_instance_free(VkInstance instance_handle,
                          const VkAllocationCallbacks *allocator, void *ptr) {
    VkPs5Instance *instance = (VkPs5Instance *)instance_handle;
    const VkAllocationCallbacks *selected = allocator;
    if (!selected && instance) selected = instance_allocator(instance);
    ps5_free(selected, ptr);
}

VkResult vk_ps5_set_device_loader_data(VkDevice device_handle, void *object) {
    if (!device_handle || !object) return VK_ERROR_INITIALIZATION_FAILED;
    set_loader_magic_value(object);
    return VK_SUCCESS;
}

VkDevice vk_ps5_queue_device(VkQueue queue_handle) {
    VkPs5Queue *queue = (VkPs5Queue *)queue_handle;
    return queue ? (VkDevice)queue->device : VK_NULL_HANDLE;
}

VkResult vk_ps5_queue_submit_native(VkQueue queue_handle,
    uint32_t command_buffer_count,
    const AgcCommandBuffer *command_buffers)
{
    VkPs5Queue *queue = (VkPs5Queue *)queue_handle;
    AgcFence fence = NULL;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    int32_t result;
    const char *failure_stage = "create-fence";

    if (!queue || !queue->device || !queue->device->native_graphics_queue ||
        !command_buffer_count || !command_buffers)
        return VK_ERROR_INITIALIZATION_FAILED;
    const char *record_only = getenv("VULKAN_PS5_RECORD_ONLY");
    if (record_only && strcmp(record_only, "1") == 0) {
        fprintf(stderr,
            "vulkan-ps5: record-only mode skipped native submission "
            "(command_buffers=%u)\n", command_buffer_count);
        return VK_SUCCESS;
    }
    while (atomic_flag_test_and_set_explicit(
        &queue->submit_lock, memory_order_acquire)) {}
    result = agcCreateFence(queue->device->native_device,
        &fence_desc, &fence);
    if (result != AGC_OK) {
        atomic_flag_clear_explicit(&queue->submit_lock, memory_order_release);
        return result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_DEVICE_LOST;
    }
    submit.command_buffer_count = command_buffer_count;
    submit.command_buffers = command_buffers;
    failure_stage = "submit";
    result = agcQueueSubmit(queue->device->native_graphics_queue,
        &submit, fence);
    if (result == AGC_OK) {
        failure_stage = "wait";
        result = agcWaitFence(fence, UINT64_C(5000000000));
    }
    if (result == AGC_OK) {
        (void)agcDestroyFence(queue->present_ready_fence);
        queue->present_ready_fence = fence;
        fence = NULL;
    }
    if (fence) {
        int32_t destroy_result = agcDestroyFence(fence);
        if (destroy_result != AGC_OK && result == AGC_OK) {
            failure_stage = "destroy-fence";
            result = destroy_result;
        }
    }
    atomic_flag_clear_explicit(&queue->submit_lock, memory_order_release);
    if (result == AGC_OK)
        return VK_SUCCESS;
    fprintf(stderr,
        "vulkan-ps5: native queue %s failed: 0x%08x "
        "(command_buffers=%u)\n", failure_stage, (unsigned)result,
        command_buffer_count);
    if (result == AGC_ERROR_OUT_OF_MEMORY)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (result == AGC_ERROR_NOT_SUPPORTED)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    return VK_ERROR_DEVICE_LOST;
}

VkResult vk_ps5_queue_present_native(VkQueue queue_handle,
    AgcPresentChain present_chain, uint32_t image_index, uint64_t frame_id,
    uint64_t timeout_ns)
{
    VkPs5Queue *queue = (VkPs5Queue *)queue_handle;
    if (!queue || !present_chain || !queue->present_ready_fence)
        return VK_ERROR_INITIALIZATION_FAILED;
    while (atomic_flag_test_and_set_explicit(
        &queue->submit_lock, memory_order_acquire)) {}
    int32_t result = agcPresent(present_chain, image_index, frame_id,
        queue->present_ready_fence, timeout_ns);
    atomic_flag_clear_explicit(&queue->submit_lock, memory_order_release);
    if (result == AGC_OK)
        return VK_SUCCESS;
    if (result == AGC_ERROR_TIMEOUT || result == AGC_ERROR_BUSY)
        return VK_TIMEOUT;
    if (result == AGC_ERROR_INVALID_STATE)
        return VK_ERROR_INITIALIZATION_FAILED;
    return VK_ERROR_SURFACE_LOST_KHR;
}

VkResult vk_ps5_device_initialize_present_images(VkDevice device_handle,
    uint32_t image_count, const AgcImage *images)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcCommandBuffer command = NULL;
    AgcResourceTransition transitions[16];
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    uint32_t i;
    int32_t native_result;

    if (!device || !images || image_count < 2u || image_count > 16u)
        return VK_ERROR_INITIALIZATION_FAILED;
    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 512u;
    native_result = agcCreateCommandBuffer(device->native_device,
        &command_desc, &command);
    if (native_result != AGC_OK)
        return native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    native_result = agcBeginCommandBuffer(command);
    for (i = 0u; native_result == AGC_OK && i < image_count; ++i) {
        transitions[i] = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT;
        transitions[i].resource_type = kAgcResourceTypeImage;
        transitions[i].before = kAgcResourceUsageUndefined;
        transitions[i].after = kAgcResourceUsageVideoOutScanout;
        transitions[i].before_owner = kAgcResourceOwnerHost;
        transitions[i].after_owner = kAgcResourceOwnerGraphics;
        transitions[i].image = images[i];
        transitions[i].image_range =
            (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
    }
    if (native_result == AGC_OK)
        native_result = agcCmdTransitionResources(command, image_count,
            transitions);
    if (native_result == AGC_OK)
        native_result = agcEndCommandBuffer(command);
    if (native_result == AGC_OK) {
        AgcCommandBuffer submitted = command;
        result = vk_ps5_queue_submit_native((VkQueue)&device->queue,
            1u, &submitted);
    }
    if (result == VK_SUCCESS)
        (void)agcResetCommandBuffer(command);
    (void)agcDestroyCommandBuffer(command);
    return native_result == AGC_OK ? result :
        native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
}

VkDeviceSize vk_ps5_memory_size(VkDeviceMemory memory_handle) {
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    return memory ? memory->size : 0;
}

static int32_t destroy_native_object(
    VkPs5NativeObjectType type, void *object)
{
    switch (type) {
    case VK_PS5_NATIVE_BUFFER:
        return agcDestroyBuffer((AgcBuffer)object);
    case VK_PS5_NATIVE_IMAGE:
        return agcDestroyImage((AgcImage)object);
    case VK_PS5_NATIVE_IMAGE_VIEW:
        return agcDestroyImageView((AgcImageView)object);
    case VK_PS5_NATIVE_SAMPLER:
        return agcDestroySampler((AgcSampler)object);
    case VK_PS5_NATIVE_SHADER:
        return agcDestroyShader((AgcShader)object);
    case VK_PS5_NATIVE_GRAPHICS_PIPELINE:
        return agcDestroyGraphicsPipeline((AgcGraphicsPipeline)object);
    case VK_PS5_NATIVE_COMPUTE_PIPELINE:
        return agcDestroyComputePipeline((AgcComputePipeline)object);
    case VK_PS5_NATIVE_MEMORY:
        return agcDestroyMemory((AgcMemory)object);
    default:
        return AGC_ERROR_INVALID_ARGUMENT;
    }
}

void vk_ps5_destroy_or_defer_native(VkDevice device_handle,
    VkPs5NativeObjectType type, void *object)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    if (!device || !object)
        return;
    int32_t result = destroy_native_object(type, object);
    if (result != AGC_ERROR_BUSY)
        return;
    VkPs5DeferredNative *entry = ps5_alloc(device_allocator(device),
        sizeof(*entry), _Alignof(VkPs5DeferredNative),
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (!entry)
        return;
    entry->type = type;
    entry->object = object;
    while (atomic_flag_test_and_set_explicit(&device->deferred_native_lock,
            memory_order_acquire)) {}
    entry->next = device->deferred_native;
    device->deferred_native = entry;
    atomic_flag_clear_explicit(&device->deferred_native_lock,
        memory_order_release);
}

void vk_ps5_collect_deferred_native(VkDevice device_handle)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    if (!device)
        return;
    while (atomic_flag_test_and_set_explicit(&device->deferred_native_lock,
            memory_order_acquire)) {}
    VkBool32 progress;
    do {
        progress = VK_FALSE;
        VkPs5DeferredNative **link = &device->deferred_native;
        while (*link) {
            VkPs5DeferredNative *entry = *link;
            int32_t result = destroy_native_object(entry->type, entry->object);
            if (result == AGC_ERROR_BUSY) {
                link = &entry->next;
                continue;
            }
            *link = entry->next;
            ps5_free(device_allocator(device), entry);
            progress = VK_TRUE;
        }
    } while (progress && device->deferred_native);
    atomic_flag_clear_explicit(&device->deferred_native_lock,
        memory_order_release);
}

uint32_t vk_ps5_deferred_native_count(VkDevice device_handle)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    if (!device)
        return 0u;
    while (atomic_flag_test_and_set_explicit(&device->deferred_native_lock,
            memory_order_acquire)) {}
    uint32_t count = 0u;
    for (VkPs5DeferredNative *entry = device->deferred_native; entry;
         entry = entry->next)
        count++;
    atomic_flag_clear_explicit(&device->deferred_native_lock,
        memory_order_release);
    return count;
}

uint64_t vk_ps5_memory_gpu_address(
    VkDeviceMemory memory_handle, VkDeviceSize offset) {
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    if (!memory || offset > memory->size) return 0;
    AgcAllocationInfo info = AGC_ALLOCATION_INFO_INIT;
    if (agcGetObjectAllocationInfo(memory->device, AGC_OBJECT_TYPE_MEMORY,
            memory->native_memory, &info) != AGC_OK ||
        offset > UINT64_MAX - info.gpu_address)
        return 0;
    return info.gpu_address + offset;
}

AgcDevice vk_ps5_native_device(VkDevice device_handle) {
    VkPs5Device *device = (VkPs5Device *)device_handle;
    return device ? device->native_device : NULL;
}

AgcMemory vk_ps5_native_memory(VkDeviceMemory memory_handle) {
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    return memory ? memory->native_memory : NULL;
}

uint32_t vk_ps5_memory_type_index(VkDeviceMemory memory_handle) {
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    return memory ? memory->memory_type_index : UINT32_MAX;
}

VkBool32 vk_ps5_device_null_descriptor(VkDevice device_handle) {
    const VkPs5Device *device = (const VkPs5Device *)device_handle;
    return device ? device->null_descriptor : VK_FALSE;
}

static VkResult enumerate_items(uint32_t total, size_t item_size, const void *items,
                                uint32_t *count, void *properties) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    if (!properties) {
        *count = total;
        return VK_SUCCESS;
    }
    uint32_t written = *count < total ? *count : total;
    if (written) memcpy(properties, items, written * item_size);
    *count = written;
    return written < total ? VK_INCOMPLETE : VK_SUCCESS;
}

static const VkExtensionProperties instance_extensions[] = {
    { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_SURFACE_SPEC_VERSION },
    { VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
      VK_EXT_HEADLESS_SURFACE_SPEC_VERSION },
    { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, 2 },
    { VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME, 1 },
    { VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME, 1 },
    { VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME, 1 },
    { VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME, 1 },
};

static const VkExtensionProperties device_extensions[] = {
{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SWAPCHAIN_SPEC_VERSION },
{ VK_KHR_MAINTENANCE_1_EXTENSION_NAME, VK_KHR_MAINTENANCE_1_SPEC_VERSION },
{ VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
VK_KHR_CREATE_RENDERPASS_2_SPEC_VERSION },
{ VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_SPEC_VERSION },
{ VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
VK_KHR_TIMELINE_SEMAPHORE_SPEC_VERSION },
{ VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
VK_KHR_IMAGE_FORMAT_LIST_SPEC_VERSION },
{ VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_SPEC_VERSION },
{ VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
VK_KHR_INCREMENTAL_PRESENT_SPEC_VERSION },
{ VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME,
VK_EXT_LINE_RASTERIZATION_SPEC_VERSION },
{ VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
VK_EXT_SCALAR_BLOCK_LAYOUT_SPEC_VERSION },
{ VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
VK_KHR_DYNAMIC_RENDERING_SPEC_VERSION },
{ VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
VK_EXT_CUSTOM_BORDER_COLOR_SPEC_VERSION },
{ VK_EXT_BORDER_COLOR_SWIZZLE_EXTENSION_NAME,
VK_EXT_BORDER_COLOR_SWIZZLE_SPEC_VERSION },
{ VK_KHR_MAINTENANCE_5_EXTENSION_NAME, VK_KHR_MAINTENANCE_5_SPEC_VERSION },
{ VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
VK_EXT_HOST_QUERY_RESET_SPEC_VERSION },
{ VK_EXT_ROBUSTNESS_2_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_SPEC_VERSION },
{ VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
VK_EXT_DEPTH_CLIP_ENABLE_SPEC_VERSION },
    { VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
      VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_SPEC_VERSION },
    { VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
      VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_SPEC_VERSION },
    { VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
      VK_KHR_DRIVER_PROPERTIES_SPEC_VERSION },
    { VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
      VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_SPEC_VERSION },
    { VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
      VK_KHR_SHADER_FLOAT_CONTROLS_SPEC_VERSION },
};

static VkBool32 extension_supported(const char *name,
                                    const VkExtensionProperties *extensions,
                                    size_t count) {
    if (!name) return VK_FALSE;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(name, extensions[i].extensionName) == 0) return VK_TRUE;
    return VK_FALSE;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceVersion(uint32_t *pApiVersion) {
    if (!pApiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = VK_PS5_API_VERSION;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceExtensionProperties(const char *pLayerName,
                                       uint32_t *pPropertyCount,
                                       VkExtensionProperties *pProperties) {
    if (pLayerName) return VK_ERROR_LAYER_NOT_PRESENT;
    return enumerate_items((uint32_t)(sizeof(instance_extensions) /
                           sizeof(instance_extensions[0])), sizeof(*pProperties),
                           instance_extensions, pPropertyCount, pProperties);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                   VkLayerProperties *pProperties) {
    return enumerate_items(0, sizeof(*pProperties), NULL, pPropertyCount, pProperties);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
    if (!pCreateInfo || !pInstance || pCreateInfo->sType != VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pCreateInfo->enabledLayerCount) return VK_ERROR_LAYER_NOT_PRESENT;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i)
        if (!extension_supported(pCreateInfo->ppEnabledExtensionNames[i], instance_extensions,
                                 sizeof(instance_extensions) / sizeof(instance_extensions[0])))
            return VK_ERROR_EXTENSION_NOT_PRESENT;
    if (pCreateInfo->pApplicationInfo) {
        uint32_t requested = pCreateInfo->pApplicationInfo->apiVersion;
        if (VK_API_VERSION_VARIANT(requested) != 0 ||
            VK_API_VERSION_MAJOR(requested) > 1 ||
            (VK_API_VERSION_MAJOR(requested) == 1 &&
             VK_API_VERSION_MINOR(requested) > 2))
            return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    VkPs5Instance *instance = ps5_alloc(pAllocator, sizeof(*instance),
                                        _Alignof(VkPs5Instance),
                                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (!instance) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(instance, 0, sizeof(*instance));
    if (pAllocator) {
        instance->allocator = *pAllocator;
        instance->has_allocator = VK_TRUE;
    }
    instance->physical_device.instance = instance;
    set_loader_magic_value(instance);
    set_loader_magic_value(&instance->physical_device);
    *pInstance = (VkInstance)instance;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance_handle, const VkAllocationCallbacks *pAllocator) {
    VkPs5Instance *instance = (VkPs5Instance *)instance_handle;
    if (!instance) return;
    const VkAllocationCallbacks *allocator = pAllocator ? pAllocator : instance_allocator(instance);
    ps5_free(allocator, instance);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(VkInstance instance_handle, uint32_t *pPhysicalDeviceCount,
                           VkPhysicalDevice *pPhysicalDevices) {
    if (!instance_handle || !pPhysicalDeviceCount) return VK_ERROR_INITIALIZATION_FAILED;
    VkPhysicalDevice physical = (VkPhysicalDevice)&((VkPs5Instance *)instance_handle)->physical_device;
    return enumerate_items(1, sizeof(physical), &physical, pPhysicalDeviceCount, pPhysicalDevices);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
                                VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties) {
    uint32_t count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if (result != VK_SUCCESS) return result;
    if (!pPhysicalDeviceGroupCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pPhysicalDeviceGroupProperties) {
        *pPhysicalDeviceGroupCount = 1;
        return VK_SUCCESS;
    }
    if (*pPhysicalDeviceGroupCount == 0) return VK_INCOMPLETE;
    VkPhysicalDeviceGroupProperties *group = &pPhysicalDeviceGroupProperties[0];
    if (group->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES)
        return VK_ERROR_INITIALIZATION_FAILED;
    group->physicalDeviceCount = 1;
    group->physicalDevices[0] = physical;
    group->subsetAllocation = VK_FALSE;
    *pPhysicalDeviceGroupCount = 1;
    return VK_SUCCESS;
}

static void fill_properties(VkPhysicalDeviceProperties *properties) {
    AgcDeviceProperties capabilities = ps5_capabilities();
    memset(properties, 0, sizeof(*properties));
    properties->apiVersion = VK_PS5_API_VERSION;
    properties->driverVersion = VK_MAKE_VERSION(0, 1, 0);
    properties->vendorID = VK_PS5_VENDOR_ID;
    properties->deviceID = VK_PS5_DEVICE_ID;
    properties->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    strncpy(properties->deviceName, "PlayStation 5 gfx1013 (host ICD)",
            VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    memcpy(properties->pipelineCacheUUID, "VulkanPS5-gfx1013", VK_UUID_SIZE);

    VkPhysicalDeviceLimits *limits = &properties->limits;
    limits->maxImageDimension1D = capabilities.max_image_dimension_1d;
    limits->maxImageDimension2D = capabilities.max_image_dimension_2d;
    limits->maxImageDimension3D = capabilities.max_image_dimension_3d;
    limits->maxImageDimensionCube = capabilities.max_image_dimension_cube;
    limits->maxImageArrayLayers = capabilities.max_image_array_layers;
    limits->maxTexelBufferElements = 1u << 27;
    limits->maxUniformBufferRange = 65536;
    limits->maxStorageBufferRange = UINT32_MAX;
    limits->maxPushConstantsSize = 256;
    limits->maxMemoryAllocationCount = 4096;
    limits->maxSamplerAllocationCount = 4096;
    limits->bufferImageGranularity = 256;
    limits->sparseAddressSpaceSize = 0;
    limits->maxBoundDescriptorSets = 8;
    limits->maxPerStageDescriptorSamplers = 16;
    limits->maxPerStageDescriptorUniformBuffers = 12;
    limits->maxPerStageDescriptorStorageBuffers = 8;
    limits->maxPerStageDescriptorSampledImages = 16;
    limits->maxPerStageDescriptorStorageImages = 8;
    limits->maxPerStageDescriptorInputAttachments = 8;
    limits->maxPerStageResources = 128;
    limits->maxDescriptorSetSamplers = 96;
    limits->maxDescriptorSetUniformBuffers = 72;
    limits->maxDescriptorSetUniformBuffersDynamic = 16;
    limits->maxDescriptorSetStorageBuffers = 48;
    limits->maxDescriptorSetStorageBuffersDynamic = 16;
    limits->maxDescriptorSetSampledImages = 96;
    limits->maxDescriptorSetStorageImages = 48;
    limits->maxDescriptorSetInputAttachments = 48;
    limits->maxVertexInputAttributes = 32;
    limits->maxVertexInputBindings = 32;
    limits->maxVertexInputAttributeOffset = 2047;
    limits->maxVertexInputBindingStride = 2048;
    limits->maxVertexOutputComponents = 128;
    limits->maxTessellationGenerationLevel = 64;
    limits->maxTessellationPatchSize = 32;
    limits->maxTessellationControlPerVertexInputComponents = 128;
    limits->maxTessellationControlPerVertexOutputComponents = 128;
    limits->maxTessellationControlPerPatchOutputComponents = 120;
    limits->maxTessellationControlTotalOutputComponents = 4096;
    limits->maxTessellationEvaluationInputComponents = 128;
    limits->maxTessellationEvaluationOutputComponents = 128;
    limits->maxGeometryShaderInvocations = 32;
    limits->maxGeometryInputComponents = 64;
    limits->maxGeometryOutputComponents = 128;
    limits->maxGeometryOutputVertices = 256;
    limits->maxGeometryTotalOutputComponents = 1024;
    limits->maxFragmentInputComponents = 128;
    limits->maxFragmentOutputAttachments = 8;
    limits->maxFragmentDualSrcAttachments = 1;
    limits->maxFragmentCombinedOutputResources = 8;
    limits->maxComputeSharedMemorySize = capabilities.max_compute_shared_memory_size;
    limits->maxComputeWorkGroupCount[0] = 65535;
    limits->maxComputeWorkGroupCount[1] = 65535;
    limits->maxComputeWorkGroupCount[2] = 65535;
    limits->maxComputeWorkGroupInvocations = capabilities.max_compute_workgroup_invocations;
    limits->maxComputeWorkGroupSize[0] = capabilities.max_compute_workgroup_size[0];
    limits->maxComputeWorkGroupSize[1] = capabilities.max_compute_workgroup_size[1];
    limits->maxComputeWorkGroupSize[2] = capabilities.max_compute_workgroup_size[2];
    limits->subPixelPrecisionBits = 8;
    limits->subTexelPrecisionBits = 8;
    limits->mipmapPrecisionBits = 8;
    limits->maxDrawIndexedIndexValue = UINT32_MAX;
    limits->maxDrawIndirectCount = UINT32_MAX;
    limits->maxSamplerLodBias = 16.0f;
    limits->maxSamplerAnisotropy = 16.0f;
    limits->maxViewports = 16;
    limits->maxViewportDimensions[0] = capabilities.max_image_dimension_2d;
    limits->maxViewportDimensions[1] = capabilities.max_image_dimension_2d;
    limits->viewportBoundsRange[0] = -32768.0f;
    limits->viewportBoundsRange[1] = 32767.0f;
    limits->viewportSubPixelBits = 8;
    limits->minMemoryMapAlignment = 4096;
    limits->minTexelBufferOffsetAlignment = 16;
    limits->minUniformBufferOffsetAlignment = 256;
    limits->minStorageBufferOffsetAlignment = 16;
    limits->minTexelOffset = -8;
    limits->maxTexelOffset = 7;
    limits->minTexelGatherOffset = -8;
    limits->maxTexelGatherOffset = 7;
    limits->minInterpolationOffset = -0.5f;
    limits->maxInterpolationOffset = 0.5f;
    limits->subPixelInterpolationOffsetBits = 4;
    limits->maxFramebufferWidth = capabilities.max_image_dimension_2d;
    limits->maxFramebufferHeight = capabilities.max_image_dimension_2d;
    limits->maxFramebufferLayers = capabilities.max_image_array_layers;
    limits->framebufferColorSampleCounts = capabilities.color_sample_counts;
    limits->framebufferDepthSampleCounts = capabilities.depth_sample_counts;
    limits->framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits->framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits->maxColorAttachments = capabilities.max_color_targets;
    limits->sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits->sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits->sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits->sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits->storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits->maxSampleMaskWords = 1;
    limits->timestampComputeAndGraphics = VK_FALSE;
    limits->timestampPeriod = 1.0f;
    limits->maxClipDistances = 8;
    limits->maxCullDistances = 8;
    limits->maxCombinedClipAndCullDistances = 8;
    limits->discreteQueuePriorities = 2;
    limits->pointSizeRange[0] = 1.0f;
    limits->pointSizeRange[1] = 64.0f;
    limits->lineWidthRange[0] = 1.0f;
    limits->lineWidthRange[1] = 64.0f;
    limits->pointSizeGranularity = 0.125f;
    limits->lineWidthGranularity = 0.125f;
    limits->strictLines = VK_FALSE;
    limits->standardSampleLocations = VK_TRUE;
    limits->optimalBufferCopyOffsetAlignment = 4;
    limits->optimalBufferCopyRowPitchAlignment = 256;
    limits->nonCoherentAtomSize = 64;
    properties->sparseProperties.residencyStandard2DBlockShape = VK_FALSE;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                              VkPhysicalDeviceProperties *pProperties) {
    (void)physicalDevice;
    if (pProperties) fill_properties(pProperties);
}

static void fill_features(VkPhysicalDeviceFeatures *features) {
    memset(features, 0, sizeof(*features));
    features->robustBufferAccess = VK_TRUE;
    features->dualSrcBlend = VK_TRUE;
    features->alphaToOne = VK_TRUE;
    features->depthBiasClamp = VK_TRUE;
    features->depthClamp = VK_TRUE;
    features->drawIndirectFirstInstance = VK_TRUE;
    features->fragmentStoresAndAtomics = VK_TRUE;
    features->fillModeNonSolid = VK_TRUE;
    features->geometryShader = VK_TRUE;
    features->imageCubeArray = VK_TRUE;
    features->independentBlend = VK_TRUE;
    features->largePoints = VK_TRUE;
    features->logicOp = VK_TRUE;
    features->multiViewport = VK_TRUE;
    features->multiDrawIndirect = VK_TRUE;
    features->occlusionQueryPrecise = VK_TRUE;
    features->samplerAnisotropy = VK_TRUE;
    features->sampleRateShading = VK_TRUE;
    features->shaderClipDistance = VK_TRUE;
    features->shaderCullDistance = VK_TRUE;
    features->shaderImageGatherExtended = VK_TRUE;
    features->shaderStorageImageWriteWithoutFormat = VK_TRUE;
    features->tessellationShader = VK_TRUE;
    features->vertexPipelineStoresAndAtomics = VK_TRUE;
    features->wideLines = VK_TRUE;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures) {
    (void)physicalDevice;
    if (pFeatures) fill_features(pFeatures);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceMemoryProperties *pMemoryProperties) {
    AgcDeviceProperties capabilities = ps5_capabilities();
    (void)physicalDevice;
    if (!pMemoryProperties) return;
    memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));
    pMemoryProperties->memoryTypeCount = capabilities.memory_heap_count;
    pMemoryProperties->memoryHeapCount = capabilities.memory_heap_count;
    for (uint32_t i = 0; i < capabilities.memory_heap_count; ++i) {
        pMemoryProperties->memoryTypes[i].propertyFlags =
            ps5_memory_flags(capabilities.memory_heaps[i].property_flags);
        pMemoryProperties->memoryTypes[i].heapIndex = i;
        pMemoryProperties->memoryHeaps[i].size = capabilities.memory_heaps[i].size;
        pMemoryProperties->memoryHeaps[i].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                         uint32_t *pQueueFamilyPropertyCount,
                                         VkQueueFamilyProperties *pQueueFamilyProperties) {
    (void)physicalDevice;
    if (!pQueueFamilyPropertyCount) return;
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = 1;
        return;
    }
    if (*pQueueFamilyPropertyCount == 0) return;
    memset(&pQueueFamilyProperties[0], 0, sizeof(pQueueFamilyProperties[0]));
    pQueueFamilyProperties[0].queueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueCount = 1;
    pQueueFamilyProperties[0].timestampValidBits = 0;
    pQueueFamilyProperties[0].minImageTransferGranularity = (VkExtent3D){1, 1, 1};
    *pQueueFamilyPropertyCount = 1;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                    VkFormatProperties *pFormatProperties) {
    AgcDeviceProperties capabilities = ps5_capabilities();
    int color_index = ps5_color_format(format);
    int depth_index = ps5_depth_format(format);
    int bc_format = ps5_bc_format(format);
    const int sampled_only = format == VK_FORMAT_R4G4_UNORM_PACK8 ||
        format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    (void)physicalDevice;
    if (!pFormatProperties) return;
    memset(pFormatProperties, 0, sizeof(*pFormatProperties));
    if (!bc_format && !sampled_only &&
        (color_index < 0 || !(capabilities.color_target_format_mask &
                              (1ull << (uint32_t)color_index))) &&
        (depth_index < 0 || !(capabilities.depth_stencil_format_mask &
                              (1u << (uint32_t)depth_index))))
        return;

    const VkFormatFeatureFlags transfer =
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const VkFormatFeatureFlags sampled_nearest =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    const VkFormatFeatureFlags sampled = sampled_nearest |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const VkFormatFeatureFlags blit_source = VK_FORMAT_FEATURE_BLIT_SRC_BIT;
    const VkFormatFeatureFlags color = sampled | transfer | blit_source |
        VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    const VkFormatFeatureFlags storage_color = color |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    const VkFormatFeatureFlags integer_color = sampled_nearest | transfer |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    const VkFormatFeatureFlags depth = transfer |
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    switch (format) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC2_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC4_UNORM_BLOCK:
    case VK_FORMAT_BC4_SNORM_BLOCK:
    case VK_FORMAT_BC5_UNORM_BLOCK:
    case VK_FORMAT_BC5_SNORM_BLOCK:
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        pFormatProperties->linearTilingFeatures = sampled | transfer |
            blit_source;
        pFormatProperties->optimalTilingFeatures = sampled | transfer |
            blit_source;
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        pFormatProperties->linearTilingFeatures = storage_color;
        pFormatProperties->optimalTilingFeatures = color;
        pFormatProperties->bufferFeatures =
            VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT |
            VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
        break;
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8G8_UNORM:
        pFormatProperties->linearTilingFeatures = storage_color;
        pFormatProperties->optimalTilingFeatures = storage_color;
        pFormatProperties->bufferFeatures =
            VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT |
            VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
        break;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        pFormatProperties->linearTilingFeatures = color;
        pFormatProperties->optimalTilingFeatures = color;
        pFormatProperties->bufferFeatures =
            VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT |
            VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
        break;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    case VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT:
        pFormatProperties->linearTilingFeatures = color;
        pFormatProperties->optimalTilingFeatures = color;
        break;
    case VK_FORMAT_R4G4_UNORM_PACK8:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        pFormatProperties->linearTilingFeatures = sampled | transfer |
            blit_source;
        pFormatProperties->optimalTilingFeatures = sampled | transfer |
            blit_source;
        break;
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_SNORM:
    case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
        pFormatProperties->linearTilingFeatures = storage_color;
        pFormatProperties->optimalTilingFeatures = storage_color;
        break;
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_B8G8R8A8_SRGB:
        pFormatProperties->linearTilingFeatures = color;
        pFormatProperties->optimalTilingFeatures = color;
        break;
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
        pFormatProperties->linearTilingFeatures = integer_color;
        pFormatProperties->optimalTilingFeatures = integer_color;
        break;
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_S8_UINT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        pFormatProperties->linearTilingFeatures = depth;
        pFormatProperties->optimalTilingFeatures = depth;
        break;
    default:
        break;
    }
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                         VkImageType type, VkImageTiling tiling,
                                         VkImageUsageFlags usage, VkImageCreateFlags flags,
                                         VkImageFormatProperties *pImageFormatProperties) {
    AgcDeviceProperties capabilities = ps5_capabilities();
    (void)physicalDevice;
    if (!pImageFormatProperties) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pImageFormatProperties, 0, sizeof(*pImageFormatProperties));
    if (type < VK_IMAGE_TYPE_1D || type > VK_IMAGE_TYPE_3D ||
        (ps5_bc_format(format) && type == VK_IMAGE_TYPE_3D) ||
        (flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                  VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                  VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
                  VK_IMAGE_CREATE_PROTECTED_BIT)))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
    VkFormatFeatureFlags supported = tiling == VK_IMAGE_TILING_LINEAR ?
        properties.linearTilingFeatures : properties.optimalTilingFeatures;
    if (!supported) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) &&
        !(supported & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        !(supported & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
        !(supported & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
        !(supported & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
        !(supported & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
        !(supported & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
        if (type != VK_IMAGE_TYPE_2D ||
            (!ps5_bc_format(format) &&
             format != VK_FORMAT_R8G8B8A8_UNORM &&
             format != VK_FORMAT_B8G8R8A8_UNORM) ||
            !(usage & VK_IMAGE_USAGE_SAMPLED_BIT))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    pImageFormatProperties->maxExtent.width = type == VK_IMAGE_TYPE_3D ?
        capabilities.max_image_dimension_3d : capabilities.max_image_dimension_2d;
    pImageFormatProperties->maxExtent.height = type == VK_IMAGE_TYPE_1D ? 1 :
        capabilities.max_image_dimension_2d;
    pImageFormatProperties->maxExtent.depth = type == VK_IMAGE_TYPE_3D ?
        capabilities.max_image_dimension_3d : 1;
    uint32_t max_dimension = pImageFormatProperties->maxExtent.width;
    if (pImageFormatProperties->maxExtent.height > max_dimension)
        max_dimension = pImageFormatProperties->maxExtent.height;
    if (pImageFormatProperties->maxExtent.depth > max_dimension)
        max_dimension = pImageFormatProperties->maxExtent.depth;
    pImageFormatProperties->maxMipLevels = 1;
    while (max_dimension > 1u &&
           pImageFormatProperties->maxMipLevels < 15u) {
        max_dimension >>= 1u;
        ++pImageFormatProperties->maxMipLevels;
    }
    pImageFormatProperties->maxArrayLayers = type == VK_IMAGE_TYPE_3D ? 1 :
        capabilities.max_image_array_layers;
    pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    if (format == VK_FORMAT_R8G8B8A8_UNORM && type == VK_IMAGE_TYPE_2D &&
        tiling == VK_IMAGE_TILING_OPTIMAL &&
        (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        !(usage & ~(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)))
        pImageFormatProperties->sampleCounts |= VK_SAMPLE_COUNT_4_BIT;
    pImageFormatProperties->maxResourceSize = capabilities.memory_heaps[0].size;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling,
    uint32_t *pPropertyCount, VkSparseImageFormatProperties *pProperties) {
    (void)physicalDevice; (void)format; (void)type; (void)samples;
    (void)usage; (void)tiling; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                     uint32_t *pPropertyCount,
                                     VkExtensionProperties *pProperties) {
    (void)physicalDevice;
    if (pLayerName) return VK_ERROR_LAYER_NOT_PRESENT;
    return enumerate_items((uint32_t)(sizeof(device_extensions) /
                           sizeof(device_extensions[0])), sizeof(*pProperties),
                           device_extensions, pPropertyCount, pProperties);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                               VkPhysicalDeviceProperties2 *pProperties) {
    AgcDeviceProperties capabilities = ps5_capabilities();
    if (!pProperties) return;
    fill_properties(&pProperties->properties);
    for (VkBaseOutStructure *next = (VkBaseOutStructure *)pProperties->pNext;
         next; next = next->pNext) {
        switch (next->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
            VkPhysicalDeviceIDProperties *id = (VkPhysicalDeviceIDProperties *)next;
            memset(id->deviceUUID, 0, VK_UUID_SIZE);
            memset(id->driverUUID, 0, VK_UUID_SIZE);
            memcpy(id->deviceUUID, "PS5-gfx1013-dev", VK_UUID_SIZE);
            memcpy(id->driverUUID, "VulkanPS5-driver", VK_UUID_SIZE);
            memset(id->deviceLUID, 0, VK_LUID_SIZE);
            id->deviceLUIDValid = VK_FALSE;
            id->deviceNodeMask = 0;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
            VkPhysicalDeviceSubgroupProperties *subgroup =
                (VkPhysicalDeviceSubgroupProperties *)next;
            subgroup->subgroupSize = capabilities.subgroup_size;
            subgroup->supportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
            subgroup->supportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
            subgroup->quadOperationsInAllStages = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES:
            ((VkPhysicalDevicePointClippingProperties *)next)->pointClippingBehavior =
                VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
            VkPhysicalDeviceMultiviewProperties *multiview =
                (VkPhysicalDeviceMultiviewProperties *)next;
            multiview->maxMultiviewViewCount = 0;
            multiview->maxMultiviewInstanceIndex = 0;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES:
            ((VkPhysicalDeviceProtectedMemoryProperties *)next)->protectedNoFault = VK_FALSE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
            VkPhysicalDeviceMaintenance3Properties *maintenance =
                (VkPhysicalDeviceMaintenance3Properties *)next;
            maintenance->maxPerSetDescriptors = 1024;
            maintenance->maxMemoryAllocationSize = capabilities.memory_heaps[1].size;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT:
            ((VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)next)
                ->maxVertexAttribDivisor = UINT32_MAX;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES: {
            VkPhysicalDeviceVertexAttributeDivisorProperties *divisor =
                (VkPhysicalDeviceVertexAttributeDivisorProperties *)next;
            divisor->maxVertexAttribDivisor = UINT32_MAX;
            divisor->supportsNonZeroFirstInstance = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
            VkPhysicalDeviceDriverProperties *driver =
                (VkPhysicalDeviceDriverProperties *)next;
            void *saved_next = driver->pNext;
            memset(driver, 0, sizeof(*driver));
            driver->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            driver->pNext = saved_next;
            driver->driverID = VK_DRIVER_ID_MESA_RADV;
            strncpy(driver->driverName, "Vulkan-PS5",
                    VK_MAX_DRIVER_NAME_SIZE - 1);
            strncpy(driver->driverInfo,
                    "OpenAGC gfx1013 experimental Vulkan ICD",
                    VK_MAX_DRIVER_INFO_SIZE - 1);
            driver->conformanceVersion = (VkConformanceVersion){0, 0, 0, 0};
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES: {
            VkPhysicalDeviceFloatControlsProperties *controls =
                (VkPhysicalDeviceFloatControlsProperties *)next;
            void *saved_next = controls->pNext;
            memset(controls, 0, sizeof(*controls));
            controls->sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES;
            controls->pNext = saved_next;
            controls->denormBehaviorIndependence =
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
            controls->roundingModeIndependence =
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
            VkPhysicalDeviceVulkan11Properties *v11 =
                (VkPhysicalDeviceVulkan11Properties *)next;
            memcpy(v11->deviceUUID, "PS5-gfx1013-dev", VK_UUID_SIZE);
            memcpy(v11->driverUUID, "VulkanPS5-driver", VK_UUID_SIZE);
            memset(v11->deviceLUID, 0, VK_LUID_SIZE);
            v11->deviceNodeMask = 0;
            v11->deviceLUIDValid = VK_FALSE;
            v11->subgroupSize = capabilities.subgroup_size;
            v11->subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
            v11->subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
            v11->subgroupQuadOperationsInAllStages = VK_FALSE;
            v11->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
            v11->maxMultiviewViewCount = 0;
            v11->maxMultiviewInstanceIndex = 0;
            v11->protectedNoFault = VK_FALSE;
            v11->maxPerSetDescriptors = 1024;
            v11->maxMemoryAllocationSize = capabilities.memory_heaps[1].size;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: {
            VkPhysicalDeviceVulkan12Properties *v12 =
                (VkPhysicalDeviceVulkan12Properties *)next;
            void *saved_next = v12->pNext;
            memset(v12, 0, sizeof(*v12));
            v12->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
            v12->pNext = saved_next;
            v12->driverID = VK_DRIVER_ID_MESA_RADV;
            strncpy(v12->driverName, "Vulkan-PS5",
                VK_MAX_DRIVER_NAME_SIZE - 1u);
            strncpy(v12->driverInfo,
                "OpenAGC gfx1013 experimental Vulkan ICD",
                VK_MAX_DRIVER_INFO_SIZE - 1u);
            v12->denormBehaviorIndependence =
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
            v12->roundingModeIndependence =
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
            v12->maxTimelineSemaphoreValueDifference = UINT64_MAX;
            v12->framebufferIntegerColorSampleCounts =
                VK_SAMPLE_COUNT_1_BIT;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES: {
            VkPhysicalDeviceTimelineSemaphoreProperties *timeline =
                (VkPhysicalDeviceTimelineSemaphoreProperties *)next;
            timeline->maxTimelineSemaphoreValueDifference = UINT64_MAX;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES: {
            VkPhysicalDeviceLineRasterizationProperties *line =
                (VkPhysicalDeviceLineRasterizationProperties *)next;
            line->lineSubPixelPrecisionBits = 8u;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT:
            ((VkPhysicalDeviceCustomBorderColorPropertiesEXT *)next)
                ->maxCustomBorderColorSamplers = 64u;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES: {
            VkPhysicalDeviceMaintenance5Properties *maintenance5 =
                (VkPhysicalDeviceMaintenance5Properties *)next;
            maintenance5->earlyFragmentMultisampleCoverageAfterSampleCounting =
                VK_FALSE;
            maintenance5->earlyFragmentSampleMaskTestBeforeSampleCounting =
                VK_FALSE;
            maintenance5->depthStencilSwizzleOneSupport = VK_FALSE;
            maintenance5->polygonModePointSize = VK_FALSE;
            maintenance5->nonStrictSinglePixelWideLinesUseParallelogram =
                VK_FALSE;
            maintenance5->nonStrictWideLinesUseParallelogram = VK_FALSE;
            break;
        }
        default:
            break;
        }
    }
    (void)physicalDevice;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                             VkPhysicalDeviceFeatures2 *pFeatures) {
    (void)physicalDevice;
    if (!pFeatures) return;
    fill_features(&pFeatures->features);
    for (VkBaseOutStructure *next = (VkBaseOutStructure *)pFeatures->pNext;
         next; next = next->pNext) {
        switch (next->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
            VkPhysicalDevice16BitStorageFeatures *f =
                (VkPhysicalDevice16BitStorageFeatures *)next;
            f->storageBuffer16BitAccess = VK_FALSE;
            f->uniformAndStorageBuffer16BitAccess = VK_FALSE;
            f->storagePushConstant16 = VK_FALSE;
            f->storageInputOutput16 = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
            VkPhysicalDeviceMultiviewFeatures *f = (VkPhysicalDeviceMultiviewFeatures *)next;
            f->multiview = VK_FALSE;
            f->multiviewGeometryShader = VK_FALSE;
            f->multiviewTessellationShader = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
            VkPhysicalDeviceVariablePointersFeatures *f =
                (VkPhysicalDeviceVariablePointersFeatures *)next;
            f->variablePointersStorageBuffer = VK_TRUE;
            f->variablePointers = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES:
            ((VkPhysicalDeviceProtectedMemoryFeatures *)next)->protectedMemory = VK_FALSE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES:
            ((VkPhysicalDeviceSamplerYcbcrConversionFeatures *)next)->samplerYcbcrConversion =
                VK_FALSE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES:
            ((VkPhysicalDeviceShaderDrawParametersFeatures *)next)->shaderDrawParameters =
                VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES:
            ((VkPhysicalDeviceHostQueryResetFeatures *)next)->hostQueryReset =
                VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            ((VkPhysicalDeviceTimelineSemaphoreFeatures *)next)
                ->timelineSemaphore = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
            ((VkPhysicalDeviceScalarBlockLayoutFeatures *)next)
                ->scalarBlockLayout = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
            ((VkPhysicalDeviceDynamicRenderingFeatures *)next)
                ->dynamicRendering = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
            VkPhysicalDeviceCustomBorderColorFeaturesEXT *border =
                (VkPhysicalDeviceCustomBorderColorFeaturesEXT *)next;
            border->customBorderColors = VK_TRUE;
            border->customBorderColorWithoutFormat = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: {
            VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *swizzle =
                (VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *)next;
            swizzle->borderColorSwizzle = VK_FALSE;
            swizzle->borderColorSwizzleFromImage = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES:
            ((VkPhysicalDeviceMaintenance5Features *)next)->maintenance5 =
                VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
            VkPhysicalDeviceRobustness2FeaturesEXT *robustness2 =
                (VkPhysicalDeviceRobustness2FeaturesEXT *)next;
            robustness2->robustBufferAccess2 = VK_FALSE;
            robustness2->robustImageAccess2 = VK_FALSE;
            robustness2->nullDescriptor = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT:
            ((VkPhysicalDeviceDepthClipEnableFeaturesEXT *)next)
                ->depthClipEnable = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES: {
            VkPhysicalDeviceLineRasterizationFeatures *line =
                (VkPhysicalDeviceLineRasterizationFeatures *)next;
            line->rectangularLines = VK_TRUE;
            line->bresenhamLines = VK_FALSE;
            line->smoothLines = VK_FALSE;
            line->stippledRectangularLines = VK_FALSE;
            line->stippledBresenhamLines = VK_FALSE;
            line->stippledSmoothLines = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT:
            ((VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT *)next)
                ->shaderDemoteToHelperInvocation = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES: {
            VkPhysicalDeviceVertexAttributeDivisorFeatures *divisor =
                (VkPhysicalDeviceVertexAttributeDivisorFeatures *)next;
            divisor->vertexAttributeInstanceRateDivisor = VK_TRUE;
            divisor->vertexAttributeInstanceRateZeroDivisor = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            VkPhysicalDeviceVulkan12Features *f =
                (VkPhysicalDeviceVulkan12Features *)next;
            memset(&f->samplerMirrorClampToEdge, 0,
                sizeof(*f) - offsetof(VkPhysicalDeviceVulkan12Features,
                    samplerMirrorClampToEdge));
            f->samplerMirrorClampToEdge = VK_TRUE;
            f->scalarBlockLayout = VK_TRUE;
            f->hostQueryReset = VK_TRUE;
            f->timelineSemaphore = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            VkPhysicalDeviceVulkan11Features *f = (VkPhysicalDeviceVulkan11Features *)next;
            f->storageBuffer16BitAccess = VK_FALSE;
            f->uniformAndStorageBuffer16BitAccess = VK_FALSE;
            f->storagePushConstant16 = VK_FALSE;
            f->storageInputOutput16 = VK_FALSE;
            f->multiview = VK_FALSE;
            f->multiviewGeometryShader = VK_FALSE;
            f->multiviewTessellationShader = VK_FALSE;
            f->variablePointersStorageBuffer = VK_TRUE;
            f->variablePointers = VK_TRUE;
            f->protectedMemory = VK_FALSE;
            f->samplerYcbcrConversion = VK_FALSE;
            f->shaderDrawParameters = VK_TRUE;
            break;
        }
        default:
            break;
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                     VkPhysicalDeviceMemoryProperties2 *pMemoryProperties) {
    if (pMemoryProperties)
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &pMemoryProperties->memoryProperties);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice,
                                          uint32_t *pCount,
                                          VkQueueFamilyProperties2 *pProperties) {
    if (!pCount) return;
    if (!pProperties) {
        *pCount = 1;
        return;
    }
    if (*pCount == 0) return;
    uint32_t count = 1;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count,
                                             &pProperties[0].queueFamilyProperties);
    *pCount = 1;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                     VkFormatProperties2 *pFormatProperties) {
    if (pFormatProperties)
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format,
                                            &pFormatProperties->formatProperties);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
    VkImageFormatProperties2 *pImageFormatProperties) {
    if (!pImageFormatInfo || !pImageFormatProperties ||
        pImageFormatInfo->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 ||
        pImageFormatProperties->sType != VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)pImageFormatInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO &&
            ((const VkPhysicalDeviceExternalImageFormatInfo *)next)->handleType != 0)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    VkResult result = vkGetPhysicalDeviceImageFormatProperties(
        physicalDevice, pImageFormatInfo->format, pImageFormatInfo->type,
        pImageFormatInfo->tiling, pImageFormatInfo->usage, pImageFormatInfo->flags,
        &pImageFormatProperties->imageFormatProperties);
    if (result != VK_SUCCESS) return result;
    for (VkBaseOutStructure *next = (VkBaseOutStructure *)pImageFormatProperties->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES) {
            VkExternalImageFormatProperties *external =
                (VkExternalImageFormatProperties *)next;
            memset(&external->externalMemoryProperties, 0,
                   sizeof(external->externalMemoryProperties));
        } else if (next->sType ==
                   VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES) {
            ((VkSamplerYcbcrConversionImageFormatProperties *)next)
                ->combinedImageSamplerDescriptorCount = 1;
        }
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
    uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties) {
    (void)physicalDevice; (void)pFormatInfo; (void)pProperties;
    if (pPropertyCount) *pPropertyCount = 0;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
    VkExternalBufferProperties *pExternalBufferProperties) {
    (void)physicalDevice; (void)pExternalBufferInfo;
    if (pExternalBufferProperties)
        memset(&pExternalBufferProperties->externalMemoryProperties, 0,
               sizeof(pExternalBufferProperties->externalMemoryProperties));
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
    VkExternalFenceProperties *pExternalFenceProperties) {
    (void)physicalDevice; (void)pExternalFenceInfo;
    if (!pExternalFenceProperties) return;
    pExternalFenceProperties->exportFromImportedHandleTypes = 0;
    pExternalFenceProperties->compatibleHandleTypes = 0;
    pExternalFenceProperties->externalFenceFeatures = 0;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties *pExternalSemaphoreProperties) {
    (void)physicalDevice; (void)pExternalSemaphoreInfo;
    if (!pExternalSemaphoreProperties) return;
    pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
    pExternalSemaphoreProperties->compatibleHandleTypes = 0;
    pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
}

static VkBool32 unsupported_features_requested(const VkPhysicalDeviceFeatures *features) {
    if (!features) return VK_FALSE;
    VkPhysicalDeviceFeatures supported;
    fill_features(&supported);
    const VkBool32 *requested_bits = (const VkBool32 *)features;
    const VkBool32 *supported_bits = (const VkBool32 *)&supported;
    for (size_t i = 0; i < sizeof(*features) / sizeof(*requested_bits); ++i)
        if (requested_bits[i] && !supported_bits[i]) return VK_TRUE;
    return VK_FALSE;
}

static VkBool32 unsupported_device_features_requested(const void *pNext) {
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pNext;
         next; next = next->pNext) {
        switch (next->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
            if (unsupported_features_requested(
                    &((const VkPhysicalDeviceFeatures2 *)next)->features))
                return VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
            const VkPhysicalDevice16BitStorageFeatures *f =
                (const VkPhysicalDevice16BitStorageFeatures *)next;
            if (f->storageBuffer16BitAccess || f->uniformAndStorageBuffer16BitAccess ||
                f->storagePushConstant16 || f->storageInputOutput16) return VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
            const VkPhysicalDeviceMultiviewFeatures *f =
                (const VkPhysicalDeviceMultiviewFeatures *)next;
            if (f->multiview || f->multiviewGeometryShader ||
                f->multiviewTessellationShader) return VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES:
            if (((const VkPhysicalDeviceProtectedMemoryFeatures *)next)->protectedMemory)
                return VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES:
            if (((const VkPhysicalDeviceSamplerYcbcrConversionFeatures *)next)
                    ->samplerYcbcrConversion)
                return VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: {
            const VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *swizzle =
                (const VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *)next;
            if (swizzle->borderColorSwizzle)
                return VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
            const VkPhysicalDeviceRobustness2FeaturesEXT *robustness2 =
                (const VkPhysicalDeviceRobustness2FeaturesEXT *)next;
            if (robustness2->robustBufferAccess2 ||
                robustness2->robustImageAccess2)
                return VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES: {
            const VkPhysicalDeviceLineRasterizationFeatures *line =
                (const VkPhysicalDeviceLineRasterizationFeatures *)next;
            if (line->bresenhamLines || line->smoothLines ||
                line->stippledRectangularLines ||
                line->stippledBresenhamLines || line->stippledSmoothLines)
                return VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES: {
            const VkPhysicalDeviceVertexAttributeDivisorFeatures *f =
                (const VkPhysicalDeviceVertexAttributeDivisorFeatures *)next;
            if (f->vertexAttributeInstanceRateZeroDivisor)
                return VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            const VkPhysicalDeviceVulkan12Features *requested =
                (const VkPhysicalDeviceVulkan12Features *)next;
            VkPhysicalDeviceVulkan12Features supported = {0};
            supported.samplerMirrorClampToEdge = VK_TRUE;
            supported.scalarBlockLayout = VK_TRUE;
            supported.hostQueryReset = VK_TRUE;
            supported.timelineSemaphore = VK_TRUE;
            const VkBool32 *request_bits =
                &requested->samplerMirrorClampToEdge;
            const VkBool32 *supported_bits =
                &supported.samplerMirrorClampToEdge;
            const size_t count = (sizeof(*requested) -
                offsetof(VkPhysicalDeviceVulkan12Features,
                    samplerMirrorClampToEdge)) / sizeof(VkBool32);
            for (size_t i = 0; i < count; ++i)
                if (request_bits[i] && !supported_bits[i])
                    return VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            const VkPhysicalDeviceVulkan11Features *f =
                (const VkPhysicalDeviceVulkan11Features *)next;
            if (f->storageBuffer16BitAccess || f->uniformAndStorageBuffer16BitAccess ||
                f->storagePushConstant16 || f->storageInputOutput16 || f->multiview ||
                f->multiviewGeometryShader || f->multiviewTessellationShader ||
                f->protectedMemory || f->samplerYcbcrConversion)
                return VK_TRUE;
            break;
        }
        default:
            break;
        }
    }
    return VK_FALSE;
}

static VkResult validate_device_create_chain(VkPhysicalDevice physicalDevice,
                                             const void *pNext) {
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO) {
            const VkDeviceGroupDeviceCreateInfo *group =
                (const VkDeviceGroupDeviceCreateInfo *)next;
            if (group->physicalDeviceCount != 1 || !group->pPhysicalDevices ||
                group->pPhysicalDevices[0] != physicalDevice)
                return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    return VK_SUCCESS;
}

static VkBool32 robust_buffer_access_requested(
    const VkDeviceCreateInfo *create_info)
{
    if (create_info->pEnabledFeatures &&
        create_info->pEnabledFeatures->robustBufferAccess)
        return VK_TRUE;
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)create_info->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 &&
            ((const VkPhysicalDeviceFeatures2 *)next)->features.robustBufferAccess)
            return VK_TRUE;
    }
    return VK_FALSE;
}

static VkBool32 null_descriptor_requested(
    const VkDeviceCreateInfo *create_info)
{
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)create_info->pNext;
         next; next = next->pNext) {
        if (next->sType ==
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT)
            return ((const VkPhysicalDeviceRobustness2FeaturesEXT *)next)
                ->nullDescriptor;
    }
    return VK_FALSE;
}

static VkBool32 depth_clip_enable_requested(
    const VkDeviceCreateInfo *create_info)
{
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)create_info->pNext;
         next; next = next->pNext) {
        if (next->sType ==
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT)
            return ((const VkPhysicalDeviceDepthClipEnableFeaturesEXT *)next)
                ->depthClipEnable;
    }
    return VK_FALSE;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    if (!physicalDevice || !pCreateInfo || !pDevice ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pCreateInfo->enabledLayerCount) return VK_ERROR_LAYER_NOT_PRESENT;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        if (!extension_supported(pCreateInfo->ppEnabledExtensionNames[i],
                device_extensions,
                sizeof(device_extensions) / sizeof(device_extensions[0])))
            return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    if (unsupported_features_requested(pCreateInfo->pEnabledFeatures) ||
        unsupported_device_features_requested(pCreateInfo->pNext))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    VkResult chain_result = validate_device_create_chain(physicalDevice, pCreateInfo->pNext);
    if (chain_result != VK_SUCCESS) return chain_result;
    if (pCreateInfo->queueCreateInfoCount != 1) return VK_ERROR_INITIALIZATION_FAILED;
    const VkDeviceQueueCreateInfo *queue = &pCreateInfo->pQueueCreateInfos[0];
    if (queue->sType != VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO ||
        queue->queueFamilyIndex != 0 || queue->queueCount != 1 || !queue->pQueuePriorities ||
        queue->pQueuePriorities[0] < 0.0f || queue->pQueuePriorities[0] > 1.0f)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkPs5Device *device = ps5_alloc(pAllocator, sizeof(*device), _Alignof(VkPs5Device),
                                    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
    if (!device) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(device, 0, sizeof(*device));
    if (pAllocator) {
        device->allocator = *pAllocator;
        device->has_allocator = VK_TRUE;
    }
    device->physical_device = (VkPs5PhysicalDevice *)physicalDevice;
    device->robust_buffer_access = robust_buffer_access_requested(pCreateInfo);
    device->null_descriptor = null_descriptor_requested(pCreateInfo);
    device->depth_clip_enable = depth_clip_enable_requested(pCreateInfo);
    device->queue.device = device;
    atomic_init(&device->memory_allocation_count, 0);
    atomic_flag_clear(&device->deferred_native_lock);
    atomic_flag_clear(&device->meta_attachment_lock);
    atomic_flag_clear(&device->queue.submit_lock);
    AgcDeviceDesc native_device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc native_queue_desc = AGC_QUEUE_DESC_INIT;
    native_device_desc.required_capability_bits = AGC_RUNTIME_CAP_BASELINE;
    int32_t native_result = agcCreateDevice(
        &native_device_desc, &device->native_device);
    if (native_result == AGC_OK) {
        AgcDebugCallbackDesc debug_desc = AGC_DEBUG_CALLBACK_DESC_INIT;
        debug_desc.severity_mask = AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT;
        debug_desc.category_mask = AGC_DEBUG_MESSAGE_CATEGORY_ALL;
        debug_desc.callback = vk_ps5_native_debug_message;
        native_result = agcSetDebugCallback(
            device->native_device, &debug_desc);
    }
    if (native_result == AGC_OK)
        native_result = agcCreateQueue(device->native_device,
            &native_queue_desc, &device->native_graphics_queue);
    native_queue_desc.type = kAgcQueueCompute;
    if (native_result == AGC_OK)
        native_result = agcCreateQueue(device->native_device,
            &native_queue_desc, &device->native_compute_queue);
    if (native_result == AGC_OK) {
        AgcFenceDesc present_fence_desc = AGC_FENCE_DESC_INIT;
        present_fence_desc.signaled = 1u;
        native_result = agcCreateFence(device->native_device,
            &present_fence_desc, &device->queue.present_ready_fence);
    }
    if (native_result == AGC_OK) {
        VkResult meta_result = vk_ps5_initialize_meta_clear(
            (VkDevice)device, &device->meta_clear_layout,
            &device->meta_clear_pipeline);
        if (meta_result != VK_SUCCESS)
            native_result = meta_result == VK_ERROR_OUT_OF_HOST_MEMORY ||
                meta_result == VK_ERROR_OUT_OF_DEVICE_MEMORY ?
                AGC_ERROR_OUT_OF_MEMORY : AGC_ERROR_INTERNAL;
    }
    if (native_result != AGC_OK) {
        if (device->meta_clear_pipeline)
            vkDestroyPipeline((VkDevice)device,
                device->meta_clear_pipeline, NULL);
        if (device->meta_clear_layout)
            vkDestroyPipelineLayout((VkDevice)device,
                device->meta_clear_layout, NULL);
        if (device->queue.present_ready_fence)
            (void)agcDestroyFence(device->queue.present_ready_fence);
        if (device->native_compute_queue)
            (void)agcDestroyQueue(device->native_compute_queue);
        if (device->native_graphics_queue)
            (void)agcDestroyQueue(device->native_graphics_queue);
        if (device->native_device)
            (void)agcDestroyDevice(device->native_device);
        ps5_free(pAllocator, device);
        return native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    }
    set_loader_magic_value(device);
    set_loader_magic_value(&device->queue);
    *pDevice = (VkDevice)device;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device_handle, const VkAllocationCallbacks *pAllocator) {
    VkPs5Device *device = (VkPs5Device *)device_handle;
    if (!device) return;
    const VkAllocationCallbacks *allocator = pAllocator ? pAllocator : device_allocator(device);
    vk_ps5_collect_deferred_native(device_handle);
    for (uint32_t index = 0u; index < device->meta_attachment_count; ++index) {
        vkDestroyPipeline(device_handle,
            device->meta_attachments[index].pipeline, NULL);
        vkDestroyPipelineLayout(device_handle,
            device->meta_attachments[index].layout, NULL);
    }
    for (uint32_t index = 0u; index < device->meta_blit_count; ++index) {
        vkDestroyPipeline(device_handle, device->meta_blits[index].pipeline,
            NULL);
        vkDestroyPipelineLayout(device_handle,
            device->meta_blits[index].layout, NULL);
    }
    vkDestroySampler(device_handle, device->meta_blit_samplers[0], NULL);
    vkDestroySampler(device_handle, device->meta_blit_samplers[1], NULL);
    for (uint32_t index = 0u; index < device->meta_resolve_count; ++index) {
        vkDestroyPipeline(device_handle,
            device->meta_resolves[index].pipeline, NULL);
        vkDestroyPipelineLayout(device_handle,
            device->meta_resolves[index].layout, NULL);
    }
    vkDestroyPipeline(device_handle, device->meta_clear_pipeline, NULL);
    vkDestroyPipelineLayout(device_handle, device->meta_clear_layout, NULL);
    vk_ps5_collect_deferred_native(device_handle);
    (void)agcDestroyFence(device->queue.present_ready_fence);
    (void)agcDestroyQueue(device->native_compute_queue);
    (void)agcDestroyQueue(device->native_graphics_queue);
    (void)agcDestroyDevice(device->native_device);
    ps5_free(allocator, device);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceQueue(VkDevice device_handle, uint32_t queueFamilyIndex, uint32_t queueIndex,
                 VkQueue *pQueue) {
    if (!pQueue) return;
    if (!device_handle || queueFamilyIndex != 0 || queueIndex != 0) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }
    *pQueue = (VkQueue)&((VkPs5Device *)device_handle)->queue;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2 *pQueueInfo, VkQueue *pQueue) {
    if (!pQueue) return;
    if (!pQueueInfo || pQueueInfo->sType != VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2 ||
        pQueueInfo->flags != 0) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }
    vkGetDeviceQueue(device, pQueueInfo->queueFamilyIndex, pQueueInfo->queueIndex, pQueue);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex,
                                   uint32_t localDeviceIndex, uint32_t remoteDeviceIndex,
                                   VkPeerMemoryFeatureFlags *pPeerMemoryFeatures) {
    (void)device;
    if (!pPeerMemoryFeatures) return;
    if (heapIndex < 2 && localDeviceIndex == 0 && remoteDeviceIndex == 0)
        *pPeerMemoryFeatures = VK_PEER_MEMORY_FEATURE_COPY_SRC_BIT |
            VK_PEER_MEMORY_FEATURE_COPY_DST_BIT |
            VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT |
            VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT;
    else
        *pPeerMemoryFeatures = 0;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkDeviceWaitIdle(VkDevice device) {
    return device ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueueWaitIdle(VkQueue queue) {
    return queue ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateMemory(VkDevice device_handle, const VkMemoryAllocateInfo *pAllocateInfo,
                 const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory) {
    VkPs5Device *device = (VkPs5Device *)device_handle;
    AgcDeviceProperties capabilities = ps5_capabilities();
    if (!device || !pAllocateInfo || !pMemory ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO ||
        pAllocateInfo->memoryTypeIndex >= capabilities.memory_heap_count ||
        pAllocateInfo->allocationSize == 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceSize heap_size =
        capabilities.memory_heaps[pAllocateInfo->memoryTypeIndex].size;
    if (pAllocateInfo->allocationSize > heap_size || pAllocateInfo->allocationSize > SIZE_MAX)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkBool32 dedicated = VK_FALSE;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pAllocateInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO) {
            const VkMemoryAllocateFlagsInfo *flags = (const VkMemoryAllocateFlagsInfo *)next;
            if ((flags->flags & ~VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT) ||
                ((flags->flags & VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT) && flags->deviceMask != 1))
                return VK_ERROR_FEATURE_NOT_PRESENT;
        } else if (next->sType == VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO &&
                   ((const VkExportMemoryAllocateInfo *)next)->handleTypes != 0) {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        } else if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO) {
            const VkMemoryDedicatedAllocateInfo *info =
                (const VkMemoryDedicatedAllocateInfo *)next;
            if (info->image && info->buffer)
                return VK_ERROR_INITIALIZATION_FAILED;
            dedicated = (info->image || info->buffer) ? VK_TRUE : VK_FALSE;
        }
    }
    unsigned allocations = atomic_load(&device->memory_allocation_count);
    do {
        if (allocations >= 4096) return VK_ERROR_TOO_MANY_OBJECTS;
    } while (!atomic_compare_exchange_weak(&device->memory_allocation_count,
                                           &allocations, allocations + 1));
    const VkAllocationCallbacks *allocator = pAllocator ? pAllocator : device_allocator(device);
    VkPs5Memory *memory = ps5_alloc(allocator, sizeof(*memory), _Alignof(VkPs5Memory),
                                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (!memory) {
        atomic_fetch_sub(&device->memory_allocation_count, 1);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    uint64_t alignment =
        capabilities.memory_heaps[pAllocateInfo->memoryTypeIndex].minimum_alignment;
    AgcMemoryDesc memory_desc = AGC_MEMORY_DESC_INIT;
    memory_desc.size = pAllocateInfo->allocationSize;
    memory_desc.heap = pAllocateInfo->memoryTypeIndex == 0u ?
        AGC_MEMORY_HEAP_FLEXIBLE : AGC_MEMORY_HEAP_GARLIC;
    memory_desc.alignment = memory_desc.heap == AGC_MEMORY_HEAP_FLEXIBLE &&
        alignment > 0x4000u ? 0x4000u : alignment;
    if (dedicated)
        memory_desc.flags |= AGC_MEMORY_CREATE_DEDICATED_BIT;
    int32_t memory_result = agcCreateMemory(device->native_device,
        &memory_desc, &memory->native_memory);
    if (memory_result != AGC_OK) {
        ps5_free(allocator, memory);
        atomic_fetch_sub(&device->memory_allocation_count, 1);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    memory->device = device->native_device;
    if (agcMapMemory(memory->native_memory, 0u, pAllocateInfo->allocationSize,
            &memory->data) != AGC_OK) {
        (void)agcDestroyMemory(memory->native_memory);
        ps5_free(allocator, memory);
        atomic_fetch_sub(&device->memory_allocation_count, 1);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    memory->size = pAllocateInfo->allocationSize;
    memory->memory_type_index = pAllocateInfo->memoryTypeIndex;
    *pMemory = (VkDeviceMemory)memory;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkFreeMemory(VkDevice device_handle, VkDeviceMemory memory_handle,
             const VkAllocationCallbacks *pAllocator) {
    VkPs5Device *device = (VkPs5Device *)device_handle;
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    if (!device || !memory) return;
    const VkAllocationCallbacks *allocator = pAllocator ? pAllocator : device_allocator(device);
    (void)agcUnmapMemory(memory->native_memory);
    vk_ps5_destroy_or_defer_native(device_handle, VK_PS5_NATIVE_MEMORY,
        memory->native_memory);
    ps5_free(allocator, memory);
    atomic_fetch_sub(&device->memory_allocation_count, 1);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkMapMemory(VkDevice device, VkDeviceMemory memory_handle, VkDeviceSize offset,
            VkDeviceSize size, VkMemoryMapFlags flags, void **ppData) {
    (void)device; (void)flags;
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    if (!memory || !ppData || offset > memory->size) return VK_ERROR_MEMORY_MAP_FAILED;
    VkDeviceSize available = memory->size - offset;
    if (size != VK_WHOLE_SIZE && size > available) return VK_ERROR_MEMORY_MAP_FAILED;
    VkDeviceSize map_size = size == VK_WHOLE_SIZE ? available : size;
    if (!map_size || agcMapMemory(memory->native_memory, offset, map_size,
            ppData) != AGC_OK)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    (void)device;
    VkPs5Memory *native = (VkPs5Memory *)memory;
    if (native) (void)agcUnmapMemory(native->native_memory);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                          const VkMappedMemoryRange *pMemoryRanges) {
    (void)device;
    if (memoryRangeCount && !pMemoryRanges) return VK_ERROR_MEMORY_MAP_FAILED;
    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        VkPs5Memory *memory = (VkPs5Memory *)pMemoryRanges[i].memory;
        if (!memory || pMemoryRanges[i].offset > memory->size)
            return VK_ERROR_MEMORY_MAP_FAILED;
        VkDeviceSize size = pMemoryRanges[i].size == VK_WHOLE_SIZE ?
            memory->size - pMemoryRanges[i].offset : pMemoryRanges[i].size;
        if (!size || size > memory->size - pMemoryRanges[i].offset ||
            agcFlushMemory(memory->native_memory,
                pMemoryRanges[i].offset, size) != AGC_OK)
            return VK_ERROR_MEMORY_MAP_FAILED;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                               const VkMappedMemoryRange *pMemoryRanges) {
    (void)device;
    if (memoryRangeCount && !pMemoryRanges) return VK_ERROR_MEMORY_MAP_FAILED;
    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        VkPs5Memory *memory = (VkPs5Memory *)pMemoryRanges[i].memory;
        if (!memory || pMemoryRanges[i].offset > memory->size)
            return VK_ERROR_MEMORY_MAP_FAILED;
        VkDeviceSize size = pMemoryRanges[i].size == VK_WHOLE_SIZE ?
            memory->size - pMemoryRanges[i].offset : pMemoryRanges[i].size;
        if (!size || size > memory->size - pMemoryRanges[i].offset ||
            agcInvalidateMemory(memory->native_memory,
                pMemoryRanges[i].offset, size) != AGC_OK)
            return VK_ERROR_MEMORY_MAP_FAILED;
    }
    return VK_SUCCESS;
}

typedef struct ProcEntry { const char *name; PFN_vkVoidFunction proc; } ProcEntry;
#define ENTRY(name) { #name, (PFN_vkVoidFunction)name }
#define ALIAS(name, target) { name, (PFN_vkVoidFunction)target }

static const ProcEntry instance_procs[] = {
    ENTRY(vkEnumerateInstanceVersion), ENTRY(vkEnumerateInstanceExtensionProperties),
    ENTRY(vkEnumerateInstanceLayerProperties), ENTRY(vkCreateInstance),
    ENTRY(vkDestroyInstance), ENTRY(vkEnumeratePhysicalDevices),
    ENTRY(vkEnumeratePhysicalDeviceGroups), ENTRY(vkGetPhysicalDeviceProperties),
    ENTRY(vkGetPhysicalDeviceProperties2), ENTRY(vkGetPhysicalDeviceFeatures),
    ENTRY(vkGetPhysicalDeviceFeatures2), ENTRY(vkGetPhysicalDeviceMemoryProperties),
    ENTRY(vkGetPhysicalDeviceMemoryProperties2),
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties),
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties2),
    ENTRY(vkGetPhysicalDeviceFormatProperties),
    ENTRY(vkGetPhysicalDeviceFormatProperties2),
    ENTRY(vkGetPhysicalDeviceImageFormatProperties),
    ENTRY(vkGetPhysicalDeviceImageFormatProperties2),
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties),
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties2),
    ENTRY(vkGetPhysicalDeviceExternalBufferProperties),
    ENTRY(vkGetPhysicalDeviceExternalFenceProperties),
    ENTRY(vkGetPhysicalDeviceExternalSemaphoreProperties),
    ENTRY(vkEnumerateDeviceExtensionProperties), ENTRY(vkEnumerateDeviceLayerProperties),
    ENTRY(vkCreateDevice),
    ENTRY(vkDestroySurfaceKHR), ENTRY(vkCreateHeadlessSurfaceEXT),
    ENTRY(vkGetPhysicalDeviceSurfaceSupportKHR),
    ENTRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR),
    ENTRY(vkGetPhysicalDeviceSurfaceFormatsKHR),
    ENTRY(vkGetPhysicalDeviceSurfacePresentModesKHR),
    ENTRY(vkGetPhysicalDevicePresentRectanglesKHR),
    ALIAS("vkEnumeratePhysicalDeviceGroupsKHR", vkEnumeratePhysicalDeviceGroups),
    ALIAS("vkGetPhysicalDeviceFeatures2KHR", vkGetPhysicalDeviceFeatures2),
    ALIAS("vkGetPhysicalDeviceProperties2KHR", vkGetPhysicalDeviceProperties2),
    ALIAS("vkGetPhysicalDeviceFormatProperties2KHR", vkGetPhysicalDeviceFormatProperties2),
    ALIAS("vkGetPhysicalDeviceImageFormatProperties2KHR",
          vkGetPhysicalDeviceImageFormatProperties2),
    ALIAS("vkGetPhysicalDeviceQueueFamilyProperties2KHR",
          vkGetPhysicalDeviceQueueFamilyProperties2),
    ALIAS("vkGetPhysicalDeviceMemoryProperties2KHR", vkGetPhysicalDeviceMemoryProperties2),
    ALIAS("vkGetPhysicalDeviceSparseImageFormatProperties2KHR",
          vkGetPhysicalDeviceSparseImageFormatProperties2),
    ALIAS("vkGetPhysicalDeviceExternalBufferPropertiesKHR",
          vkGetPhysicalDeviceExternalBufferProperties),
    ALIAS("vkGetPhysicalDeviceExternalFencePropertiesKHR",
          vkGetPhysicalDeviceExternalFenceProperties),
    ALIAS("vkGetPhysicalDeviceExternalSemaphorePropertiesKHR",
          vkGetPhysicalDeviceExternalSemaphoreProperties),
};

static const ProcEntry device_procs[] = {
    ENTRY(vkDestroyDevice), ENTRY(vkGetDeviceQueue), ENTRY(vkGetDeviceQueue2),
    ENTRY(vkGetDeviceGroupPeerMemoryFeatures), ENTRY(vkDeviceWaitIdle),
    ENTRY(vkQueueWaitIdle), ENTRY(vkAllocateMemory), ENTRY(vkFreeMemory),
    ENTRY(vkMapMemory), ENTRY(vkUnmapMemory), ENTRY(vkFlushMappedMemoryRanges),
    ENTRY(vkInvalidateMappedMemoryRanges),
    ENTRY(vkCreateSwapchainKHR), ENTRY(vkDestroySwapchainKHR),
    ENTRY(vkGetSwapchainImagesKHR), ENTRY(vkAcquireNextImageKHR),
    ENTRY(vkAcquireNextImage2KHR), ENTRY(vkQueuePresentKHR),
    ENTRY(vkGetDeviceGroupPresentCapabilitiesKHR),
    ENTRY(vkGetDeviceGroupSurfacePresentModesKHR),
    ENTRY(vkQueueSubmit), ENTRY(vkQueueBindSparse),
    ENTRY(vkGetDeviceMemoryCommitment), ENTRY(vkBindBufferMemory),
    ENTRY(vkBindImageMemory), ENTRY(vkGetBufferMemoryRequirements),
    ENTRY(vkGetImageMemoryRequirements), ENTRY(vkGetImageSparseMemoryRequirements),
    ENTRY(vkCreateFence), ENTRY(vkDestroyFence), ENTRY(vkResetFences),
    ENTRY(vkGetFenceStatus), ENTRY(vkWaitForFences),
    ENTRY(vkCreateSemaphore), ENTRY(vkDestroySemaphore),
    ENTRY(vkGetSemaphoreCounterValue), ENTRY(vkWaitSemaphores),
    ENTRY(vkSignalSemaphore),
    ENTRY(vkCreateEvent), ENTRY(vkDestroyEvent), ENTRY(vkGetEventStatus),
    ENTRY(vkSetEvent), ENTRY(vkResetEvent),
    ENTRY(vkCreateBuffer), ENTRY(vkDestroyBuffer),
    ENTRY(vkCreateImage), ENTRY(vkDestroyImage),
    ENTRY(vkGetImageSubresourceLayout), ENTRY(vkCreateImageView),
    ENTRY(vkGetImageSubresourceLayout2),
    ENTRY(vkGetDeviceImageSubresourceLayout),
    ENTRY(vkDestroyImageView), ENTRY(vkCreateBufferView), ENTRY(vkDestroyBufferView),
    ENTRY(vkCreateCommandPool), ENTRY(vkDestroyCommandPool),
    ENTRY(vkResetCommandPool), ENTRY(vkTrimCommandPool),
    ENTRY(vkAllocateCommandBuffers), ENTRY(vkFreeCommandBuffers),
    ENTRY(vkBeginCommandBuffer), ENTRY(vkEndCommandBuffer), ENTRY(vkResetCommandBuffer),
    ENTRY(vkBindBufferMemory2), ENTRY(vkBindImageMemory2),
    ENTRY(vkGetBufferMemoryRequirements2), ENTRY(vkGetImageMemoryRequirements2),
    ENTRY(vkGetImageSparseMemoryRequirements2),
    ALIAS("vkBindBufferMemory2KHR", vkBindBufferMemory2),
    ALIAS("vkBindImageMemory2KHR", vkBindImageMemory2),
    ALIAS("vkGetBufferMemoryRequirements2KHR", vkGetBufferMemoryRequirements2),
    ALIAS("vkGetImageMemoryRequirements2KHR", vkGetImageMemoryRequirements2),
    ALIAS("vkGetImageSparseMemoryRequirements2KHR", vkGetImageSparseMemoryRequirements2),
    ALIAS("vkTrimCommandPoolKHR", vkTrimCommandPool),
    ENTRY(vkCreateQueryPool), ENTRY(vkDestroyQueryPool), ENTRY(vkGetQueryPoolResults),
    ENTRY(vkResetQueryPool), ENTRY(vkResetQueryPoolEXT),
    ENTRY(vkCreateShaderModule), ENTRY(vkDestroyShaderModule),
    ENTRY(vkCreatePipelineCache), ENTRY(vkDestroyPipelineCache),
    ENTRY(vkGetPipelineCacheData), ENTRY(vkMergePipelineCaches),
    ENTRY(vkCreateComputePipelines), ENTRY(vkCreateGraphicsPipelines),
    ENTRY(vkDestroyPipeline), ENTRY(vkCreatePipelineLayout),
    ENTRY(vkDestroyPipelineLayout), ENTRY(vkCreateSampler), ENTRY(vkDestroySampler),
    ENTRY(vkCreateDescriptorSetLayout), ENTRY(vkDestroyDescriptorSetLayout),
    ENTRY(vkCreateDescriptorPool), ENTRY(vkDestroyDescriptorPool),
    ENTRY(vkResetDescriptorPool), ENTRY(vkAllocateDescriptorSets),
    ENTRY(vkFreeDescriptorSets), ENTRY(vkUpdateDescriptorSets),
    ENTRY(vkCreateFramebuffer), ENTRY(vkDestroyFramebuffer),
    ENTRY(vkCreateRenderPass), ENTRY(vkDestroyRenderPass),
    ENTRY(vkCreateRenderPass2), ENTRY(vkCmdBeginRenderPass2),
    ENTRY(vkCmdNextSubpass2), ENTRY(vkCmdEndRenderPass2),
    ENTRY(vkGetRenderAreaGranularity), ENTRY(vkCreateDescriptorUpdateTemplate),
    ENTRY(vkDestroyDescriptorUpdateTemplate), ENTRY(vkUpdateDescriptorSetWithTemplate),
    ENTRY(vkGetDescriptorSetLayoutSupport), ENTRY(vkCreateSamplerYcbcrConversion),
    ENTRY(vkDestroySamplerYcbcrConversion),
    ENTRY(vkCmdCopyBuffer), ENTRY(vkCmdCopyImage), ENTRY(vkCmdCopyBufferToImage),
    ENTRY(vkCmdCopyImageToBuffer), ENTRY(vkCmdUpdateBuffer), ENTRY(vkCmdFillBuffer),
    ENTRY(vkCmdPipelineBarrier), ENTRY(vkCmdBeginQuery), ENTRY(vkCmdEndQuery),
    ENTRY(vkCmdResetQueryPool), ENTRY(vkCmdWriteTimestamp),
    ENTRY(vkCmdCopyQueryPoolResults), ENTRY(vkCmdExecuteCommands),
    ENTRY(vkCmdBindPipeline), ENTRY(vkCmdBindDescriptorSets),
    ENTRY(vkCmdClearColorImage), ENTRY(vkCmdDispatch), ENTRY(vkCmdDispatchIndirect),
    ENTRY(vkCmdSetEvent), ENTRY(vkCmdResetEvent), ENTRY(vkCmdWaitEvents),
    ENTRY(vkCmdPushConstants), ENTRY(vkCmdSetViewport), ENTRY(vkCmdSetScissor),
    ENTRY(vkCmdSetLineWidth), ENTRY(vkCmdSetDepthBias), ENTRY(vkCmdSetBlendConstants),
    ENTRY(vkCmdSetDepthBounds), ENTRY(vkCmdSetStencilCompareMask),
    ENTRY(vkCmdSetStencilWriteMask), ENTRY(vkCmdSetStencilReference),
    ENTRY(vkCmdBindIndexBuffer), ENTRY(vkCmdBindIndexBuffer2),
    ENTRY(vkCmdBindVertexBuffers), ENTRY(vkCmdBindVertexBuffers2),
    ALIAS("vkCmdBindVertexBuffers2EXT", vkCmdBindVertexBuffers2),
    ENTRY(vkCmdDraw),
    ENTRY(vkCmdDrawIndexed), ENTRY(vkCmdDrawIndirect), ENTRY(vkCmdDrawIndexedIndirect),
    ENTRY(vkCmdDrawIndirectCount), ENTRY(vkCmdDrawIndexedIndirectCount),
    ENTRY(vkCmdBlitImage), ENTRY(vkCmdClearDepthStencilImage),
    ENTRY(vkCmdClearAttachments), ENTRY(vkCmdResolveImage),
    ENTRY(vkCmdBeginRenderPass), ENTRY(vkCmdNextSubpass), ENTRY(vkCmdEndRenderPass),
    ENTRY(vkCmdBeginRendering), ENTRY(vkCmdEndRendering),
    ENTRY(vkGetRenderingAreaGranularity),
    ENTRY(vkCmdSetDeviceMask), ENTRY(vkCmdDispatchBase),
    ALIAS("vkCmdSetDeviceMaskKHR", vkCmdSetDeviceMask),
    ALIAS("vkCmdDispatchBaseKHR", vkCmdDispatchBase),
    ALIAS("vkCreateDescriptorUpdateTemplateKHR", vkCreateDescriptorUpdateTemplate),
    ALIAS("vkDestroyDescriptorUpdateTemplateKHR", vkDestroyDescriptorUpdateTemplate),
    ALIAS("vkUpdateDescriptorSetWithTemplateKHR", vkUpdateDescriptorSetWithTemplate),
    ALIAS("vkGetSemaphoreCounterValueKHR", vkGetSemaphoreCounterValue),
    ALIAS("vkWaitSemaphoresKHR", vkWaitSemaphores),
    ALIAS("vkSignalSemaphoreKHR", vkSignalSemaphore),
    ALIAS("vkCreateRenderPass2KHR", vkCreateRenderPass2),
    ALIAS("vkCmdBeginRenderPass2KHR", vkCmdBeginRenderPass2),
    ALIAS("vkCmdNextSubpass2KHR", vkCmdNextSubpass2),
    ALIAS("vkCmdEndRenderPass2KHR", vkCmdEndRenderPass2),
    ALIAS("vkCmdBeginRenderingKHR", vkCmdBeginRendering),
    ALIAS("vkCmdEndRenderingKHR", vkCmdEndRendering),
    ALIAS("vkCmdBindIndexBuffer2KHR", vkCmdBindIndexBuffer2),
    ALIAS("vkGetRenderingAreaGranularityKHR", vkGetRenderingAreaGranularity),
    ALIAS("vkGetDeviceImageSubresourceLayoutKHR",
        vkGetDeviceImageSubresourceLayout),
    ALIAS("vkGetImageSubresourceLayout2KHR", vkGetImageSubresourceLayout2),
    ENTRY(vkGetBufferDeviceAddress),
    ENTRY(vkGetBufferOpaqueCaptureAddress),
    ENTRY(vkGetDeviceMemoryOpaqueCaptureAddress),
    ALIAS("vkGetDescriptorSetLayoutSupportKHR", vkGetDescriptorSetLayoutSupport),
    ALIAS("vkCreateSamplerYcbcrConversionKHR", vkCreateSamplerYcbcrConversion),
    ALIAS("vkDestroySamplerYcbcrConversionKHR", vkDestroySamplerYcbcrConversion),
    ALIAS("vkGetDeviceQueue2KHR", vkGetDeviceQueue2),
    ALIAS("vkGetDeviceGroupPeerMemoryFeaturesKHR", vkGetDeviceGroupPeerMemoryFeatures),
};

static PFN_vkVoidFunction find_proc(const ProcEntry *entries, size_t count, const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(entries[i].name, name) == 0) return entries[i].proc;
    return NULL;
}

VK_PS5_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName) {
    if (!device) return NULL;
    return find_proc(device_procs, sizeof(device_procs) / sizeof(device_procs[0]), pName);
}

VK_PS5_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName) {
    if (!pName) return NULL;
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0)
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (instance && strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (!instance) {
        if (strcmp(pName, "vkCreateInstance") == 0)
            return (PFN_vkVoidFunction)vkCreateInstance;
        if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
            return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
        if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0)
            return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
        if (strcmp(pName, "vkEnumerateInstanceVersion") == 0)
            return (PFN_vkVoidFunction)vkEnumerateInstanceVersion;
        return NULL;
    }
    PFN_vkVoidFunction proc = find_proc(instance_procs,
        sizeof(instance_procs) / sizeof(instance_procs[0]), pName);
    return proc ? proc : find_proc(device_procs,
        sizeof(device_procs) / sizeof(device_procs[0]), pName);
}

VK_PS5_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName) {
    return vkGetInstanceProcAddr(instance, pName);
}

VK_PS5_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName) {
    (void)instance;
    return find_proc(instance_procs, sizeof(instance_procs) / sizeof(instance_procs[0]), pName);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion) {
    if (!pSupportedVersion) return VK_ERROR_INITIALIZATION_FAILED;
    if (*pSupportedVersion < 5) return VK_ERROR_INCOMPATIBLE_DRIVER;
    if (*pSupportedVersion > 5) *pSupportedVersion = 5;
    return VK_SUCCESS;
}
