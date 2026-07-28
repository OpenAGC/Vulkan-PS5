#include "vulkan_ps5_internal.h"
#include <openagc_psbc.h>
#include <agc_cb.h>
#include <agc_graphics.h>
#include <agc_memory.h>
#include <agc_shader.h>
#include <agc_texture.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct VkPs5Fence { atomic_bool signaled; } VkPs5Fence;
typedef struct VkPs5Semaphore { atomic_bool signaled; } VkPs5Semaphore;
typedef struct VkPs5Event { atomic_int status; } VkPs5Event;

VkResult vk_ps5_signal_acquire(VkSemaphore semaphore_handle,
                               VkFence fence_handle) {
    if (!semaphore_handle && !fence_handle)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (semaphore_handle)
        atomic_store(&((VkPs5Semaphore *)semaphore_handle)->signaled, true);
    if (fence_handle)
        atomic_store(&((VkPs5Fence *)fence_handle)->signaled, true);
    return VK_SUCCESS;
}

VkResult vk_ps5_consume_semaphores(uint32_t semaphore_count,
                                   const VkSemaphore *semaphores) {
    if (semaphore_count && !semaphores)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < semaphore_count; ++i) {
        VkPs5Semaphore *semaphore = (VkPs5Semaphore *)semaphores[i];
        if (!semaphore || !atomic_load(&semaphore->signaled))
            return VK_NOT_READY;
    }
    for (uint32_t i = 0; i < semaphore_count; ++i)
        atomic_store(&((VkPs5Semaphore *)semaphores[i])->signaled, false);
    return VK_SUCCESS;
}

typedef struct VkPs5Buffer {
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkDeviceMemory memory;
    VkDeviceSize memory_offset;
} VkPs5Buffer;

typedef struct VkPs5Image {
    VkImageType type;
    VkFormat format;
    VkExtent3D extent;
    uint32_t mip_levels;
    uint32_t array_layers;
    VkSampleCountFlagBits samples;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkDeviceSize row_pitch;
    VkDeviceSize depth_pitch;
    VkDeviceSize array_pitch;
    VkDeviceSize alignment;
    VkDeviceSize depth_plane_offset;
    VkDeviceSize stencil_plane_offset;
    AgcGfx1013DepthSurfaceLayout depth_layout;
    AgcGfx1013DepthSurfaceFormat depth_format;
    VkBool32 is_depth_surface;
    VkDeviceSize size;
    VkDeviceMemory memory;
    VkDeviceSize memory_offset;
} VkPs5Image;

typedef struct VkPs5ImageView {
    VkImage image;
    VkFormat format;
    VkComponentMapping components;
} VkPs5ImageView;
typedef struct VkPs5BufferView { VkBuffer buffer; VkFormat format; } VkPs5BufferView;
typedef struct VkPs5Opaque { uint32_t kind; } VkPs5Opaque;
typedef struct VkPs5Sampler { AgcSamplerDescriptor descriptor; } VkPs5Sampler;

#define VK_PS5_MAX_RENDER_ATTACHMENTS 8u
#define VK_PS5_MAX_SUBPASSES 8u
#define VK_PS5_MAX_VERTEX_BINDINGS 32u
#define VK_PS5_VERTEX_TABLE_SIZE 0x4000u
#define VK_PS5_VERTEX_TABLE_SLICE \
    (VK_PS5_MAX_VERTEX_BINDINGS * sizeof(AgcGfx1013BufferDescriptor))
#define VK_PS5_INDIRECT_DESCRIPTOR_TABLE_SLICE 256u

typedef struct VkPs5ShaderModule {
    size_t code_size;
    uint32_t code[];
} VkPs5ShaderModule;

typedef struct VkPs5PipelineCache {
    size_t data_size;
    uint8_t data[];
} VkPs5PipelineCache;

typedef struct VkPs5DescriptorSetLayout {
    uint32_t binding_count;
    VkDescriptorSetLayoutBinding bindings[];
} VkPs5DescriptorSetLayout;

typedef struct VkPs5PipelineLayout {
    uint32_t binding_count;
    uint32_t push_constant_size;
    OpenAgcPsbcDescriptorBinding bindings[];
} VkPs5PipelineLayout;

typedef struct VkPs5RenderPass {
    uint32_t attachment_count;
    uint32_t subpass_count;
    VkAttachmentDescription attachments[VK_PS5_MAX_RENDER_ATTACHMENTS];
    struct {
        uint32_t color_attachment_count;
        uint32_t color_attachments[AGC_GFX1013_MAX_COLOR_TARGETS];
        uint32_t depth_stencil_attachment;
        VkImageLayout depth_stencil_layout;
    } subpasses[VK_PS5_MAX_SUBPASSES];
} VkPs5RenderPass;

typedef struct VkPs5Framebuffer {
    VkPs5RenderPass *render_pass;
    uint32_t attachment_count;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    VkPs5ImageView *attachments[VK_PS5_MAX_RENDER_ATTACHMENTS];
} VkPs5Framebuffer;

typedef struct VkPs5RuntimeShader {
    AgcShaderRecord record;
    AgcShaderRecord front_record;
    AgcShaderRecord fused_record;
    AgcRegisterValue fused_registers[64];
    AgcGpuMemory code_memory;
    AgcGpuMemory front_code_memory;
    void *code;
    size_t code_size;
    void *front_code;
    size_t front_code_size;
    AgcGfx1013ShaderBinding binding;
} VkPs5RuntimeShader;

typedef struct VkPs5Pipeline {
    uint32_t stage_count;
    VkPipelineBindPoint bind_point;
    uint32_t primitive_type;
    OpenAgcPsbcStage stage_types[3];
    OpenAgcPsbcOutput stages[3];
    VkPs5RuntimeShader runtime[3];
    AgcGfx1013ViewportState viewport;
    AgcGfx1013ScissorState scissor;
    uint32_t vertex_binding_mask;
    uint32_t vertex_strides[VK_PS5_MAX_VERTEX_BINDINGS];
    VkBool32 robust_buffer_access;
    uint32_t vertex_attribute_mask;
    uint32_t vertex_attribute_bindings[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
    uint32_t vertex_attribute_offsets[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
    uint32_t vertex_attribute_sizes[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
    AgcGfx1013ColorBlendState color_blend;
    AgcGfx1013PolygonMode polygon_mode;
    AgcGfx1013PrimitiveSizeState primitive_size;
    VkBool32 line_width_dynamic;
    AgcGfx1013DepthBiasState depth_bias;
    VkBool32 depth_bias_enable;
    VkBool32 depth_bias_dynamic;
    VkBool32 depth_clamp_enable;
    AgcGfx1013DepthStencilState depth_stencil;
    VkBool32 has_depth_stencil;
    const AgcGfx1013TessellationState *tessellation;
    uint64_t tess_ring_descriptor_address;
    uint32_t tcs_offchip_layout;
    uint32_t tes_offchip_layout;
} VkPs5Pipeline;

typedef struct VkPs5QueryPool {
    VkQueryType type;
    uint32_t count;
    AgcGpuMemory memory;
} VkPs5QueryPool;

#define VK_PS5_QUERY_AVAILABILITY_OFFSET \
    AGC_GFX1013_OCCLUSION_QUERY_STRIDE
#define VK_PS5_QUERY_SLOT_SIZE \
    (AGC_GFX1013_OCCLUSION_QUERY_STRIDE + 16u)

typedef struct VkPs5DescriptorSet VkPs5DescriptorSet;
typedef struct VkPs5DescriptorValue {
    VkDescriptorType type;
    VkDescriptorBufferInfo buffer;
    VkDescriptorImageInfo image;
    VkBool32 valid;
} VkPs5DescriptorValue;

typedef struct VkPs5DescriptorPool {
    VkDevice device;
    uint32_t max_sets;
    uint32_t allocated_sets;
    VkPs5DescriptorSet *sets;
} VkPs5DescriptorPool;

struct VkPs5DescriptorSet {
    VkPs5DescriptorPool *pool;
    VkPs5DescriptorSet *next;
    VkPs5DescriptorSetLayout *layout;
    uint32_t descriptor_count;
    AgcGpuMemory table_memory;
    VkPs5DescriptorValue values[];
};

#define VK_PS5_DESCRIPTOR_TABLE_SIZE 0x4000u

typedef struct VkPs5CommandPool {
    VkDevice device;
    uint32_t queue_family_index;
    struct VkPs5CommandBuffer *buffers;
} VkPs5CommandPool;

typedef enum VkPs5CommandState {
    VK_PS5_COMMAND_INITIAL,
    VK_PS5_COMMAND_RECORDING,
    VK_PS5_COMMAND_EXECUTABLE,
    VK_PS5_COMMAND_PENDING,
} VkPs5CommandState;

typedef struct VkPs5CommandBuffer {
    VK_LOADER_DATA loader_data;
    VkDevice device;
    VkCommandPool pool;
    VkCommandBufferLevel level;
    VkPs5CommandState state;
    VkResult record_error;
    VkBool32 compute_defaults_emitted;
    VkPs5Pipeline *bound_compute;
    VkPs5Pipeline *bound_graphics;
    VkPs5DescriptorSet *compute_sets[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    VkPs5DescriptorSet *graphics_sets[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    VkPs5RenderPass *active_render_pass;
    VkPs5Framebuffer *active_framebuffer;
    uint32_t active_subpass;
    AgcGfx1013FrameState frame_state;
    AgcGfx1013DepthSurfaceState depth_surface_state;
    float dynamic_line_width;
    VkBool32 dynamic_line_width_set;
    AgcGfx1013DepthBiasState dynamic_depth_bias;
    VkBool32 dynamic_depth_bias_set;
    VkPs5QueryPool *active_query_pool;
    uint32_t active_query;
    VkPs5Buffer *index_buffer;
    VkDeviceSize index_offset;
    VkIndexType index_type;
    VkPs5Buffer *vertex_buffers[VK_PS5_MAX_VERTEX_BINDINGS];
    VkDeviceSize vertex_offsets[VK_PS5_MAX_VERTEX_BINDINGS];
    AgcGpuMemory vertex_table_memory;
    size_t vertex_table_offset;
    uint32_t last_indirect_descriptor_table;
    size_t last_indirect_descriptor_table_offset;
    uint32_t last_indirect_descriptor_register;
    uint32_t *dcb_storage;
    size_t dcb_size;
    SceAgcCb dcb;
    struct VkPs5CommandBuffer *next;
} VkPs5CommandBuffer;

uint32_t vk_ps5_command_buffer_dwords(
    VkCommandBuffer command_buffer, const uint32_t **commands) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)command_buffer;
    if (!command || !commands) return 0;
    *commands = command->dcb_storage;
    return agcCbUsedDwords(&command->dcb);
}

uint32_t vk_ps5_command_buffer_indirect_descriptor_table(
    VkCommandBuffer command_buffer) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)command_buffer;
    return command ? command->last_indirect_descriptor_table : 0u;
}

uint32_t vk_ps5_command_buffer_indirect_descriptor_entry(
    VkCommandBuffer command_buffer, uint32_t set) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)command_buffer;
    if (!command || !command->last_indirect_descriptor_table ||
        set >= OPENAGC_PSBC_MAX_DESCRIPTOR_SETS)
        return 0u;
    size_t offset = command->last_indirect_descriptor_table_offset;
    if (offset > VK_PS5_VERTEX_TABLE_SIZE - sizeof(uint32_t) ||
        set > (VK_PS5_VERTEX_TABLE_SIZE - offset) / sizeof(uint32_t) - 1u)
        return 0u;
    const uint32_t *entries = (const uint32_t *)(
        (const uint8_t *)command->vertex_table_memory.cpu_address + offset);
    return entries[set];
}

uint32_t vk_ps5_command_buffer_indirect_descriptor_register(
    VkCommandBuffer command_buffer) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)command_buffer;
    return command ? command->last_indirect_descriptor_register : 0u;
}

static void *alloc_object(VkDevice device, const VkAllocationCallbacks *allocator,
                          size_t size, size_t alignment) {
    void *object = vk_ps5_device_alloc(device, allocator, size, alignment,
                                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (object) memset(object, 0, size);
    return object;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                 VkLayerProperties *pProperties) {
    (void)physicalDevice; (void)pProperties;
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
              VkFence fence_handle) {
    if (!queue || (submitCount && !pSubmits)) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < submitCount; ++i) {
        if (pSubmits[i].sType != VK_STRUCTURE_TYPE_SUBMIT_INFO)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0; j < pSubmits[i].waitSemaphoreCount; ++j) {
            VkPs5Semaphore *semaphore = (VkPs5Semaphore *)pSubmits[i].pWaitSemaphores[j];
            if (!semaphore || !atomic_exchange(&semaphore->signaled, false))
                return VK_NOT_READY;
        }
        for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; ++j) {
            VkPs5CommandBuffer *command =
                (VkPs5CommandBuffer *)pSubmits[i].pCommandBuffers[j];
            if (!command || command->state != VK_PS5_COMMAND_EXECUTABLE)
                return VK_ERROR_INITIALIZATION_FAILED;
            command->state = VK_PS5_COMMAND_PENDING;
            VkResult result = vk_ps5_queue_submit_dcb(
                queue, command->dcb_storage, agcCbUsedDwords(&command->dcb));
            command->state = VK_PS5_COMMAND_EXECUTABLE;
            if (result != VK_SUCCESS) return result;
        }
        for (uint32_t j = 0; j < pSubmits[i].signalSemaphoreCount; ++j) {
            VkPs5Semaphore *semaphore = (VkPs5Semaphore *)pSubmits[i].pSignalSemaphores[j];
            if (!semaphore) return VK_ERROR_INITIALIZATION_FAILED;
            atomic_store(&semaphore->signaled, true);
        }
    }
    if (fence_handle) atomic_store(&((VkPs5Fence *)fence_handle)->signaled, true);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                  const VkBindSparseInfo *pBindInfo, VkFence fence) {
    (void)queue; (void)bindInfoCount; (void)pBindInfo; (void)fence;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    if (!device || !pCreateInfo || !pFence ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_FENCE_CREATE_INFO ||
        (pCreateInfo->flags & ~VK_FENCE_CREATE_SIGNALED_BIT))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Fence *fence = alloc_object(device, pAllocator, sizeof(*fence), _Alignof(VkPs5Fence));
    if (!fence) return VK_ERROR_OUT_OF_HOST_MEMORY;
    atomic_init(&fence->signaled,
                (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0);
    *pFence = (VkFence)fence;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator) {
    if (fence) vk_ps5_device_free(device, pAllocator, (void *)fence);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    (void)device;
    if (fenceCount && !pFences) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < fenceCount; ++i) {
        if (!pFences[i]) return VK_ERROR_INITIALIZATION_FAILED;
        atomic_store(&((VkPs5Fence *)pFences[i])->signaled, false);
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetFenceStatus(VkDevice device, VkFence fence) {
    (void)device;
    if (!fence) return VK_ERROR_INITIALIZATION_FAILED;
    return atomic_load(&((VkPs5Fence *)fence)->signaled) ? VK_SUCCESS : VK_NOT_READY;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences,
                VkBool32 waitAll, uint64_t timeout) {
    (void)device; (void)timeout;
    if (!fenceCount || !pFences) return VK_ERROR_INITIALIZATION_FAILED;
    VkBool32 any = VK_FALSE;
    VkBool32 all = VK_TRUE;
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkBool32 signaled = pFences[i] &&
            atomic_load(&((VkPs5Fence *)pFences[i])->signaled);
        any |= signaled;
        all &= signaled;
    }
    return (waitAll ? all : any) ? VK_SUCCESS : VK_TIMEOUT;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
    if (!device || !pCreateInfo || !pSemaphore ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO || pCreateInfo->flags)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Semaphore *semaphore = alloc_object(device, pAllocator, sizeof(*semaphore),
                                              _Alignof(VkPs5Semaphore));
    if (!semaphore) return VK_ERROR_OUT_OF_HOST_MEMORY;
    atomic_init(&semaphore->signaled, false);
    *pSemaphore = (VkSemaphore)semaphore;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                   const VkAllocationCallbacks *pAllocator) {
    if (semaphore) vk_ps5_device_free(device, pAllocator, (void *)semaphore);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkEvent *pEvent) {
    if (!device || !pCreateInfo || !pEvent ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_EVENT_CREATE_INFO || pCreateInfo->flags)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Event *event = alloc_object(device, pAllocator, sizeof(*event), _Alignof(VkPs5Event));
    if (!event) return VK_ERROR_OUT_OF_HOST_MEMORY;
    atomic_init(&event->status, VK_EVENT_RESET);
    *pEvent = (VkEvent)event;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator) {
    if (event) vk_ps5_device_free(device, pAllocator, (void *)event);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetEventStatus(VkDevice device, VkEvent event) {
    (void)device;
    return event ? (VkResult)atomic_load(&((VkPs5Event *)event)->status) :
        VK_ERROR_INITIALIZATION_FAILED;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkSetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    atomic_store(&((VkPs5Event *)event)->status, VK_EVENT_SET);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    atomic_store(&((VkPs5Event *)event)->status, VK_EVENT_RESET);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
    if (!device || !pCreateInfo || !pBuffer ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO ||
        !pCreateInfo->size || pCreateInfo->sharingMode > VK_SHARING_MODE_CONCURRENT ||
        (pCreateInfo->flags & (VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                               VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT |
                               VK_BUFFER_CREATE_SPARSE_ALIASED_BIT |
                               VK_BUFFER_CREATE_PROTECTED_BIT)))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext)
        if (next->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO &&
            ((const VkExternalMemoryBufferCreateInfo *)next)->handleTypes != 0)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPs5Buffer *buffer = alloc_object(device, pAllocator, sizeof(*buffer),
                                       _Alignof(VkPs5Buffer));
    if (!buffer) return VK_ERROR_OUT_OF_HOST_MEMORY;
    buffer->size = pCreateInfo->size;
    buffer->usage = pCreateInfo->usage;
    *pBuffer = (VkBuffer)buffer;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
    if (buffer) vk_ps5_device_free(device, pAllocator, (void *)buffer);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer_handle,
                              VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!buffer_handle || !pMemoryRequirements) return;
    VkPs5Buffer *buffer = (VkPs5Buffer *)buffer_handle;
    pMemoryRequirements->alignment = 16;
    pMemoryRequirements->size = (buffer->size + 15u) & ~(VkDeviceSize)15u;
    pMemoryRequirements->memoryTypeBits = 0x3;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindBufferMemory(VkDevice device, VkBuffer buffer_handle, VkDeviceMemory memory,
                   VkDeviceSize memoryOffset) {
    (void)device;
    VkPs5Buffer *buffer = (VkPs5Buffer *)buffer_handle;
    if (!buffer || !memory || memoryOffset % 16 != 0 ||
        memoryOffset > vk_ps5_memory_size(memory) ||
        buffer->size > vk_ps5_memory_size(memory) - memoryOffset)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    buffer->memory = memory;
    buffer->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

static uint32_t format_bytes(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_S8_UINT: return 1;
    case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_D16_UNORM: return 2;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT: return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    default: return 0;
    }
}

static bool color_target_format(
    VkFormat format, AgcGfx1013ColorTargetFormat *target_format) {
    if (!target_format) return false;
    switch (format) {
    case VK_FORMAT_R8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_R8_UNORM; break;
    case VK_FORMAT_R8G8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_RG8_UNORM; break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA8_UNORM; break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_BGRA8_UNORM; break;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        *target_format = AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM; break;
    case VK_FORMAT_R16_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_R16_FLOAT; break;
    case VK_FORMAT_R16G16_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RG16_FLOAT; break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT; break;
    case VK_FORMAT_R32_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_R32_FLOAT; break;
    case VK_FORMAT_R32G32_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RG32_FLOAT; break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT; break;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        *target_format = AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT; break;
    case VK_FORMAT_R8G8B8A8_SRGB:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA8_SRGB; break;
    case VK_FORMAT_B8G8R8A8_SRGB:
        *target_format = AGC_GFX1013_RT_FORMAT_BGRA8_SRGB; break;
    default:
        return false;
    }
    return true;
}

static bool depth_surface_format(
    VkFormat format, AgcGfx1013DepthSurfaceFormat *depth_format)
{
    if (!depth_format) return false;
    switch (format) {
    case VK_FORMAT_D16_UNORM:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM; break;
    case VK_FORMAT_D32_SFLOAT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT; break;
    case VK_FORMAT_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_S8_UINT; break;
    case VK_FORMAT_D16_UNORM_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT; break;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT; break;
    default:
        return false;
    }
    return true;
}

static AgcGfx1013StencilOp stencil_operation(VkStencilOp operation)
{
    switch (operation) {
    case VK_STENCIL_OP_KEEP: return AGC_GFX1013_STENCIL_KEEP;
    case VK_STENCIL_OP_ZERO: return AGC_GFX1013_STENCIL_ZERO;
    case VK_STENCIL_OP_REPLACE: return AGC_GFX1013_STENCIL_REPLACE;
    case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
        return AGC_GFX1013_STENCIL_INCREMENT_CLAMP;
    case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
        return AGC_GFX1013_STENCIL_DECREMENT_CLAMP;
    case VK_STENCIL_OP_INVERT: return AGC_GFX1013_STENCIL_INVERT;
    case VK_STENCIL_OP_INCREMENT_AND_WRAP:
        return AGC_GFX1013_STENCIL_INCREMENT_WRAP;
    case VK_STENCIL_OP_DECREMENT_AND_WRAP:
        return AGC_GFX1013_STENCIL_DECREMENT_WRAP;
    default: return (AgcGfx1013StencilOp)-1;
    }
}

static bool stencil_face_state(
    const VkStencilOpState *source, AgcGfx1013StencilFaceState *dest)
{
    if (!source || !dest || source->compareOp > VK_COMPARE_OP_ALWAYS)
        return false;
    dest->compare_operation = (AgcGfx1013CompareOp)source->compareOp;
    dest->fail_operation = stencil_operation(source->failOp);
    dest->depth_fail_operation = stencil_operation(source->depthFailOp);
    dest->pass_operation = stencil_operation(source->passOp);
    if ((int)dest->fail_operation < 0 ||
        (int)dest->depth_fail_operation < 0 ||
        (int)dest->pass_operation < 0)
        return false;
    dest->reference = source->reference;
    dest->compare_mask = source->compareMask;
    dest->write_mask = source->writeMask;
    return true;
}

static bool blend_factor(
    VkBlendFactor source, AgcGfx1013BlendFactor *destination)
{
    if (!destination) return false;
    switch (source) {
    case VK_BLEND_FACTOR_ZERO:
        *destination = AGC_GFX1013_BLEND_ZERO; break;
    case VK_BLEND_FACTOR_ONE:
        *destination = AGC_GFX1013_BLEND_ONE; break;
    case VK_BLEND_FACTOR_SRC_COLOR:
        *destination = AGC_GFX1013_BLEND_SRC_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC_COLOR; break;
    case VK_BLEND_FACTOR_DST_COLOR:
        *destination = AGC_GFX1013_BLEND_DST_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_DST_COLOR; break;
    case VK_BLEND_FACTOR_SRC_ALPHA:
        *destination = AGC_GFX1013_BLEND_SRC_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC_ALPHA; break;
    case VK_BLEND_FACTOR_DST_ALPHA:
        *destination = AGC_GFX1013_BLEND_DST_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_DST_ALPHA; break;
    case VK_BLEND_FACTOR_CONSTANT_COLOR:
        *destination = AGC_GFX1013_BLEND_CONSTANT_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_COLOR; break;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA:
        *destination = AGC_GFX1013_BLEND_CONSTANT_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_ALPHA; break;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
        *destination = AGC_GFX1013_BLEND_SRC_ALPHA_SATURATE; break;
    case VK_BLEND_FACTOR_SRC1_COLOR:
        *destination = AGC_GFX1013_BLEND_SRC1_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC1_COLOR; break;
    case VK_BLEND_FACTOR_SRC1_ALPHA:
        *destination = AGC_GFX1013_BLEND_SRC1_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA; break;
    default:
        return false;
    }
    return true;
}

static bool blend_operation(VkBlendOp source, AgcGfx1013BlendOp *destination)
{
    if (!destination) return false;
    switch (source) {
    case VK_BLEND_OP_ADD:
        *destination = AGC_GFX1013_BLEND_OP_ADD; break;
    case VK_BLEND_OP_SUBTRACT:
        *destination = AGC_GFX1013_BLEND_OP_SUBTRACT; break;
    case VK_BLEND_OP_REVERSE_SUBTRACT:
        *destination = AGC_GFX1013_BLEND_OP_REVERSE_SUBTRACT; break;
    case VK_BLEND_OP_MIN:
        *destination = AGC_GFX1013_BLEND_OP_MIN; break;
    case VK_BLEND_OP_MAX:
        *destination = AGC_GFX1013_BLEND_OP_MAX; break;
    default:
        return false;
    }
    return true;
}

static bool logic_operation(VkLogicOp source, AgcGfx1013LogicOp *destination)
{
    if (!destination)
        return false;
    switch (source) {
    case VK_LOGIC_OP_CLEAR: *destination = AGC_GFX1013_LOGIC_CLEAR; break;
    case VK_LOGIC_OP_AND: *destination = AGC_GFX1013_LOGIC_AND; break;
    case VK_LOGIC_OP_AND_REVERSE:
        *destination = AGC_GFX1013_LOGIC_AND_REVERSE; break;
    case VK_LOGIC_OP_COPY: *destination = AGC_GFX1013_LOGIC_COPY; break;
    case VK_LOGIC_OP_AND_INVERTED:
        *destination = AGC_GFX1013_LOGIC_AND_INVERTED; break;
    case VK_LOGIC_OP_NO_OP: *destination = AGC_GFX1013_LOGIC_NO_OP; break;
    case VK_LOGIC_OP_XOR: *destination = AGC_GFX1013_LOGIC_XOR; break;
    case VK_LOGIC_OP_OR: *destination = AGC_GFX1013_LOGIC_OR; break;
    case VK_LOGIC_OP_NOR: *destination = AGC_GFX1013_LOGIC_NOR; break;
    case VK_LOGIC_OP_EQUIVALENT:
        *destination = AGC_GFX1013_LOGIC_EQUIVALENT; break;
    case VK_LOGIC_OP_INVERT: *destination = AGC_GFX1013_LOGIC_INVERT; break;
    case VK_LOGIC_OP_OR_REVERSE:
        *destination = AGC_GFX1013_LOGIC_OR_REVERSE; break;
    case VK_LOGIC_OP_COPY_INVERTED:
        *destination = AGC_GFX1013_LOGIC_COPY_INVERTED; break;
    case VK_LOGIC_OP_OR_INVERTED:
        *destination = AGC_GFX1013_LOGIC_OR_INVERTED; break;
    case VK_LOGIC_OP_NAND: *destination = AGC_GFX1013_LOGIC_NAND; break;
    case VK_LOGIC_OP_SET: *destination = AGC_GFX1013_LOGIC_SET; break;
    default: return false;
    }
    return true;
}

static bool polygon_mode(
    VkPolygonMode source, AgcGfx1013PolygonMode *destination)
{
    if (!destination)
        return false;
    switch (source) {
    case VK_POLYGON_MODE_FILL:
        *destination = AGC_GFX1013_POLYGON_MODE_FILL;
        break;
    case VK_POLYGON_MODE_LINE:
        *destination = AGC_GFX1013_POLYGON_MODE_LINE;
        break;
    case VK_POLYGON_MODE_POINT:
        *destination = AGC_GFX1013_POLYGON_MODE_POINT;
        break;
    default:
        return false;
    }
    return true;
}

static bool primitive_topology(
    VkPrimitiveTopology source, AgcGfx1013PrimitiveTopology *destination)
{
    if (!destination)
        return false;
    switch (source) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_POINT_LIST;
        break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_LINE_LIST;
        break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
        *destination = AGC_GFX1013_TOPOLOGY_LINE_STRIP;
        break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_TRIANGLE_LIST;
        break;
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_PATCH_LIST;
        break;
    default:
        return false;
    }
    return true;
}

static bool color_blend_state(
    const VkPipelineColorBlendStateCreateInfo *source,
    uint32_t target_count, AgcGfx1013ColorBlendState *destination,
    bool *dual_source_blend)
{
    const VkColorComponentFlags component_mask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (!source || !destination || !dual_source_blend || !target_count ||
        target_count > AGC_GFX1013_MAX_COLOR_TARGETS ||
        source->logicOpEnable > VK_TRUE ||
        source->attachmentCount != target_count || !source->pAttachments)
        return false;
    memset(destination, 0, sizeof(*destination));
    *dual_source_blend = false;
    destination->target_count = target_count;
    destination->logic_enable = source->logicOpEnable;
    if (source->logicOpEnable) {
        if (!logic_operation(source->logicOp,
                &destination->logic_operation))
            return false;
    } else {
        destination->logic_operation = AGC_GFX1013_LOGIC_COPY;
    }
    memcpy(destination->constants, source->blendConstants,
        sizeof(destination->constants));
    for (uint32_t i = 0u; i < target_count; ++i) {
        const VkPipelineColorBlendAttachmentState *attachment =
            &source->pAttachments[i];
        AgcGfx1013ColorBlendTargetState *target =
            &destination->targets[i];
        if (attachment->colorWriteMask & ~component_mask)
            return false;
        target->enable = source->logicOpEnable ? VK_FALSE :
            attachment->blendEnable;
        target->write_mask = attachment->colorWriteMask;
        if (!target->enable) {
            target->color_source = AGC_GFX1013_BLEND_ONE;
            target->color_destination = AGC_GFX1013_BLEND_ZERO;
            target->color_operation = AGC_GFX1013_BLEND_OP_ADD;
            target->alpha_source = AGC_GFX1013_BLEND_ONE;
            target->alpha_destination = AGC_GFX1013_BLEND_ZERO;
            target->alpha_operation = AGC_GFX1013_BLEND_OP_ADD;
            continue;
        }
        const VkBlendFactor factors[] = {
            attachment->srcColorBlendFactor,
            attachment->dstColorBlendFactor,
            attachment->srcAlphaBlendFactor,
            attachment->dstAlphaBlendFactor,
        };
        for (uint32_t j = 0; j < sizeof(factors) / sizeof(factors[0]); ++j) {
            if (factors[j] >= VK_BLEND_FACTOR_SRC1_COLOR &&
                factors[j] <= VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA) {
                if (i != 0u)
                    return false;
                *dual_source_blend = true;
            }
        }
        if (!blend_factor(attachment->srcColorBlendFactor,
                &target->color_source) ||
            !blend_factor(attachment->dstColorBlendFactor,
                &target->color_destination) ||
            !blend_operation(attachment->colorBlendOp,
                &target->color_operation) ||
            !blend_factor(attachment->srcAlphaBlendFactor,
                &target->alpha_source) ||
            !blend_factor(attachment->dstAlphaBlendFactor,
                &target->alpha_destination) ||
            !blend_operation(attachment->alphaBlendOp,
                &target->alpha_operation))
            return false;
        target->separate_alpha =
            target->alpha_source != target->color_source ||
            target->alpha_destination != target->color_destination ||
            target->alpha_operation != target->color_operation;
    }
    return true;
}

static bool layout_resource_usage(
    VkImageLayout layout, AgcGfx1013ResourceUsage *usage) {
    if (!usage) return false;
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        *usage = AGC_GFX1013_RESOURCE_USAGE_UNDEFINED; break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
    case VK_IMAGE_LAYOUT_GENERAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_HOST_READ; break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ; break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_COPY_SOURCE; break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION; break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        *usage = AGC_GFX1013_RESOURCE_USAGE_PRESENT; break;
    default:
        return false;
    }
    return true;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    if (!device || !pCreateInfo || !pImage ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO ||
        !pCreateInfo->extent.width || !pCreateInfo->extent.height ||
        !pCreateInfo->extent.depth || pCreateInfo->mipLevels != 1 ||
        !pCreateInfo->arrayLayers || pCreateInfo->samples != VK_SAMPLE_COUNT_1_BIT ||
        !format_bytes(pCreateInfo->format) ||
        ((pCreateInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
         (pCreateInfo->format != VK_FORMAT_R8G8B8A8_UNORM ||
          pCreateInfo->tiling != VK_IMAGE_TILING_LINEAR)) ||
        (pCreateInfo->flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                               VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                               VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
                               VK_IMAGE_CREATE_PROTECTED_BIT)))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext)
        if (next->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO &&
            ((const VkExternalMemoryImageCreateInfo *)next)->handleTypes != 0)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPs5Image *image = alloc_object(device, pAllocator, sizeof(*image),
                                     _Alignof(VkPs5Image));
    if (!image) return VK_ERROR_OUT_OF_HOST_MEMORY;
    image->type = pCreateInfo->imageType;
    image->format = pCreateInfo->format;
    image->extent = pCreateInfo->extent;
    image->mip_levels = pCreateInfo->mipLevels;
    image->array_layers = pCreateInfo->arrayLayers;
    image->samples = pCreateInfo->samples;
    image->tiling = pCreateInfo->tiling;
    image->usage = pCreateInfo->usage;
    image->alignment = 256u;
    AgcGfx1013DepthSurfaceFormat depth_format;
    bool is_depth_format = depth_surface_format(
        pCreateInfo->format, &depth_format);
    if (pCreateInfo->tiling == VK_IMAGE_TILING_OPTIMAL && is_depth_format) {
        if (pCreateInfo->imageType != VK_IMAGE_TYPE_2D ||
            pCreateInfo->extent.depth != 1u) {
            vk_ps5_device_free(device, pAllocator, image);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
        const bool has_depth = pCreateInfo->format != VK_FORMAT_S8_UINT;
        const bool has_stencil = pCreateInfo->format == VK_FORMAT_S8_UINT ||
            pCreateInfo->format == VK_FORMAT_D16_UNORM_S8_UINT ||
            pCreateInfo->format == VK_FORMAT_D32_SFLOAT_S8_UINT;
        const AgcGfx1013DepthSurfaceLayoutInput input = {
            .width = pCreateInfo->extent.width,
            .height = pCreateInfo->extent.height,
            .layer_count = pCreateInfo->arrayLayers,
            .mip_level_count = pCreateInfo->mipLevels,
            .sample_count = 1u,
            .format = depth_format,
            .depth_swizzle_mode = has_depth ?
                AGC_GFX1013_SWIZZLE_64KB_Z_X : 0u,
            .stencil_swizzle_mode = has_stencil ?
                AGC_GFX1013_SWIZZLE_64KB_Z_X : 0u,
        };
        if (agcGfx1013GetDepthSurfaceLayout(
                &input, &image->depth_layout) != AGC_OK) {
            vk_ps5_device_free(device, pAllocator, image);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
        image->is_depth_surface = VK_TRUE;
        image->depth_format = depth_format;
        image->alignment = AGC_GFX1013_64KB_SURFACE_ALIGNMENT;
        VkDeviceSize size = image->depth_layout.depth.allocation_size;
        if (has_stencil) {
            VkDeviceSize stencil_alignment =
                image->depth_layout.stencil.alignment;
            if (!stencil_alignment || size > UINT64_MAX - stencil_alignment + 1u) {
                vk_ps5_device_free(device, pAllocator, image);
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
            image->stencil_plane_offset =
                (size + stencil_alignment - 1u) & ~(stencil_alignment - 1u);
            if (image->depth_layout.stencil.allocation_size >
                    UINT64_MAX - image->stencil_plane_offset) {
                vk_ps5_device_free(device, pAllocator, image);
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
            size = image->stencil_plane_offset +
                image->depth_layout.stencil.allocation_size;
        }
        image->size = size;
        *pImage = (VkImage)image;
        return VK_SUCCESS;
    }
    if (pCreateInfo->tiling != VK_IMAGE_TILING_LINEAR &&
        pCreateInfo->tiling != VK_IMAGE_TILING_OPTIMAL) {
        vk_ps5_device_free(device, pAllocator, image);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    uint64_t row_bytes =
        (uint64_t)image->extent.width * format_bytes(image->format);
    if (row_bytes > UINT64_MAX - 255u) {
        vk_ps5_device_free(device, pAllocator, image);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    image->row_pitch = (row_bytes + 255u) & ~(VkDeviceSize)255u;
    if (image->extent.height > UINT64_MAX / image->row_pitch) {
        vk_ps5_device_free(device, pAllocator, image);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    image->depth_pitch = image->row_pitch * image->extent.height;
    if (image->extent.depth > UINT64_MAX / image->depth_pitch) {
        vk_ps5_device_free(device, pAllocator, image);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    image->array_pitch = image->depth_pitch * image->extent.depth;
    if (image->array_layers > UINT64_MAX / image->array_pitch) {
        vk_ps5_device_free(device, pAllocator, image);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    image->size = image->array_pitch * image->array_layers;
    *pImage = (VkImage)image;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator) {
    if (image) vk_ps5_device_free(device, pAllocator, (void *)image);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageMemoryRequirements(VkDevice device, VkImage image_handle,
                             VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!image_handle || !pMemoryRequirements) return;
    VkPs5Image *image = (VkPs5Image *)image_handle;
    pMemoryRequirements->size = image->size;
    pMemoryRequirements->alignment = image->alignment;
    pMemoryRequirements->memoryTypeBits = image->is_depth_surface ? 0x2 : 0x3;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSparseMemoryRequirements(VkDevice device, VkImage image,
                                   uint32_t *pSparseMemoryRequirementCount,
                                   VkSparseImageMemoryRequirements *pSparseMemoryRequirements) {
    (void)device; (void)image; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory(VkDevice device, VkImage image_handle, VkDeviceMemory memory,
                  VkDeviceSize memoryOffset) {
    (void)device;
    VkPs5Image *image = (VkPs5Image *)image_handle;
    if (!image || !memory || memoryOffset % image->alignment != 0 ||
        memoryOffset > vk_ps5_memory_size(memory) ||
        image->size > vk_ps5_memory_size(memory) - memoryOffset)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    image->memory = memory;
    image->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSubresourceLayout(VkDevice device, VkImage image_handle,
                            const VkImageSubresource *pSubresource,
                            VkSubresourceLayout *pLayout) {
    (void)device;
    VkPs5Image *image = (VkPs5Image *)image_handle;
    if (!image || !pSubresource || !pLayout) return;
    pLayout->offset = image->array_pitch * pSubresource->arrayLayer;
    pLayout->size = image->array_pitch;
    pLayout->rowPitch = image->row_pitch;
    pLayout->arrayPitch = image->array_pitch;
    pLayout->depthPitch = image->depth_pitch;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                            VkDeviceSize *pCommittedMemoryInBytes) {
    (void)device;
    if (pCommittedMemoryInBytes) *pCommittedMemoryInBytes = vk_ps5_memory_size(memory);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
    if (!device || !pCreateInfo || !pCommandPool ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO ||
        pCreateInfo->queueFamilyIndex != 0 ||
        (pCreateInfo->flags & VK_COMMAND_POOL_CREATE_PROTECTED_BIT))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5CommandPool *pool = alloc_object(device, pAllocator, sizeof(*pool),
                                          _Alignof(VkPs5CommandPool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->device = device;
    pool->queue_family_index = pCreateInfo->queueFamilyIndex;
    *pCommandPool = (VkCommandPool)pool;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                     const VkAllocationCallbacks *pAllocator) {
    VkPs5CommandPool *pool = (VkPs5CommandPool *)commandPool;
    if (!pool) return;
    while (pool->buffers) {
        VkPs5CommandBuffer *command = pool->buffers;
        pool->buffers = command->next;
        agcGpuMemoryFreeFlexible(&command->vertex_table_memory);
        vk_ps5_device_free(device, NULL, command->dcb_storage);
        vk_ps5_device_free(device, NULL, command);
    }
    vk_ps5_device_free(device, pAllocator, pool);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                   VkCommandPoolResetFlags flags) {
    (void)device; (void)flags;
    VkPs5CommandPool *pool = (VkPs5CommandPool *)commandPool;
    if (!pool) return VK_ERROR_INITIALIZATION_FAILED;
    for (VkPs5CommandBuffer *command = pool->buffers; command; command = command->next) {
        command->state = VK_PS5_COMMAND_INITIAL;
        command->record_error = VK_SUCCESS;
        command->compute_defaults_emitted = VK_FALSE;
        command->bound_compute = NULL;
        command->bound_graphics = NULL;
        command->active_render_pass = NULL;
        command->active_framebuffer = NULL;
        command->active_query_pool = NULL;
        command->index_buffer = NULL;
        memset(command->vertex_buffers, 0, sizeof(command->vertex_buffers));
        command->vertex_table_offset = 0u;
        command->last_indirect_descriptor_table = 0u;
        command->last_indirect_descriptor_table_offset = 0u;
        command->last_indirect_descriptor_register = 0u;
        agcCbReset(&command->dcb, command->dcb_storage, command->dcb_size);
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkTrimCommandPool(VkDevice device, VkCommandPool commandPool,
                  VkCommandPoolTrimFlags flags) {
    (void)device; (void)commandPool; (void)flags;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                         VkCommandBuffer *pCommandBuffers) {
    if (!device || !pAllocateInfo || !pCommandBuffers ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO ||
        !pAllocateInfo->commandPool || !pAllocateInfo->commandBufferCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5CommandPool *pool = (VkPs5CommandPool *)pAllocateInfo->commandPool;
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i)
        pCommandBuffers[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i) {
        VkPs5CommandBuffer *command = alloc_object(device, NULL, sizeof(*command),
                                                    _Alignof(VkPs5CommandBuffer));
        if (!command) {
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i, pCommandBuffers);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        command->device = device;
        command->pool = pAllocateInfo->commandPool;
        command->level = pAllocateInfo->level;
        command->state = VK_PS5_COMMAND_INITIAL;
        command->dcb_size = VK_PS5_DCB_SIZE;
        command->dcb_storage = vk_ps5_device_alloc(
            device, NULL, command->dcb_size, 256,
            VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
        if (!command->dcb_storage) {
            vk_ps5_device_free(device, NULL, command);
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i, pCommandBuffers);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        agcCbInit(&command->dcb, command->dcb_storage, command->dcb_size);
        if (agcGpuMemoryAllocateFlexible(&command->vertex_table_memory,
                VK_PS5_VERTEX_TABLE_SIZE, 256u,
                "vulkan_ps5_vertex_tables") != AGC_OK) {
            vk_ps5_device_free(device, NULL, command->dcb_storage);
            vk_ps5_device_free(device, NULL, command);
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i,
                                 pCommandBuffers);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        VkResult result = vk_ps5_set_device_loader_data(device, command);
        if (result != VK_SUCCESS) {
            agcGpuMemoryFreeFlexible(&command->vertex_table_memory);
            vk_ps5_device_free(device, NULL, command->dcb_storage);
            vk_ps5_device_free(device, NULL, command);
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i, pCommandBuffers);
            return result;
        }
        command->next = pool->buffers;
        pool->buffers = command;
        pCommandBuffers[i] = (VkCommandBuffer)command;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                     uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    VkPs5CommandPool *pool = (VkPs5CommandPool *)commandPool;
    if (!pCommandBuffers) return;
    for (uint32_t i = 0; i < commandBufferCount; ++i) {
        VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)pCommandBuffers[i];
        if (!command || !pool) continue;
        VkPs5CommandBuffer **link = &pool->buffers;
        while (*link && *link != command) link = &(*link)->next;
        if (*link) {
            *link = command->next;
            agcGpuMemoryFreeFlexible(&command->vertex_table_memory);
            vk_ps5_device_free(device, NULL, command->dcb_storage);
            vk_ps5_device_free(device, NULL, command);
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                     const VkCommandBufferBeginInfo *pBeginInfo) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || !pBeginInfo ||
        pBeginInfo->sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO ||
        command->state == VK_PS5_COMMAND_RECORDING || command->state == VK_PS5_COMMAND_PENDING)
        return VK_ERROR_INITIALIZATION_FAILED;
    command->state = VK_PS5_COMMAND_RECORDING;
    command->record_error = VK_SUCCESS;
    command->compute_defaults_emitted = VK_FALSE;
    command->bound_compute = NULL;
    command->bound_graphics = NULL;
    command->active_render_pass = NULL;
    command->active_framebuffer = NULL;
    command->active_query_pool = NULL;
    command->index_buffer = NULL;
    command->dynamic_line_width_set = VK_FALSE;
    command->dynamic_depth_bias_set = VK_FALSE;
    memset(command->vertex_buffers, 0, sizeof(command->vertex_buffers));
    command->vertex_table_offset = 0u;
    command->last_indirect_descriptor_table = 0u;
    command->last_indirect_descriptor_table_offset = 0u;
    command->last_indirect_descriptor_register = 0u;
    memset(command->compute_sets, 0, sizeof(command->compute_sets));
    memset(command->graphics_sets, 0, sizeof(command->graphics_sets));
    agcCbReset(&command->dcb, command->dcb_storage, command->dcb_size);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (command->record_error != VK_SUCCESS || command->active_render_pass ||
        command->active_query_pool) {
        VkResult result = command->record_error != VK_SUCCESS ?
            command->record_error : VK_ERROR_INITIALIZATION_FAILED;
        command->state = VK_PS5_COMMAND_INITIAL;
        return result;
    }
    command->state = VK_PS5_COMMAND_EXECUTABLE;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    (void)flags;
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || command->state == VK_PS5_COMMAND_PENDING)
        return VK_ERROR_INITIALIZATION_FAILED;
    command->state = VK_PS5_COMMAND_INITIAL;
    command->record_error = VK_SUCCESS;
    command->compute_defaults_emitted = VK_FALSE;
    command->bound_compute = NULL;
    command->bound_graphics = NULL;
    command->active_render_pass = NULL;
    command->active_framebuffer = NULL;
    command->active_query_pool = NULL;
    command->index_buffer = NULL;
    command->dynamic_line_width_set = VK_FALSE;
    command->dynamic_depth_bias_set = VK_FALSE;
    memset(command->vertex_buffers, 0, sizeof(command->vertex_buffers));
    command->vertex_table_offset = 0u;
    command->last_indirect_descriptor_table = 0u;
    command->last_indirect_descriptor_table_offset = 0u;
    command->last_indirect_descriptor_register = 0u;
    memset(command->compute_sets, 0, sizeof(command->compute_sets));
    memset(command->graphics_sets, 0, sizeof(command->graphics_sets));
    agcCbReset(&command->dcb, command->dcb_storage, command->dcb_size);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
    if (!device || !pCreateInfo || !pView ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO ||
        !pCreateInfo->image || !format_bytes(pCreateInfo->format))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkPs5Image *image = (VkPs5Image *)pCreateInfo->image;
    VkImageAspectFlags valid_aspects = image->is_depth_surface ?
        (image->format == VK_FORMAT_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT :
         image->format == VK_FORMAT_D16_UNORM_S8_UINT ||
         image->format == VK_FORMAT_D32_SFLOAT_S8_UINT ?
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT :
            VK_IMAGE_ASPECT_DEPTH_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
    if (pCreateInfo->viewType != VK_IMAGE_VIEW_TYPE_2D ||
        pCreateInfo->format != image->format ||
        !pCreateInfo->subresourceRange.aspectMask ||
        (pCreateInfo->subresourceRange.aspectMask & ~valid_aspects) ||
        pCreateInfo->subresourceRange.baseMipLevel != 0u ||
        pCreateInfo->subresourceRange.levelCount != 1u ||
        pCreateInfo->subresourceRange.baseArrayLayer != 0u ||
        pCreateInfo->subresourceRange.layerCount != 1u)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPs5ImageView *view = alloc_object(device, pAllocator, sizeof(*view),
                                        _Alignof(VkPs5ImageView));
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->image = pCreateInfo->image;
    view->format = pCreateInfo->format;
    view->components = pCreateInfo->components;
    *pView = (VkImageView)view;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyImageView(VkDevice device, VkImageView imageView,
                   const VkAllocationCallbacks *pAllocator) {
    if (imageView) vk_ps5_device_free(device, pAllocator, (void *)imageView);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkBufferView *pView) {
    if (!device || !pCreateInfo || !pView ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO ||
        !pCreateInfo->buffer || !format_bytes(pCreateInfo->format))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkPs5Buffer *buffer = (VkPs5Buffer *)pCreateInfo->buffer;
    if (pCreateInfo->offset > buffer->size) return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceSize range = pCreateInfo->range == VK_WHOLE_SIZE ?
        buffer->size - pCreateInfo->offset : pCreateInfo->range;
    if (range > buffer->size - pCreateInfo->offset)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5BufferView *view = alloc_object(device, pAllocator, sizeof(*view),
                                         _Alignof(VkPs5BufferView));
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->buffer = pCreateInfo->buffer;
    view->format = pCreateInfo->format;
    *pView = (VkBufferView)view;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                    const VkAllocationCallbacks *pAllocator) {
    if (bufferView) vk_ps5_device_free(device, pAllocator, (void *)bufferView);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                    const VkBindBufferMemoryInfo *pBindInfos) {
    if (bindInfoCount && !pBindInfos) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        if (pBindInfos[i].sType != VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)pBindInfos[i].pNext;
             next; next = next->pNext) {
            if (next->sType == VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO) {
                const VkBindBufferMemoryDeviceGroupInfo *group =
                    (const VkBindBufferMemoryDeviceGroupInfo *)next;
                if (group->deviceIndexCount > 1 ||
                    (group->deviceIndexCount == 1 &&
                     (!group->pDeviceIndices || group->pDeviceIndices[0] != 0)))
                    return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
        VkResult result = vkBindBufferMemory(device, pBindInfos[i].buffer,
                                             pBindInfos[i].memory,
                                             pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                   const VkBindImageMemoryInfo *pBindInfos) {
    if (bindInfoCount && !pBindInfos) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        if (pBindInfos[i].sType != VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)pBindInfos[i].pNext;
             next; next = next->pNext)
            if (next->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            else if (next->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO) {
                const VkBindImageMemoryDeviceGroupInfo *group =
                    (const VkBindImageMemoryDeviceGroupInfo *)next;
                if (group->splitInstanceBindRegionCount != 0 ||
                    group->deviceIndexCount > 1 ||
                    (group->deviceIndexCount == 1 &&
                     (!group->pDeviceIndices || group->pDeviceIndices[0] != 0)))
                    return VK_ERROR_INITIALIZATION_FAILED;
            }
        VkResult result = vkBindImageMemory(device, pBindInfos[i].image,
                                            pBindInfos[i].memory,
                                            pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

static void fill_dedicated_requirements(void *pNext) {
    for (VkBaseOutStructure *next = (VkBaseOutStructure *)pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
            VkMemoryDedicatedRequirements *dedicated =
                (VkMemoryDedicatedRequirements *)next;
            dedicated->prefersDedicatedAllocation = VK_FALSE;
            dedicated->requiresDedicatedAllocation = VK_FALSE;
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements2(VkDevice device,
                               const VkBufferMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pInfo || !pMemoryRequirements) return;
    vkGetBufferMemoryRequirements(device, pInfo->buffer,
                                  &pMemoryRequirements->memoryRequirements);
    fill_dedicated_requirements(pMemoryRequirements->pNext);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageMemoryRequirements2(VkDevice device,
                              const VkImageMemoryRequirementsInfo2 *pInfo,
                              VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pInfo || !pMemoryRequirements) return;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO) {
            memset(&pMemoryRequirements->memoryRequirements, 0,
                   sizeof(pMemoryRequirements->memoryRequirements));
            return;
        }
    }
    vkGetImageMemoryRequirements(device, pInfo->image,
                                 &pMemoryRequirements->memoryRequirements);
    fill_dedicated_requirements(pMemoryRequirements->pNext);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSparseMemoryRequirements2(
    VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements) {
    (void)device; (void)pInfo; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool) {
    if (!device || !pCreateInfo || !pQueryPool ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO ||
        !pCreateInfo->queryCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pCreateInfo->queryType != VK_QUERY_TYPE_OCCLUSION)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if ((size_t)pCreateInfo->queryCount > SIZE_MAX / VK_PS5_QUERY_SLOT_SIZE)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkPs5QueryPool *pool = alloc_object(device, pAllocator, sizeof(*pool),
                                        _Alignof(VkPs5QueryPool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = pCreateInfo->queryType;
    pool->count = pCreateInfo->queryCount;
    size_t size = (size_t)pool->count * VK_PS5_QUERY_SLOT_SIZE;
    if (agcGpuMemoryAllocateFlexible(&pool->memory, size, 256u,
            "vulkan_ps5_queries") != AGC_OK) {
        vk_ps5_device_free(device, pAllocator, pool);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    memset(pool->memory.cpu_address, 0, size);
    if (agcGpuMemoryFlush(&pool->memory, 0, size) != AGC_OK) {
        agcGpuMemoryFreeFlexible(&pool->memory);
        vk_ps5_device_free(device, pAllocator, pool);
        return VK_ERROR_DEVICE_LOST;
    }
    *pQueryPool = (VkQueryPool)pool;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                   const VkAllocationCallbacks *pAllocator) {
    if (queryPool) {
        VkPs5QueryPool *pool = (VkPs5QueryPool *)queryPool;
        agcGpuMemoryFreeFlexible(&pool->memory);
        vk_ps5_device_free(device, pAllocator, pool);
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkResetQueryPoolEXT(VkDevice device, VkQueryPool queryPool_handle,
                    uint32_t firstQuery, uint32_t queryCount) {
    (void)device;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)queryPool_handle;
    if (!pool || firstQuery > pool->count ||
        queryCount > pool->count - firstQuery)
        return;
    size_t offset = (size_t)firstQuery * VK_PS5_QUERY_SLOT_SIZE;
    size_t size = (size_t)queryCount * VK_PS5_QUERY_SLOT_SIZE;
    if (!size) return;
    memset((uint8_t *)pool->memory.cpu_address + offset, 0, size);
    (void)agcGpuMemoryFlush(&pool->memory, offset, size);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool_handle, uint32_t firstQuery,
                      uint32_t queryCount, size_t dataSize, void *pData,
                      VkDeviceSize stride, VkQueryResultFlags flags) {
    (void)device;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)queryPool_handle;
    if (!pool || !pData || firstQuery > pool->count || queryCount > pool->count - firstQuery)
        return VK_ERROR_INITIALIZATION_FAILED;
    size_t value_size = (flags & VK_QUERY_RESULT_64_BIT) ? sizeof(uint64_t) : sizeof(uint32_t);
    size_t entry_size = value_size +
        ((flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? value_size : 0);
    if (stride < entry_size || (queryCount && dataSize <
        (size_t)(queryCount - 1) * (size_t)stride + entry_size))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < queryCount; ++i) {
        size_t slot = (size_t)(firstQuery + i) * VK_PS5_QUERY_SLOT_SIZE;
        if (flags & VK_QUERY_RESULT_WAIT_BIT) {
            int32_t wait = agcGpuMemoryWait32(&pool->memory,
                slot + VK_PS5_QUERY_AVAILABILITY_OFFSET, 1u, 5000000u);
            if (wait != AGC_OK && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
                result = VK_NOT_READY;
        }
        if (agcGpuMemoryInvalidate(&pool->memory, slot,
                VK_PS5_QUERY_SLOT_SIZE) != AGC_OK)
            return VK_ERROR_DEVICE_LOST;
        const uint8_t *source =
            (const uint8_t *)pool->memory.cpu_address + slot;
        uint32_t available_word;
        memcpy(&available_word, source + VK_PS5_QUERY_AVAILABILITY_OFFSET,
               sizeof(available_word));
        VkBool32 available = available_word == 1u;
        uint64_t value = 0u;
        for (uint32_t rb = 0; rb < AGC_GFX1013_OCCLUSION_QUERY_MAX_RBS;
             ++rb) {
            uint64_t begin, end;
            memcpy(&begin, source + rb * 16u, sizeof(begin));
            memcpy(&end, source + rb * 16u + 8u, sizeof(end));
            if ((begin >> 63u) && (end >> 63u))
                value += (end & INT64_MAX) - (begin & INT64_MAX);
        }
        uint8_t *dst = (uint8_t *)pData + (size_t)i * (size_t)stride;
        if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
            if (flags & VK_QUERY_RESULT_64_BIT)
                memcpy(dst, &value, sizeof(value));
            else {
                uint32_t value32 = (uint32_t)value;
                memcpy(dst, &value32, sizeof(value32));
            }
        }
        if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            if (flags & VK_QUERY_RESULT_64_BIT) {
                uint64_t availability = available;
                memcpy(dst + value_size, &availability, sizeof(availability));
            } else {
                uint32_t availability = available;
                memcpy(dst + value_size, &availability, sizeof(availability));
            }
        }
        if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
            result = VK_NOT_READY;
    }
    return result;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
    if (!device || !pCreateInfo || !pShaderModule ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO ||
        pCreateInfo->codeSize < sizeof(uint32_t) || pCreateInfo->codeSize % 4 ||
        !pCreateInfo->pCode || pCreateInfo->pCode[0] != 0x07230203u)
        return VK_ERROR_INITIALIZATION_FAILED;
    size_t size = sizeof(VkPs5ShaderModule) + pCreateInfo->codeSize;
    VkPs5ShaderModule *module = alloc_object(device, pAllocator, size,
                                              _Alignof(VkPs5ShaderModule));
    if (!module) return VK_ERROR_OUT_OF_HOST_MEMORY;
    module->code_size = pCreateInfo->codeSize;
    memcpy(module->code, pCreateInfo->pCode, pCreateInfo->codeSize);
    *pShaderModule = (VkShaderModule)module;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                      const VkAllocationCallbacks *pAllocator) {
    if (shaderModule) vk_ps5_device_free(device, pAllocator, (void *)shaderModule);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkPipelineCache *pPipelineCache) {
    if (!device || !pCreateInfo || !pPipelineCache ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO ||
        (pCreateInfo->initialDataSize && !pCreateInfo->pInitialData))
        return VK_ERROR_INITIALIZATION_FAILED;
    static const uint8_t cache_uuid[] = "VulkanPS5-gfx1013";
    size_t initial_size = 0;
    if (pCreateInfo->initialDataSize >= sizeof(VkPipelineCacheHeaderVersionOne)) {
        const VkPipelineCacheHeaderVersionOne *header = pCreateInfo->pInitialData;
        if (header->headerSize == sizeof(*header) &&
            header->headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
            header->vendorID == 0x1002u && header->deviceID == 0x163fu &&
            memcmp(header->pipelineCacheUUID, cache_uuid, VK_UUID_SIZE) == 0)
            initial_size = pCreateInfo->initialDataSize;
    }
    size_t data_size = initial_size ? initial_size : sizeof(VkPipelineCacheHeaderVersionOne);
    size_t size = sizeof(VkPs5PipelineCache) + data_size;
    VkPs5PipelineCache *cache = alloc_object(device, pAllocator, size,
                                              _Alignof(VkPs5PipelineCache));
    if (!cache) return VK_ERROR_OUT_OF_HOST_MEMORY;
    cache->data_size = data_size;
    if (initial_size) {
        memcpy(cache->data, pCreateInfo->pInitialData, initial_size);
    } else {
        VkPipelineCacheHeaderVersionOne header = {
            .headerSize = sizeof(header),
            .headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE,
            .vendorID = 0x1002u,
            .deviceID = 0x163fu,
        };
        memcpy(header.pipelineCacheUUID, cache_uuid, VK_UUID_SIZE);
        memcpy(cache->data, &header, sizeof(header));
    }
    *pPipelineCache = (VkPipelineCache)cache;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                       const VkAllocationCallbacks *pAllocator) {
    if (pipelineCache) vk_ps5_device_free(device, pAllocator, (void *)pipelineCache);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache_handle,
                       size_t *pDataSize, void *pData) {
    (void)device;
    VkPs5PipelineCache *cache = (VkPs5PipelineCache *)pipelineCache_handle;
    if (!cache || !pDataSize) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pData) {
        *pDataSize = cache->data_size;
        return VK_SUCCESS;
    }
    size_t written = *pDataSize < cache->data_size ? *pDataSize : cache->data_size;
    if (written) memcpy(pData, cache->data, written);
    *pDataSize = written;
    return written < cache->data_size ? VK_INCOMPLETE : VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                      uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches) {
    (void)device; (void)dstCache;
    return srcCacheCount && !pSrcCaches ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS;
}

static bool psbc_descriptor_type(VkDescriptorType source,
                                 OpenAgcPsbcDescriptorType *dest) {
    switch (source) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_SAMPLER; return true;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER; return true;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        *dest = OPENAGC_PSBC_DESCRIPTOR_SAMPLED_IMAGE; return true;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        *dest = OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE; return true;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_UNIFORM_TEXEL_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_STORAGE_TEXEL_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        *dest = OPENAGC_PSBC_DESCRIPTOR_UNIFORM_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        *dest = OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        *dest = OPENAGC_PSBC_DESCRIPTOR_INPUT_ATTACHMENT; return true;
    default:
        return false;
    }
}

static bool psbc_vertex_format(VkFormat source, OpenAgcPsbcVertexFormat *dest) {
    switch (source) {
    case VK_FORMAT_R32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32_SFLOAT; return true;
    case VK_FORMAT_R32G32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32G32_SFLOAT; return true;
    case VK_FORMAT_R32G32B32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32_SFLOAT; return true;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32A32_SFLOAT; return true;
    case VK_FORMAT_R8G8B8A8_UNORM:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R8G8B8A8_UNORM; return true;
    case VK_FORMAT_R16G16_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R16G16_SFLOAT; return true;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R16G16B16A16_SFLOAT; return true;
    default:
        return false;
    }
}

static uint32_t psbc_vertex_format_size(OpenAgcPsbcVertexFormat format) {
    switch (format) {
    case OPENAGC_PSBC_VERTEX_FORMAT_R32_SFLOAT:
    case OPENAGC_PSBC_VERTEX_FORMAT_R8G8B8A8_UNORM:
    case OPENAGC_PSBC_VERTEX_FORMAT_R16G16_SFLOAT:
        return 4u;
    case OPENAGC_PSBC_VERTEX_FORMAT_R32G32_SFLOAT:
    case OPENAGC_PSBC_VERTEX_FORMAT_R16G16B16A16_SFLOAT:
        return 8u;
    case OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32_SFLOAT:
        return 12u;
    case OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32A32_SFLOAT:
        return 16u;
    default:
        return 0u;
    }
}

static VkResult psbc_result(OpenAgcPsbcResult result) {
    switch (result) {
    case OPENAGC_PSBC_SUCCESS: return VK_SUCCESS;
    case OPENAGC_PSBC_ERROR_OUT_OF_MEMORY: return VK_ERROR_OUT_OF_HOST_MEMORY;
    case OPENAGC_PSBC_ERROR_UNSUPPORTED_STAGE:
    case OPENAGC_PSBC_ERROR_UNSUPPORTED_PIPELINE:
        return VK_ERROR_FEATURE_NOT_PRESENT;
    case OPENAGC_PSBC_ERROR_INVALID_SPIRV:
    case OPENAGC_PSBC_ERROR_NIR:
    case OPENAGC_PSBC_ERROR_ACO:
        return VK_ERROR_INVALID_SHADER_NV;
    default:
        return VK_ERROR_INITIALIZATION_FAILED;
    }
}

typedef struct PsbcSpecialization {
    OpenAgcPsbcSpecializationConstant constants[
        OPENAGC_PSBC_MAX_SPECIALIZATION_CONSTANTS];
    uint32_t count;
    const void *data;
    size_t data_size;
} PsbcSpecialization;

static VkResult translate_specialization(const VkSpecializationInfo *source,
                                         PsbcSpecialization *dest) {
    memset(dest, 0, sizeof(*dest));
    if (!source) return VK_SUCCESS;
    if (source->mapEntryCount > OPENAGC_PSBC_MAX_SPECIALIZATION_CONSTANTS ||
        (source->mapEntryCount && !source->pMapEntries) ||
        (source->dataSize && !source->pData))
        return VK_ERROR_INITIALIZATION_FAILED;
    dest->count = source->mapEntryCount;
    dest->data = source->pData;
    dest->data_size = source->dataSize;
    for (uint32_t i = 0; i < dest->count; ++i) {
        dest->constants[i].constant_id = source->pMapEntries[i].constantID;
        dest->constants[i].offset = source->pMapEntries[i].offset;
        dest->constants[i].size = source->pMapEntries[i].size;
    }
    return VK_SUCCESS;
}

static VkResult compile_stage(const VkPipelineShaderStageCreateInfo *stage,
                              OpenAgcPsbcStage psbc_stage,
                              const VkPipelineShaderStageCreateInfo *pre_stage,
                              OpenAgcPsbcStage psbc_pre_stage,
                              const VkPipelineShaderStageCreateInfo *interface_stage,
                              OpenAgcPsbcStage psbc_interface_stage,
                              const OpenAgcPsbcPipelineContext *context,
                              OpenAgcPsbcOutput *output) {
    if (!stage || stage->sType != VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ||
        !stage->module || !stage->pName)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pre_stage &&
        (pre_stage->sType != VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ||
         !pre_stage->module || !pre_stage->pName))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (interface_stage &&
        (interface_stage->sType !=
             VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ||
         !interface_stage->module || !interface_stage->pName))
        return VK_ERROR_INITIALIZATION_FAILED;
    const VkPs5ShaderModule *module = (const VkPs5ShaderModule *)stage->module;
    const VkPs5ShaderModule *pre_module = pre_stage ?
        (const VkPs5ShaderModule *)pre_stage->module : NULL;
    const VkPs5ShaderModule *interface_module = interface_stage ?
        (const VkPs5ShaderModule *)interface_stage->module : NULL;
    PsbcSpecialization specialization, pre_specialization,
                       interface_specialization;
    VkResult result = translate_specialization(stage->pSpecializationInfo,
                                                &specialization);
    if (result != VK_SUCCESS) return result;
    result = translate_specialization(pre_stage ? pre_stage->pSpecializationInfo : NULL,
                                      &pre_specialization);
    if (result != VK_SUCCESS) return result;
    result = translate_specialization(
        interface_stage ? interface_stage->pSpecializationInfo : NULL,
        &interface_specialization);
    if (result != VK_SUCCESS) return result;
    const OpenAgcPsbcCompileInfo info = {
        .api_version = OPENAGC_PSBC_API_VERSION,
        .stage = psbc_stage,
        .spirv = module->code,
        .spirv_size = module->code_size,
        .entry_point = stage->pName,
        .pre_stage = psbc_pre_stage,
        .pre_spirv = pre_module ? pre_module->code : NULL,
        .pre_spirv_size = pre_module ? pre_module->code_size : 0,
        .pre_entry_point = pre_stage ? pre_stage->pName : NULL,
        .pre_specialization_constants = pre_specialization.count ?
            pre_specialization.constants : NULL,
        .pre_specialization_constant_count = pre_specialization.count,
        .pre_specialization_data = pre_specialization.data,
        .pre_specialization_data_size = pre_specialization.data_size,
        .interface_stage = psbc_interface_stage,
        .interface_spirv = interface_module ? interface_module->code : NULL,
        .interface_spirv_size = interface_module ?
            interface_module->code_size : 0,
        .interface_entry_point = interface_stage ? interface_stage->pName : NULL,
        .interface_specialization_constants = interface_specialization.count ?
            interface_specialization.constants : NULL,
        .interface_specialization_constant_count =
            interface_specialization.count,
        .interface_specialization_data = interface_specialization.data,
        .interface_specialization_data_size = interface_specialization.data_size,
        .specialization_constants = specialization.count ?
            specialization.constants : NULL,
        .specialization_constant_count = specialization.count,
        .specialization_data = specialization.data,
        .specialization_data_size = specialization.data_size,
        .pipeline = context,
        .optimize = true,
    };
    return psbc_result(openagcPsbcCompile(&info, output));
}

static VkResult build_tessellation_layouts(VkPs5Pipeline *pipeline)
{
    const OpenAgcPsbcMetadata *control = &pipeline->stages[0].metadata;
    const OpenAgcPsbcMetadata *evaluation = &pipeline->stages[1].metadata;

    if (!control->tessellation_patch_count ||
        control->tessellation_patch_count !=
            evaluation->tessellation_patch_count ||
        !control->tessellation_input_control_points ||
        !control->tessellation_output_control_points ||
        !control->tessellation_lds_size ||
        control->tessellation_lds_size > 65536u ||
        control->tessellation_output_control_points !=
            evaluation->tessellation_output_control_points)
        return VK_ERROR_INITIALIZATION_FAILED;
    const AgcGfx1013TessellationLayoutState state = {
        .patch_count = control->tessellation_patch_count,
        .input_control_points =
            control->tessellation_input_control_points,
        .output_control_points =
            control->tessellation_output_control_points,
        .vertex_output_count =
            control->tessellation_vertex_output_count,
        .control_output_count =
            control->tessellation_control_output_count,
        .primitive_mode = evaluation->tessellation_primitive_mode,
        .tes_reads_tess_factors =
            evaluation->tessellation_reads_factors,
    };
    return agcGfx1013BuildTessellationOffchipLayouts(
               &state, &pipeline->tcs_offchip_layout,
               &pipeline->tes_offchip_layout) == AGC_OK ?
        VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}

static void free_pipeline(VkDevice device, const VkAllocationCallbacks *allocator,
                          VkPs5Pipeline *pipeline) {
    if (!pipeline) return;
    for (uint32_t i = 0; i < pipeline->stage_count; ++i) {
        agcGpuMemoryFreeFlexible(&pipeline->runtime[i].code_memory);
        agcGpuMemoryFreeFlexible(&pipeline->runtime[i].front_code_memory);
        openagcPsbcFreeOutput(&pipeline->stages[i]);
    }
    vk_ps5_device_free(device, allocator, pipeline);
}

static VkResult finalize_runtime_shader(
    VkDevice device, const VkAllocationCallbacks *allocator,
    OpenAgcPsbcOutput *output, VkPs5RuntimeShader *runtime) {
    if (agcShaderRecordRelocateBinary(&runtime->record,
            output->shader.data, output->shader.size) != AGC_OK ||
        output->metadata.code_offset > output->shader.size ||
        output->metadata.code_size >
            output->shader.size - output->metadata.code_offset)
        return VK_ERROR_INITIALIZATION_FAILED;
    runtime->code_size = output->metadata.code_size;
    (void)device;
    (void)allocator;
    if (agcGpuMemoryAllocateFlexible(&runtime->code_memory,
            runtime->code_size, 256u, "vulkan_ps5_shader") != AGC_OK)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    runtime->code = runtime->code_memory.cpu_address;
    memcpy(runtime->code,
        (const uint8_t *)output->shader.data + output->metadata.code_offset,
        runtime->code_size);
    if (agcGpuMemoryFlush(&runtime->code_memory, 0,
            runtime->code_size) != AGC_OK)
        return VK_ERROR_DEVICE_LOST;
    runtime->binding = (AgcGfx1013ShaderBinding){
        .record = &runtime->record,
        .sh_registers = (const AgcRegisterValue *)(uintptr_t)
            runtime->record.sh_registers,
        .num_sh_registers = runtime->record.num_sh_registers,
        .cx_registers = (const AgcRegisterValue *)(uintptr_t)
            runtime->record.cx_registers,
        .num_cx_registers = runtime->record.num_cx_registers,
        .code_address = runtime->code_memory.gpu_address,
    };
    if (output->front_shader.data) {
        if (agcShaderRecordRelocateBinary(&runtime->front_record,
                output->front_shader.data, output->front_shader.size) != AGC_OK ||
            output->metadata.front_code_offset > output->front_shader.size ||
            output->metadata.front_code_size >
                output->front_shader.size - output->metadata.front_code_offset ||
            (uint32_t)runtime->record.num_sh_registers +
                runtime->front_record.num_sh_registers > 64u)
            return VK_ERROR_INITIALIZATION_FAILED;
        runtime->front_code_size = output->metadata.front_code_size;
        if (agcGpuMemoryAllocateFlexible(&runtime->front_code_memory,
                runtime->front_code_size, 256u,
                "vulkan_ps5_shader_front") != AGC_OK)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        runtime->front_code = runtime->front_code_memory.cpu_address;
        memcpy(runtime->front_code,
            (const uint8_t *)output->front_shader.data +
                output->metadata.front_code_offset,
            runtime->front_code_size);
        if (agcGpuMemoryFlush(&runtime->front_code_memory, 0,
                runtime->front_code_size) != AGC_OK)
            return VK_ERROR_DEVICE_LOST;
        runtime->record.code = runtime->code_memory.gpu_address;
        runtime->front_record.code = runtime->front_code_memory.gpu_address;
        if (sceAgcFuseShaderHalves_0200(
                &runtime->fused_record, &runtime->front_record,
                &runtime->record, runtime->fused_registers) != AGC_OK)
            return VK_ERROR_INITIALIZATION_FAILED;
        runtime->binding.record = &runtime->fused_record;
        runtime->binding.sh_registers = runtime->fused_registers;
        runtime->binding.num_sh_registers =
            runtime->fused_record.num_sh_registers;
    }
    return VK_SUCCESS;
}

static VkResult finalize_pipeline(
    VkDevice device, const VkAllocationCallbacks *allocator,
    VkPs5Pipeline *pipeline) {
    for (uint32_t i = 0; i < pipeline->stage_count; ++i) {
        VkResult result = finalize_runtime_shader(
            device, allocator, &pipeline->stages[i], &pipeline->runtime[i]);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                         uint32_t createInfoCount,
                         const VkComputePipelineCreateInfo *pCreateInfos,
                         const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    (void)pipelineCache;
    if (!device || !pPipelines || (createInfoCount && !pCreateInfos))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const VkComputePipelineCreateInfo *create = &pCreateInfos[i];
        if (create->sType != VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO ||
            create->stage.stage != VK_SHADER_STAGE_COMPUTE_BIT || !create->layout)
            return VK_ERROR_INITIALIZATION_FAILED;
        const VkPs5PipelineLayout *layout = (const VkPs5PipelineLayout *)create->layout;
        const OpenAgcPsbcPipelineContext context = {
            .descriptor_bindings = layout->bindings,
            .descriptor_binding_count = layout->binding_count,
            .push_constant_size = layout->push_constant_size,
        };
        VkPs5Pipeline *pipeline = alloc_object(device, pAllocator, sizeof(*pipeline),
                                                _Alignof(VkPs5Pipeline));
        if (!pipeline) return VK_ERROR_OUT_OF_HOST_MEMORY;
        VkResult result = compile_stage(&create->stage, OPENAGC_PSBC_STAGE_COMPUTE,
                                        NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                        NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                        &context, &pipeline->stages[0]);
        if (result != VK_SUCCESS) {
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        pipeline->stage_count = 1;
        pipeline->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        pipeline->stage_types[0] = OPENAGC_PSBC_STAGE_COMPUTE;
        result = finalize_pipeline(device, pAllocator, pipeline);
        if (result != VK_SUCCESS) {
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        pPipelines[i] = (VkPipeline)pipeline;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                          uint32_t createInfoCount,
                          const VkGraphicsPipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    (void)pipelineCache;
    if (!device || !pPipelines || (createInfoCount && !pCreateInfos))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const VkGraphicsPipelineCreateInfo *create = &pCreateInfos[i];
        if (create->sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO ||
            !create->layout || !create->renderPass || !create->pVertexInputState ||
            !create->pInputAssemblyState)
            return VK_ERROR_INITIALIZATION_FAILED;
        AgcGfx1013PrimitiveTopology translated_topology;
        if (!primitive_topology(create->pInputAssemblyState->topology,
                &translated_topology) ||
            create->pInputAssemblyState->primitiveRestartEnable)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (!create->pViewportState ||
            create->pViewportState->viewportCount != 1u ||
            create->pViewportState->scissorCount != 1u ||
            !create->pViewportState->pViewports ||
            !create->pViewportState->pScissors)
            return VK_ERROR_INITIALIZATION_FAILED;
        VkBool32 dynamic_depth_bias = VK_FALSE;
        VkBool32 dynamic_line_width = VK_FALSE;
        if (create->pDynamicState) {
            if (create->pDynamicState->dynamicStateCount &&
                !create->pDynamicState->pDynamicStates)
                return VK_ERROR_INITIALIZATION_FAILED;
            for (uint32_t dynamic_index = 0u;
                 dynamic_index < create->pDynamicState->dynamicStateCount;
                 ++dynamic_index) {
                switch (create->pDynamicState->pDynamicStates[dynamic_index]) {
                case VK_DYNAMIC_STATE_DEPTH_BIAS:
                    if (dynamic_depth_bias)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_depth_bias = VK_TRUE;
                    break;
                case VK_DYNAMIC_STATE_LINE_WIDTH:
                    if (dynamic_line_width)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_line_width = VK_TRUE;
                    break;
                default:
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
            }
        }
        if (!create->pRasterizationState || !create->pMultisampleState ||
            !create->pColorBlendState)
            return VK_ERROR_INITIALIZATION_FAILED;
        const VkPipelineRasterizationStateCreateInfo *raster =
            create->pRasterizationState;
        const VkPipelineMultisampleStateCreateInfo *multisample =
            create->pMultisampleState;
        const VkPipelineColorBlendStateCreateInfo *blend =
            create->pColorBlendState;
        AgcGfx1013PolygonMode translated_polygon_mode;
        if (raster->rasterizerDiscardEnable ||
            !polygon_mode(raster->polygonMode, &translated_polygon_mode) ||
            raster->cullMode != VK_CULL_MODE_NONE ||
            (!dynamic_line_width && (!(raster->lineWidth >= 1.0f) ||
                !(raster->lineWidth <= 64.0f))) ||
            multisample->rasterizationSamples != VK_SAMPLE_COUNT_1_BIT ||
            multisample->sampleShadingEnable || multisample->alphaToCoverageEnable ||
            multisample->alphaToOneEnable ||
            !blend->attachmentCount ||
            blend->attachmentCount > AGC_GFX1013_MAX_COLOR_TARGETS ||
            !blend->pAttachments)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        const VkViewport *viewport = create->pViewportState->pViewports;
        const VkRect2D *scissor = create->pViewportState->pScissors;
        if (viewport->x != 0.0f || viewport->y != 0.0f ||
            viewport->width <= 0.0f || viewport->height <= 0.0f ||
            viewport->width > 16384.0f || viewport->height > 16384.0f ||
            viewport->width != (float)(uint32_t)viewport->width ||
            viewport->height != (float)(uint32_t)viewport->height ||
            viewport->minDepth != 0.0f || viewport->maxDepth != 1.0f ||
            scissor->offset.x < 0 || scissor->offset.y < 0 ||
            !scissor->extent.width || !scissor->extent.height ||
            (uint32_t)scissor->offset.x > 16384u ||
            scissor->extent.width > 16384u - (uint32_t)scissor->offset.x ||
            (uint32_t)scissor->offset.y > 16384u ||
            scissor->extent.height > 16384u - (uint32_t)scissor->offset.y)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        const VkPipelineShaderStageCreateInfo *vertex = NULL, *tess_control = NULL;
        const VkPipelineShaderStageCreateInfo *tess_evaluation = NULL;
        const VkPipelineShaderStageCreateInfo *geometry = NULL, *fragment = NULL;
        for (uint32_t j = 0; j < create->stageCount; ++j) {
            const VkPipelineShaderStageCreateInfo *stage = &create->pStages[j];
            const VkPipelineShaderStageCreateInfo **slot = NULL;
            switch (stage->stage) {
            case VK_SHADER_STAGE_VERTEX_BIT: slot = &vertex; break;
            case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: slot = &tess_control; break;
            case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: slot = &tess_evaluation; break;
            case VK_SHADER_STAGE_GEOMETRY_BIT: slot = &geometry; break;
            case VK_SHADER_STAGE_FRAGMENT_BIT: slot = &fragment; break;
            default: return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            if (*slot) return VK_ERROR_INITIALIZATION_FAILED;
            *slot = stage;
        }
        if (!vertex || !fragment) return VK_ERROR_FEATURE_NOT_PRESENT;
        if ((tess_control == NULL) != (tess_evaluation == NULL))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if ((tess_control && create->pInputAssemblyState->topology !=
                VK_PRIMITIVE_TOPOLOGY_PATCH_LIST) ||
            (!tess_control && create->pInputAssemblyState->topology ==
                VK_PRIMITIVE_TOPOLOGY_PATCH_LIST))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (tess_control && (!create->pTessellationState ||
            !create->pTessellationState->patchControlPoints))
            return VK_ERROR_INITIALIZATION_FAILED;

        const VkPipelineVertexInputStateCreateInfo *vertex_input = create->pVertexInputState;
        if (vertex_input->vertexAttributeDescriptionCount >
                OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES ||
            vertex_input->vertexBindingDescriptionCount >
                VK_PS5_MAX_VERTEX_BINDINGS ||
            (vertex_input->vertexAttributeDescriptionCount &&
                !vertex_input->pVertexAttributeDescriptions) ||
            (vertex_input->vertexBindingDescriptionCount &&
                !vertex_input->pVertexBindingDescriptions))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        uint32_t vertex_binding_mask = 0u;
        uint32_t vertex_strides[VK_PS5_MAX_VERTEX_BINDINGS] = {0};
        VkVertexInputRate vertex_rates[VK_PS5_MAX_VERTEX_BINDINGS] = {0};
        uint32_t vertex_divisors[VK_PS5_MAX_VERTEX_BINDINGS] = {0};
        for (uint32_t j = 0;
             j < vertex_input->vertexBindingDescriptionCount; ++j) {
            const VkVertexInputBindingDescription *binding =
                &vertex_input->pVertexBindingDescriptions[j];
            if (binding->binding >= VK_PS5_MAX_VERTEX_BINDINGS ||
                (binding->inputRate != VK_VERTEX_INPUT_RATE_VERTEX &&
                 binding->inputRate != VK_VERTEX_INPUT_RATE_INSTANCE) ||
                binding->stride == 0u || binding->stride > 2048u ||
                (vertex_binding_mask & (1u << binding->binding)))
                return VK_ERROR_FEATURE_NOT_PRESENT;
            vertex_binding_mask |= 1u << binding->binding;
            vertex_strides[binding->binding] = binding->stride;
            vertex_rates[binding->binding] = binding->inputRate;
            vertex_divisors[binding->binding] =
                binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE ? 1u : 0u;
        }
        const VkPipelineVertexInputDivisorStateCreateInfoEXT *divisor_state = NULL;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)vertex_input->pNext;
             next; next = next->pNext) {
            if (next->sType ==
                    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT) {
                if (divisor_state)
                    return VK_ERROR_INITIALIZATION_FAILED;
                divisor_state =
                    (const VkPipelineVertexInputDivisorStateCreateInfoEXT *)next;
            }
        }
        uint32_t divisor_binding_mask = 0u;
        if (divisor_state &&
            (divisor_state->vertexBindingDivisorCount &&
             !divisor_state->pVertexBindingDivisors))
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0;
             divisor_state && j < divisor_state->vertexBindingDivisorCount; ++j) {
            const VkVertexInputBindingDivisorDescriptionEXT *divisor =
                &divisor_state->pVertexBindingDivisors[j];
            if (divisor->binding >= VK_PS5_MAX_VERTEX_BINDINGS ||
                !(vertex_binding_mask & (1u << divisor->binding)) ||
                vertex_rates[divisor->binding] != VK_VERTEX_INPUT_RATE_INSTANCE ||
                divisor->divisor == 0u ||
                (divisor_binding_mask & (1u << divisor->binding)))
                return VK_ERROR_FEATURE_NOT_PRESENT;
            divisor_binding_mask |= 1u << divisor->binding;
            vertex_divisors[divisor->binding] = divisor->divisor;
        }
        OpenAgcPsbcVertexAttribute attributes[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
        uint32_t vertex_attribute_mask = 0u;
        for (uint32_t j = 0; j < vertex_input->vertexAttributeDescriptionCount; ++j) {
            const VkVertexInputAttributeDescription *source =
                &vertex_input->pVertexAttributeDescriptions[j];
            uint32_t stride = 0;
            bool found_binding = false;
            for (uint32_t k = 0; k < vertex_input->vertexBindingDescriptionCount; ++k) {
                const VkVertexInputBindingDescription *binding =
                    &vertex_input->pVertexBindingDescriptions[k];
                if (binding->binding == source->binding) {
                    stride = binding->stride;
                    found_binding = true;
                    break;
                }
            }
            if (!found_binding || source->location >=
                    OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES ||
                (vertex_attribute_mask & (1u << source->location)) ||
                !psbc_vertex_format(source->format, &attributes[j].format))
                return VK_ERROR_FORMAT_NOT_SUPPORTED;
            vertex_attribute_mask |= 1u << source->location;
            attributes[j].location = source->location;
            attributes[j].binding = source->binding;
            attributes[j].offset = source->offset;
            attributes[j].stride = stride;
            attributes[j].input_rate =
                vertex_rates[source->binding] == VK_VERTEX_INPUT_RATE_INSTANCE ?
                OPENAGC_PSBC_VERTEX_INPUT_RATE_INSTANCE :
                OPENAGC_PSBC_VERTEX_INPUT_RATE_VERTEX;
            attributes[j].divisor = vertex_divisors[source->binding];
        }
        const VkPs5PipelineLayout *layout = (const VkPs5PipelineLayout *)create->layout;
        const VkPs5RenderPass *render_pass = (const VkPs5RenderPass *)create->renderPass;
        if (create->subpass >= render_pass->subpass_count)
            return VK_ERROR_INITIALIZATION_FAILED;
        uint32_t color_attachment_count =
            render_pass->subpasses[create->subpass].color_attachment_count;
        AgcGfx1013ColorBlendState color_blend;
        bool dual_source_blend = false;
        if (!color_blend_state(blend, color_attachment_count, &color_blend,
                               &dual_source_blend))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        AgcGfx1013DepthStencilState depth_stencil = {0};
        VkBool32 has_depth_stencil = render_pass->subpasses[create->subpass].
            depth_stencil_attachment != VK_ATTACHMENT_UNUSED;
        if (has_depth_stencil) {
            const VkPipelineDepthStencilStateCreateInfo *depth =
                create->pDepthStencilState;
            if (!depth || depth->sType !=
                    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO)
                return VK_ERROR_INITIALIZATION_FAILED;
            if (depth->depthBoundsTestEnable ||
                depth->depthCompareOp > VK_COMPARE_OP_ALWAYS ||
                !stencil_face_state(&depth->front, &depth_stencil.front) ||
                !stencil_face_state(&depth->back, &depth_stencil.back))
                return VK_ERROR_FEATURE_NOT_PRESENT;
            depth_stencil.depth_test_enable = depth->depthTestEnable;
            depth_stencil.depth_write_enable =
                depth->depthTestEnable && depth->depthWriteEnable;
            depth_stencil.depth_compare_operation =
                (AgcGfx1013CompareOp)depth->depthCompareOp;
            depth_stencil.min_depth_bounds = depth->minDepthBounds;
            depth_stencil.max_depth_bounds = depth->maxDepthBounds;
            depth_stencil.stencil_test_enable = depth->stencilTestEnable;
            depth_stencil.back_face_enable = depth->stencilTestEnable;
            if (render_pass->subpasses[create->subpass].depth_stencil_layout ==
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL &&
                (depth->depthWriteEnable ||
                 (depth->stencilTestEnable &&
                  (depth->front.writeMask || depth->back.writeMask))))
                return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        OpenAgcPsbcPipelineContext context = {
            .vertex_attributes = attributes,
            .vertex_attribute_count = vertex_input->vertexAttributeDescriptionCount,
            .descriptor_bindings = layout->bindings,
            .descriptor_binding_count = layout->binding_count,
            .push_constant_size = layout->push_constant_size,
            .color_attachment_count =
                color_attachment_count,
            .dual_source_blend = dual_source_blend,
            .tessellation_control_points = tess_control ?
                create->pTessellationState->patchControlPoints : 3,
            .tessellation_patches = 8,
            .robust_buffer_access =
                vk_ps5_device_robust_buffer_access(device),
        };
        VkPs5Pipeline *pipeline = alloc_object(device, pAllocator, sizeof(*pipeline),
                                                _Alignof(VkPs5Pipeline));
        if (!pipeline) return VK_ERROR_OUT_OF_HOST_MEMORY;
        pipeline->vertex_binding_mask = vertex_binding_mask;
        pipeline->robust_buffer_access = context.robust_buffer_access;
        pipeline->vertex_attribute_mask = vertex_attribute_mask;
        for (uint32_t j = 0;
             j < vertex_input->vertexAttributeDescriptionCount; ++j) {
            const OpenAgcPsbcVertexAttribute *attribute = &attributes[j];
            pipeline->vertex_attribute_bindings[attribute->location] =
                attribute->binding;
            pipeline->vertex_attribute_offsets[attribute->location] =
                attribute->offset;
            pipeline->vertex_attribute_sizes[attribute->location] =
                psbc_vertex_format_size(attribute->format);
        }
        pipeline->color_blend = color_blend;
        pipeline->polygon_mode = translated_polygon_mode;
        pipeline->primitive_size = (AgcGfx1013PrimitiveSizeState){
            .point_size = 1.0f,
            .point_size_min = 1.0f,
            .point_size_max = 64.0f,
            .line_width = dynamic_line_width ? 1.0f : raster->lineWidth,
        };
        pipeline->line_width_dynamic = dynamic_line_width;
        pipeline->depth_bias = (AgcGfx1013DepthBiasState){
            .constant_factor = raster->depthBiasConstantFactor,
            .clamp = raster->depthBiasClamp,
            .slope_factor = raster->depthBiasSlopeFactor,
        };
        pipeline->depth_bias_enable = raster->depthBiasEnable;
        pipeline->depth_bias_dynamic = dynamic_depth_bias;
        pipeline->depth_clamp_enable = raster->depthClampEnable;
        pipeline->has_depth_stencil = has_depth_stencil;
        pipeline->depth_stencil = depth_stencil;
        memcpy(pipeline->vertex_strides, vertex_strides,
               sizeof(vertex_strides));
        VkResult result = VK_SUCCESS;
        uint32_t compiled = 0;
        if (tess_control) {
            context.enable_ngg = false;
            context.wave32 = true;
            result = compile_stage(tess_control, OPENAGC_PSBC_STAGE_TESS_CONTROL,
                                   vertex, OPENAGC_PSBC_STAGE_VERTEX,
                                   tess_evaluation,
                                   OPENAGC_PSBC_STAGE_TESS_EVALUATION,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_TESS_CONTROL;
                pipeline->stage_count = ++compiled;
            }
            if (result == VK_SUCCESS) {
                context.enable_ngg = true;
                context.wave32 = true;
                result = compile_stage(
                    geometry ? geometry : tess_evaluation,
                    geometry ? OPENAGC_PSBC_STAGE_GEOMETRY :
                               OPENAGC_PSBC_STAGE_TESS_EVALUATION,
                    geometry ? tess_evaluation : NULL,
                    OPENAGC_PSBC_STAGE_TESS_EVALUATION,
                    tess_control, OPENAGC_PSBC_STAGE_TESS_CONTROL,
                    &context, &pipeline->stages[compiled]);
                if (result == VK_SUCCESS) {
                    pipeline->stage_types[compiled] = geometry ?
                        OPENAGC_PSBC_STAGE_GEOMETRY :
                        OPENAGC_PSBC_STAGE_TESS_EVALUATION;
                    pipeline->stage_count = ++compiled;
                }
            }
        } else if (geometry) {
            context.enable_ngg = true;
            context.wave32 = true;
            result = compile_stage(geometry, OPENAGC_PSBC_STAGE_GEOMETRY,
                                   vertex, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_GEOMETRY;
                pipeline->stage_count = ++compiled;
            }
        } else {
            context.enable_ngg = true;
            context.wave32 = true;
            result = compile_stage(vertex, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_VERTEX;
                pipeline->stage_count = ++compiled;
            }
        }
        if (result == VK_SUCCESS) {
            context.enable_ngg = false;
            result = compile_stage(fragment, OPENAGC_PSBC_STAGE_FRAGMENT,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_FRAGMENT;
                pipeline->stage_count = ++compiled;
            }
        }
        if (result != VK_SUCCESS) {
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        if (tess_control)
            result = build_tessellation_layouts(pipeline);
        if (result != VK_SUCCESS) {
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        pipeline->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (agcGfx1013GetPrimitiveType(
                translated_topology, &pipeline->primitive_type) != AGC_OK) {
            free_pipeline(device, pAllocator, pipeline);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        pipeline->viewport.width = (uint32_t)viewport->width;
        pipeline->viewport.height = (uint32_t)viewport->height;
        pipeline->viewport.depth_clip_space =
            AGC_GFX1013_CLIP_SPACE_ZERO_TO_ONE;
        pipeline->scissor.left = (uint32_t)scissor->offset.x;
        pipeline->scissor.top = (uint32_t)scissor->offset.y;
        pipeline->scissor.right = pipeline->scissor.left + scissor->extent.width;
        pipeline->scissor.bottom = pipeline->scissor.top + scissor->extent.height;
        result = finalize_pipeline(device, pAllocator, pipeline);
        if (result == VK_SUCCESS && tess_control)
            result = vk_ps5_device_prepare_tessellation(device,
                &pipeline->tessellation,
                &pipeline->tess_ring_descriptor_address);
        if (result != VK_SUCCESS) {
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        pPipelines[i] = (VkPipeline)pipeline;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                  const VkAllocationCallbacks *pAllocator) {
    free_pipeline(device, pAllocator, (VkPs5Pipeline *)pipeline);
}

#define DEFINE_SIMPLE_CREATE(name, InfoType, HandleType) \
VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL \
name(VkDevice device, const InfoType *pCreateInfo, \
     const VkAllocationCallbacks *pAllocator, HandleType *pObject) { \
    if (!device || !pCreateInfo || !pObject) return VK_ERROR_INITIALIZATION_FAILED; \
    VkPs5Opaque *object = alloc_object(device, pAllocator, sizeof(*object), \
                                        _Alignof(VkPs5Opaque)); \
    if (!object) return VK_ERROR_OUT_OF_HOST_MEMORY; \
    *pObject = (HandleType)object; \
    return VK_SUCCESS; \
}

#define DEFINE_SIMPLE_DESTROY(name, HandleType) \
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL \
name(VkDevice device, HandleType object, const VkAllocationCallbacks *pAllocator) { \
    if (object) vk_ps5_device_free(device, pAllocator, (void *)object); \
}

static bool sampler_clamp(VkSamplerAddressMode mode, AgcClampMode *clamp)
{
    switch (mode) {
    case VK_SAMPLER_ADDRESS_MODE_REPEAT: *clamp = kAgcClampRepeat; return true;
    case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
        *clamp = kAgcClampMirror; return true;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
        *clamp = kAgcClampClamp; return true;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
        *clamp = kAgcClampBorder; return true;
    case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
        *clamp = kAgcClampMirrorOnce; return true;
    default: return false;
    }
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
    if (!device || !pCreateInfo || !pSampler ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pCreateInfo->pNext || pCreateInfo->flags ||
        pCreateInfo->unnormalizedCoordinates ||
        pCreateInfo->minLod < 0.0f || pCreateInfo->maxLod < pCreateInfo->minLod)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (pCreateInfo->anisotropyEnable &&
        (!(pCreateInfo->maxAnisotropy >= 1.0f) ||
         pCreateInfo->maxAnisotropy > 16.0f))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    AgcClampMode u, v, w;
    if (!sampler_clamp(pCreateInfo->addressModeU, &u) ||
        !sampler_clamp(pCreateInfo->addressModeV, &v) ||
        !sampler_clamp(pCreateInfo->addressModeW, &w))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    AgcFilterMode min_filter = pCreateInfo->minFilter == VK_FILTER_NEAREST ?
        (pCreateInfo->anisotropyEnable ? kAgcFilterAnisoPoint :
                                        kAgcFilterPoint) :
        pCreateInfo->minFilter == VK_FILTER_LINEAR ?
        (pCreateInfo->anisotropyEnable ? kAgcFilterAnisoLinear :
                                        kAgcFilterBilinear) :
        (AgcFilterMode)-1;
    AgcFilterMode mag_filter = pCreateInfo->magFilter == VK_FILTER_NEAREST ?
        (pCreateInfo->anisotropyEnable ? kAgcFilterAnisoPoint :
                                        kAgcFilterPoint) :
        pCreateInfo->magFilter == VK_FILTER_LINEAR ?
        (pCreateInfo->anisotropyEnable ? kAgcFilterAnisoLinear :
                                        kAgcFilterBilinear) :
        (AgcFilterMode)-1;
    if ((int)min_filter < 0 || (int)mag_filter < 0)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    AgcMipFilterMode mip_filter = pCreateInfo->maxLod == 0.0f ?
        kAgcMipFilterNone :
        pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST ?
        kAgcMipFilterPoint : pCreateInfo->mipmapMode ==
            VK_SAMPLER_MIPMAP_MODE_LINEAR ? kAgcMipFilterLinear :
            (AgcMipFilterMode)-1;
    if ((int)mip_filter < 0)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPs5Sampler *sampler = alloc_object(device, pAllocator, sizeof(*sampler),
                                         _Alignof(VkPs5Sampler));
    if (!sampler) return VK_ERROR_OUT_OF_HOST_MEMORY;
    agcSamplerDescriptorInit(&sampler->descriptor);
    agcSamplerDescriptorSetClampMode(&sampler->descriptor, u, v, w);
    agcSamplerDescriptorSetFilterMode(&sampler->descriptor, min_filter,
                                      mag_filter, mip_filter);
    if (pCreateInfo->anisotropyEnable)
        agcSamplerDescriptorSetMaxAnisotropy(
            &sampler->descriptor, (uint32_t)pCreateInfo->maxAnisotropy);
    agcSamplerDescriptorSetLod(&sampler->descriptor, pCreateInfo->minLod,
                               pCreateInfo->maxLod, pCreateInfo->mipLodBias);
    if (pCreateInfo->compareEnable)
        agcSamplerDescriptorSetCompareFunc(&sampler->descriptor,
                                            pCreateInfo->compareOp);
    AgcBorderColor border;
    switch (pCreateInfo->borderColor) {
    case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
    case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
        border = kAgcBorderTransparentBlack; break;
    case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
    case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
        border = kAgcBorderOpaqueBlack; break;
    case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
    case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
        border = kAgcBorderWhite; break;
    default:
        vk_ps5_device_free(device, pAllocator, sampler);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    agcSamplerDescriptorSetBorderColor(&sampler->descriptor, border);
    *pSampler = (VkSampler)sampler;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySampler(VkDevice device, VkSampler sampler,
                 const VkAllocationCallbacks *pAllocator)
{
    if (sampler) vk_ps5_device_free(device, pAllocator, (void *)sampler);
}
DEFINE_SIMPLE_CREATE(vkCreateDescriptorUpdateTemplate, VkDescriptorUpdateTemplateCreateInfo,
                     VkDescriptorUpdateTemplate)
DEFINE_SIMPLE_DESTROY(vkDestroyDescriptorUpdateTemplate, VkDescriptorUpdateTemplate)

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkFramebuffer *pFramebuffer) {
    if (!device || !pCreateInfo || !pFramebuffer ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO ||
        !pCreateInfo->renderPass || !pCreateInfo->width ||
        !pCreateInfo->height || pCreateInfo->layers != 1u ||
        pCreateInfo->attachmentCount > VK_PS5_MAX_RENDER_ATTACHMENTS ||
        (pCreateInfo->attachmentCount && !pCreateInfo->pAttachments))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5RenderPass *render_pass =
        (VkPs5RenderPass *)pCreateInfo->renderPass;
    if (pCreateInfo->attachmentCount != render_pass->attachment_count)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Framebuffer *framebuffer = alloc_object(
        device, pAllocator, sizeof(*framebuffer), _Alignof(VkPs5Framebuffer));
    if (!framebuffer) return VK_ERROR_OUT_OF_HOST_MEMORY;
    framebuffer->render_pass = render_pass;
    framebuffer->attachment_count = pCreateInfo->attachmentCount;
    framebuffer->width = pCreateInfo->width;
    framebuffer->height = pCreateInfo->height;
    framebuffer->layers = pCreateInfo->layers;
    for (uint32_t i = 0; i < framebuffer->attachment_count; ++i) {
        VkPs5ImageView *view = (VkPs5ImageView *)pCreateInfo->pAttachments[i];
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        if (!view || !image || view->format != render_pass->attachments[i].format ||
            image->extent.width < framebuffer->width ||
            image->extent.height < framebuffer->height) {
            vk_ps5_device_free(device, pAllocator, framebuffer);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        framebuffer->attachments[i] = view;
    }
    *pFramebuffer = (VkFramebuffer)framebuffer;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                     const VkAllocationCallbacks *pAllocator) {
    if (framebuffer)
        vk_ps5_device_free(device, pAllocator, (void *)framebuffer);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorSetLayout(VkDevice device,
                            const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkDescriptorSetLayout *pSetLayout) {
    if (!device || !pCreateInfo || !pSetLayout ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO ||
        (pCreateInfo->bindingCount && !pCreateInfo->pBindings))
        return VK_ERROR_INITIALIZATION_FAILED;
    size_t size = sizeof(VkPs5DescriptorSetLayout) +
        (size_t)pCreateInfo->bindingCount * sizeof(VkDescriptorSetLayoutBinding);
    VkPs5DescriptorSetLayout *layout = alloc_object(device, pAllocator, size,
                                                     _Alignof(VkPs5DescriptorSetLayout));
    if (!layout) return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->binding_count = pCreateInfo->bindingCount;
    if (layout->binding_count)
        memcpy(layout->bindings, pCreateInfo->pBindings,
               (size_t)layout->binding_count * sizeof(layout->bindings[0]));
    *pSetLayout = (VkDescriptorSetLayout)layout;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout setLayout,
                             const VkAllocationCallbacks *pAllocator) {
    if (setLayout) vk_ps5_device_free(device, pAllocator, (void *)setLayout);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkPipelineLayout *pPipelineLayout) {
    if (!device || !pCreateInfo || !pPipelineLayout ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO ||
        pCreateInfo->setLayoutCount > OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
        (pCreateInfo->setLayoutCount && !pCreateInfo->pSetLayouts) ||
        (pCreateInfo->pushConstantRangeCount && !pCreateInfo->pPushConstantRanges))
        return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t binding_count = 0;
    for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; ++set) {
        const VkPs5DescriptorSetLayout *layout =
            (const VkPs5DescriptorSetLayout *)pCreateInfo->pSetLayouts[set];
        if (!layout || layout->binding_count >
            OPENAGC_PSBC_MAX_DESCRIPTOR_BINDINGS - binding_count)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        binding_count += layout->binding_count;
    }
    size_t size = sizeof(VkPs5PipelineLayout) +
        (size_t)binding_count * sizeof(OpenAgcPsbcDescriptorBinding);
    VkPs5PipelineLayout *pipeline = alloc_object(device, pAllocator, size,
                                                  _Alignof(VkPs5PipelineLayout));
    if (!pipeline) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pipeline->binding_count = binding_count;
    uint32_t index = 0;
    for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; ++set) {
        const VkPs5DescriptorSetLayout *layout =
            (const VkPs5DescriptorSetLayout *)pCreateInfo->pSetLayouts[set];
        for (uint32_t j = 0; j < layout->binding_count; ++j, ++index) {
            const VkDescriptorSetLayoutBinding *source = &layout->bindings[j];
            OpenAgcPsbcDescriptorBinding *dest = &pipeline->bindings[index];
            if (!source->descriptorCount ||
                !psbc_descriptor_type(source->descriptorType, &dest->type)) {
                vk_ps5_device_free(device, pAllocator, pipeline);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            dest->set = set;
            dest->binding = source->binding;
            dest->array_size = source->descriptorCount;
        }
    }
    for (uint32_t i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
        const VkPushConstantRange *range = &pCreateInfo->pPushConstantRanges[i];
        if (range->offset > UINT32_MAX - range->size) {
            vk_ps5_device_free(device, pAllocator, pipeline);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        uint32_t end = range->offset + range->size;
        if (end > pipeline->push_constant_size) pipeline->push_constant_size = end;
    }
    if (pipeline->push_constant_size > 256u) {
        vk_ps5_device_free(device, pAllocator, pipeline);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    *pPipelineLayout = (VkPipelineLayout)pipeline;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks *pAllocator) {
    if (pipelineLayout) vk_ps5_device_free(device, pAllocator, (void *)pipelineLayout);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    if (!device || !pCreateInfo || !pRenderPass ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO) {
            const VkRenderPassMultiviewCreateInfo *multiview =
                (const VkRenderPassMultiviewCreateInfo *)next;
            for (uint32_t i = 0; i < multiview->subpassCount; ++i)
                if (multiview->pViewMasks[i] != 0) return VK_ERROR_FEATURE_NOT_PRESENT;
        }
    }
    if (!pCreateInfo->subpassCount || !pCreateInfo->pSubpasses ||
        pCreateInfo->subpassCount > VK_PS5_MAX_SUBPASSES ||
        pCreateInfo->attachmentCount > VK_PS5_MAX_RENDER_ATTACHMENTS ||
        (pCreateInfo->attachmentCount && !pCreateInfo->pAttachments))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5RenderPass *render_pass = alloc_object(device, pAllocator,
                                                 sizeof(*render_pass),
                                                 _Alignof(VkPs5RenderPass));
    if (!render_pass) return VK_ERROR_OUT_OF_HOST_MEMORY;
    render_pass->attachment_count = pCreateInfo->attachmentCount;
    render_pass->subpass_count = pCreateInfo->subpassCount;
    if (render_pass->attachment_count)
        memcpy(render_pass->attachments, pCreateInfo->pAttachments,
               (size_t)render_pass->attachment_count *
                   sizeof(render_pass->attachments[0]));
    for (uint32_t i = 0; i < render_pass->subpass_count; ++i) {
        const VkSubpassDescription *source = &pCreateInfo->pSubpasses[i];
        render_pass->subpasses[i].depth_stencil_attachment =
            VK_ATTACHMENT_UNUSED;
        if (source->pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS ||
            source->colorAttachmentCount > AGC_GFX1013_MAX_COLOR_TARGETS ||
            (source->colorAttachmentCount && !source->pColorAttachments)) {
            vk_ps5_device_free(device, pAllocator, render_pass);
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        render_pass->subpasses[i].color_attachment_count =
            source->colorAttachmentCount;
        for (uint32_t j = 0; j < source->colorAttachmentCount; ++j) {
            uint32_t attachment = source->pColorAttachments[j].attachment;
            if (attachment != VK_ATTACHMENT_UNUSED &&
                attachment >= render_pass->attachment_count) {
                vk_ps5_device_free(device, pAllocator, render_pass);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            if (attachment != VK_ATTACHMENT_UNUSED) {
                AgcGfx1013ColorTargetFormat target_format;
                if (source->pColorAttachments[j].layout !=
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
                    render_pass->attachments[attachment].samples !=
                        VK_SAMPLE_COUNT_1_BIT ||
                    !color_target_format(
                        render_pass->attachments[attachment].format,
                        &target_format)) {
                    vk_ps5_device_free(device, pAllocator, render_pass);
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
            }
            render_pass->subpasses[i].color_attachments[j] = attachment;
        }
        if (source->pDepthStencilAttachment &&
            source->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED) {
            uint32_t attachment =
                source->pDepthStencilAttachment->attachment;
            AgcGfx1013DepthSurfaceFormat depth_format;
            if (attachment >= render_pass->attachment_count) {
                vk_ps5_device_free(device, pAllocator, render_pass);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            VkImageLayout layout = source->pDepthStencilAttachment->layout;
            if ((layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
                 layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) ||
                render_pass->attachments[attachment].samples !=
                    VK_SAMPLE_COUNT_1_BIT ||
                !depth_surface_format(
                    render_pass->attachments[attachment].format,
                    &depth_format)) {
                vk_ps5_device_free(device, pAllocator, render_pass);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            render_pass->subpasses[i].depth_stencil_attachment = attachment;
            render_pass->subpasses[i].depth_stencil_layout = layout;
        }
    }
    *pRenderPass = (VkRenderPass)render_pass;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                    const VkAllocationCallbacks *pAllocator) {
    if (renderPass) vk_ps5_device_free(device, pAllocator, (void *)renderPass);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkDescriptorPool *pDescriptorPool) {
    if (!device || !pCreateInfo || !pDescriptorPool || !pCreateInfo->maxSets)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5DescriptorPool *pool = alloc_object(device, pAllocator, sizeof(*pool),
                                              _Alignof(VkPs5DescriptorPool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->device = device;
    pool->max_sets = pCreateInfo->maxSets;
    *pDescriptorPool = (VkDescriptorPool)pool;
    return VK_SUCCESS;
}

static void clear_descriptor_pool(VkPs5DescriptorPool *pool) {
    VkPs5DescriptorSet *set = pool->sets;
    while (set) {
        VkPs5DescriptorSet *next = set->next;
        agcGpuMemoryFreeFlexible(&set->table_memory);
        vk_ps5_device_free(pool->device, NULL, set);
        set = next;
    }
    pool->sets = NULL;
    pool->allocated_sets = 0;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                        const VkAllocationCallbacks *pAllocator) {
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)descriptorPool;
    if (!pool) return;
    clear_descriptor_pool(pool);
    vk_ps5_device_free(device, pAllocator, pool);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                      VkDescriptorPoolResetFlags flags) {
    (void)device; (void)flags;
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)descriptorPool;
    if (!pool) return VK_ERROR_INITIALIZATION_FAILED;
    clear_descriptor_pool(pool);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateDescriptorSets(VkDevice device,
                         const VkDescriptorSetAllocateInfo *pAllocateInfo,
                         VkDescriptorSet *pDescriptorSets) {
    if (!device || !pAllocateInfo || !pDescriptorSets ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO ||
        (pAllocateInfo->descriptorSetCount && !pAllocateInfo->pSetLayouts))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)pAllocateInfo->descriptorPool;
    if (!pool || pAllocateInfo->descriptorSetCount >
        pool->max_sets - pool->allocated_sets) return VK_ERROR_OUT_OF_POOL_MEMORY;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i)
        pDescriptorSets[i] = VK_NULL_HANDLE;
    VkPs5DescriptorSet *old_head = pool->sets;
    uint32_t old_count = pool->allocated_sets;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        VkPs5DescriptorSetLayout *layout = (VkPs5DescriptorSetLayout *)
            pAllocateInfo->pSetLayouts[i];
        if (!layout) goto invalid_layout;
        uint32_t descriptor_count = 0;
        for (uint32_t binding = 0; binding < layout->binding_count; ++binding) {
            if (layout->bindings[binding].descriptorCount >
                UINT32_MAX - descriptor_count)
                goto invalid_layout;
            descriptor_count += layout->bindings[binding].descriptorCount;
        }
        size_t size = sizeof(VkPs5DescriptorSet) +
            (size_t)descriptor_count * sizeof(VkPs5DescriptorValue);
        VkPs5DescriptorSet *set = alloc_object(device, NULL, size,
                                               _Alignof(VkPs5DescriptorSet));
        if (!set) {
            while (pool->sets != old_head) {
                VkPs5DescriptorSet *rollback = pool->sets;
                pool->sets = rollback->next;
                agcGpuMemoryFreeFlexible(&rollback->table_memory);
                vk_ps5_device_free(device, NULL, rollback);
            }
            pool->allocated_sets = old_count;
            for (uint32_t j = 0; j < pAllocateInfo->descriptorSetCount; ++j)
                pDescriptorSets[j] = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        set->layout = layout;
        set->descriptor_count = descriptor_count;
        if (agcGpuMemoryAllocateFlexible(&set->table_memory,
                VK_PS5_DESCRIPTOR_TABLE_SIZE, 16u,
                "vulkan_ps5_descriptor_table") != AGC_OK) {
            vk_ps5_device_free(device, NULL, set);
            while (pool->sets != old_head) {
                VkPs5DescriptorSet *rollback = pool->sets;
                pool->sets = rollback->next;
                agcGpuMemoryFreeFlexible(&rollback->table_memory);
                vk_ps5_device_free(device, NULL, rollback);
            }
            pool->allocated_sets = old_count;
            for (uint32_t j = 0; j < pAllocateInfo->descriptorSetCount; ++j)
                pDescriptorSets[j] = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        set->pool = pool;
        set->next = pool->sets;
        pool->sets = set;
        pool->allocated_sets++;
        pDescriptorSets[i] = (VkDescriptorSet)set;
    }
    return VK_SUCCESS;

invalid_layout:
    while (pool->sets != old_head) {
        VkPs5DescriptorSet *rollback = pool->sets;
        pool->sets = rollback->next;
        agcGpuMemoryFreeFlexible(&rollback->table_memory);
        vk_ps5_device_free(device, NULL, rollback);
    }
    pool->allocated_sets = old_count;
    for (uint32_t j = 0; j < pAllocateInfo->descriptorSetCount; ++j)
        pDescriptorSets[j] = VK_NULL_HANDLE;
    return VK_ERROR_INITIALIZATION_FAILED;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                     uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)descriptorPool;
    if (!pool || (descriptorSetCount && !pDescriptorSets))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        VkPs5DescriptorSet **link = &pool->sets;
        while (*link && *link != (VkPs5DescriptorSet *)pDescriptorSets[i])
            link = &(*link)->next;
        if (!*link) return VK_ERROR_INITIALIZATION_FAILED;
        VkPs5DescriptorSet *set = *link;
        *link = set->next;
        pool->allocated_sets--;
        agcGpuMemoryFreeFlexible(&set->table_memory);
        vk_ps5_device_free(device, NULL, set);
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                       const VkWriteDescriptorSet *pDescriptorWrites,
                       uint32_t descriptorCopyCount,
                       const VkCopyDescriptorSet *pDescriptorCopies) {
    (void)device;
    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet *write = &pDescriptorWrites[i];
        VkPs5DescriptorSet *set = (VkPs5DescriptorSet *)write->dstSet;
        if (!set) continue;
        uint32_t first = 0;
        const VkDescriptorSetLayoutBinding *layout_binding = NULL;
        for (uint32_t j = 0; j < set->layout->binding_count; ++j) {
            const VkDescriptorSetLayoutBinding *candidate =
                &set->layout->bindings[j];
            if (candidate->binding == write->dstBinding) {
                layout_binding = candidate;
                break;
            }
            first += candidate->descriptorCount;
        }
        if (!layout_binding || layout_binding->descriptorType !=
                write->descriptorType ||
            write->dstArrayElement > layout_binding->descriptorCount ||
            write->descriptorCount > layout_binding->descriptorCount -
                write->dstArrayElement)
            continue;
        switch (write->descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (!write->pBufferInfo) continue;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            if (!write->pImageInfo) continue;
            break;
        default:
            continue;
        }
        first += write->dstArrayElement;
        for (uint32_t j = 0; j < write->descriptorCount; ++j) {
            set->values[first + j].type = write->descriptorType;
            if (write->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                write->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                write->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                write->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                set->values[first + j].buffer = write->pBufferInfo[j];
            else
                set->values[first + j].image = write->pImageInfo[j];
            set->values[first + j].valid = VK_TRUE;
        }
    }
    for (uint32_t i = 0; i < descriptorCopyCount; ++i) {
        const VkCopyDescriptorSet *copy = &pDescriptorCopies[i];
        VkPs5DescriptorSet *source = (VkPs5DescriptorSet *)copy->srcSet;
        VkPs5DescriptorSet *dest = (VkPs5DescriptorSet *)copy->dstSet;
        uint32_t source_first = 0, dest_first = 0;
        const VkDescriptorSetLayoutBinding *source_binding = NULL;
        const VkDescriptorSetLayoutBinding *dest_binding = NULL;
        if (!source || !dest) continue;
        for (uint32_t j = 0; j < source->layout->binding_count; ++j) {
            if (source->layout->bindings[j].binding == copy->srcBinding) {
                source_binding = &source->layout->bindings[j];
                break;
            }
            source_first += source->layout->bindings[j].descriptorCount;
        }
        for (uint32_t j = 0; j < dest->layout->binding_count; ++j) {
            if (dest->layout->bindings[j].binding == copy->dstBinding) {
                dest_binding = &dest->layout->bindings[j];
                break;
            }
            dest_first += dest->layout->bindings[j].descriptorCount;
        }
        if (!source_binding || !dest_binding ||
            source_binding->descriptorType != dest_binding->descriptorType ||
            copy->srcArrayElement > source_binding->descriptorCount ||
            copy->descriptorCount > source_binding->descriptorCount -
                copy->srcArrayElement ||
            copy->dstArrayElement > dest_binding->descriptorCount ||
            copy->descriptorCount > dest_binding->descriptorCount -
                copy->dstArrayElement)
            continue;
        source_first += copy->srcArrayElement;
        dest_first += copy->dstArrayElement;
        memmove(&dest->values[dest_first], &source->values[source_first],
            (size_t)copy->descriptorCount * sizeof(VkPs5DescriptorValue));
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet,
                                  VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                  const void *pData) {
    (void)device; (void)descriptorSet; (void)descriptorUpdateTemplate; (void)pData;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDescriptorSetLayoutSupport(VkDevice device,
                                const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                VkDescriptorSetLayoutSupport *pSupport) {
    (void)device;
    if (pSupport) pSupport->supported = pCreateInfo && pCreateInfo->bindingCount <= 1024;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSamplerYcbcrConversion(VkDevice device,
                               const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSamplerYcbcrConversion *pYcbcrConversion) {
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (pYcbcrConversion) *pYcbcrConversion = VK_NULL_HANDLE;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySamplerYcbcrConversion(VkDevice device,
                                VkSamplerYcbcrConversion ycbcrConversion,
                                const VkAllocationCallbacks *pAllocator) {
    (void)device; (void)ycbcrConversion; (void)pAllocator;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass,
                           VkExtent2D *pGranularity) {
    (void)device; (void)renderPass;
    if (pGranularity) *pGranularity = (VkExtent2D){1, 1};
}

#define IGNORE(x) (void)(x)

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyBuffer(VkCommandBuffer c, VkBuffer s, VkBuffer d, uint32_t n,
                const VkBufferCopy *r) {
    const uint64_t maximum_packet_bytes = UINT64_C(0xfffffffc);
    const uint64_t address_limit = UINT64_C(1) << 48u;
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *source = (VkPs5Buffer *)s;
    VkPs5Buffer *destination = (VkPs5Buffer *)d;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (command->active_render_pass || !source || !destination || !n || !r ||
        !source->memory || !destination->memory ||
        !(source->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !(destination->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    uint64_t source_base = vk_ps5_memory_gpu_address(
        source->memory, source->memory_offset);
    uint64_t destination_base = vk_ps5_memory_gpu_address(
        destination->memory, destination->memory_offset);
    uint64_t packet_count = 0u;
    for (uint32_t region = 0u; region < n; ++region) {
        const VkBufferCopy *copy = &r[region];
        if (!copy->size || ((copy->srcOffset | copy->dstOffset | copy->size) &
                3u) != 0u || copy->srcOffset > source->size ||
            copy->size > source->size - copy->srcOffset ||
            copy->dstOffset > destination->size ||
            copy->size > destination->size - copy->dstOffset ||
            copy->srcOffset > UINT64_MAX - source_base ||
            copy->dstOffset > UINT64_MAX - destination_base ||
            copy->size > UINT64_MAX - (source_base + copy->srcOffset) ||
            copy->size > UINT64_MAX -
                (destination_base + copy->dstOffset)) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        uint64_t source_start = source_base + copy->srcOffset;
        uint64_t destination_start = destination_base + copy->dstOffset;
        if (!source_start || !destination_start ||
            source_start >= address_limit || destination_start >= address_limit ||
            copy->size > address_limit - source_start ||
            copy->size > address_limit - destination_start) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        uint64_t region_packets = copy->size / maximum_packet_bytes +
            (copy->size % maximum_packet_bytes != 0u);
        if (region_packets > UINT64_MAX - packet_count) {
            command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
        packet_count += region_packets;
    }

    for (uint32_t source_region = 0u; source_region < n; ++source_region) {
        uint64_t source_start = source_base + r[source_region].srcOffset;
        uint64_t source_end = source_start + r[source_region].size;
        for (uint32_t destination_region = 0u;
             destination_region < n; ++destination_region) {
            uint64_t destination_start =
                destination_base + r[destination_region].dstOffset;
            uint64_t destination_end =
                destination_start + r[destination_region].size;
            if (source_start < destination_end &&
                destination_start < source_end) {
                command->record_error = VK_ERROR_INITIALIZATION_FAILED;
                return;
            }
        }
    }
    if (packet_count > UINT32_MAX / 8u ||
        agcCbRemainingDwords(&command->dcb) < (uint32_t)packet_count * 8u) {
        command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }

    for (uint32_t region = 0u; region < n; ++region) {
        int32_t result = agcGfx1013CopyBuffer(&command->dcb,
            source_base + r[region].srcOffset,
            destination_base + r[region].dstOffset, r[region].size);
        if (result != AGC_OK) {
            command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
                VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
               VkImageLayout dl, uint32_t n, const VkImageCopy *r) {
    IGNORE(c); IGNORE(s); IGNORE(sl); IGNORE(d); IGNORE(dl); IGNORE(n); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyBufferToImage(VkCommandBuffer c, VkBuffer s, VkImage d, VkImageLayout dl,
                       uint32_t n, const VkBufferImageCopy *r) {
    IGNORE(c); IGNORE(s); IGNORE(d); IGNORE(dl); IGNORE(n); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImageToBuffer(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkBuffer d,
                       uint32_t n, const VkBufferImageCopy *r) {
    IGNORE(c); IGNORE(s); IGNORE(sl); IGNORE(d); IGNORE(n); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdUpdateBuffer(VkCommandBuffer c, VkBuffer d, VkDeviceSize o, VkDeviceSize n,
                  const void *p) { IGNORE(c); IGNORE(d); IGNORE(o); IGNORE(n); IGNORE(p); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdFillBuffer(VkCommandBuffer c, VkBuffer d, VkDeviceSize o, VkDeviceSize n,
                uint32_t v) { IGNORE(c); IGNORE(d); IGNORE(o); IGNORE(n); IGNORE(v); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d,
                     VkDependencyFlags f, uint32_t mn, const VkMemoryBarrier *m,
                     uint32_t bn, const VkBufferMemoryBarrier *b, uint32_t in,
                     const VkImageMemoryBarrier *i) {
    IGNORE(c); IGNORE(s); IGNORE(d); IGNORE(f); IGNORE(mn); IGNORE(m);
    IGNORE(bn); IGNORE(b); IGNORE(in); IGNORE(i);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginQuery(VkCommandBuffer c, VkQueryPool p, uint32_t q, VkQueryControlFlags f) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !pool ||
        pool->type != VK_QUERY_TYPE_OCCLUSION || q >= pool->count ||
        !command->active_render_pass || command->active_query_pool) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    IGNORE(f);
    uint64_t address = pool->memory.gpu_address +
        (uint64_t)q * VK_PS5_QUERY_SLOT_SIZE;
    int32_t result = agcGfx1013BeginOcclusionQuery(&command->dcb, address, 0u);
    if (result != AGC_OK) {
        command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    command->active_query_pool = pool;
    command->active_query = q;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndQuery(VkCommandBuffer c, VkQueryPool p, uint32_t q) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !pool ||
        command->active_query_pool != pool || command->active_query != q) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    uint64_t address = pool->memory.gpu_address +
        (uint64_t)q * VK_PS5_QUERY_SLOT_SIZE;
    int32_t result = agcGfx1013EndOcclusionQuery(
        &command->dcb, address + sizeof(uint64_t));
    const AgcGfx1013EopFenceState availability = {
        .address = address + VK_PS5_QUERY_AVAILABILITY_OFFSET,
        .value = 1u,
    };
    if (result == AGC_OK)
        result = agcGfx1013SignalEopFence(&command->dcb, &availability);
    if (result != AGC_OK)
        command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    command->active_query_pool = NULL;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResetQueryPool(VkCommandBuffer c, VkQueryPool p, uint32_t f, uint32_t n) {
    static const uint32_t zeros[VK_PS5_QUERY_SLOT_SIZE / sizeof(uint32_t)] = {0};
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !pool ||
        command->active_render_pass || f > pool->count || n > pool->count - f) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t address = pool->memory.gpu_address +
            (uint64_t)(f + i) * VK_PS5_QUERY_SLOT_SIZE;
        if (!sceAgcDcbWriteData(&command->dcb, 2u, 0u, address, zeros,
                VK_PS5_QUERY_SLOT_SIZE / sizeof(uint32_t), 0u, 1u)) {
            command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdWriteTimestamp(VkCommandBuffer c, VkPipelineStageFlagBits s, VkQueryPool p,
                    uint32_t q) {
    IGNORE(s); IGNORE(p); IGNORE(q);
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (command && command->state == VK_PS5_COMMAND_RECORDING)
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyQueryPoolResults(VkCommandBuffer c, VkQueryPool p, uint32_t f, uint32_t n,
                          VkBuffer d, VkDeviceSize o, VkDeviceSize s,
                          VkQueryResultFlags flags) {
    IGNORE(p); IGNORE(f); IGNORE(n); IGNORE(d); IGNORE(o); IGNORE(s); IGNORE(flags);
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (command && command->state == VK_PS5_COMMAND_RECORDING)
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdExecuteCommands(VkCommandBuffer c, uint32_t n, const VkCommandBuffer *p) {
    IGNORE(c); IGNORE(n); IGNORE(p);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Pipeline *pipeline = (VkPs5Pipeline *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        !pipeline || pipeline->bind_point != b) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (b == VK_PIPELINE_BIND_POINT_COMPUTE)
        command->bound_compute = pipeline;
    else if (b == VK_PIPELINE_BIND_POINT_GRAPHICS)
        command->bound_graphics = pipeline;
    else
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint b, VkPipelineLayout l,
                        uint32_t f, uint32_t n, const VkDescriptorSet *s,
                        uint32_t dn, const uint32_t *d) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    IGNORE(l); IGNORE(d);
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        (n && !s) || f > OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
        n > OPENAGC_PSBC_MAX_DESCRIPTOR_SETS - f || dn != 0u) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = dn ? VK_ERROR_FEATURE_NOT_PRESENT :
                VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    VkPs5DescriptorSet **sets = b == VK_PIPELINE_BIND_POINT_COMPUTE ?
        command->compute_sets : b == VK_PIPELINE_BIND_POINT_GRAPHICS ?
        command->graphics_sets : NULL;
    if (!sets) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (!s[i]) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        sets[f + i] = (VkPs5DescriptorSet *)s[i];
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(VkCommandBuffer c, VkImage i, VkImageLayout l,
                     const VkClearColorValue *v, uint32_t n,
                     const VkImageSubresourceRange *r) {
    IGNORE(c); IGNORE(i); IGNORE(l); IGNORE(v); IGNORE(n); IGNORE(r);
}

static VkPs5DescriptorValue *descriptor_value(
    VkPs5DescriptorSet *set, uint32_t binding, uint32_t array_element,
    VkDescriptorType *type)
{
    uint32_t first = 0u;
    if (!set) return NULL;
    for (uint32_t i = 0; i < set->layout->binding_count; ++i) {
        const VkDescriptorSetLayoutBinding *candidate = &set->layout->bindings[i];
        if (candidate->binding == binding) {
            if (array_element >= candidate->descriptorCount) return NULL;
            if (type) *type = candidate->descriptorType;
            return &set->values[first + array_element];
        }
        first += candidate->descriptorCount;
    }
    return NULL;
}

static uint64_t descriptor_table_gpu_address(const VkPs5DescriptorSet *set)
{
#if defined(OPENAGC_GENERIC)
    return ((uint64_t)AGC_GFX1013_ADDRESS32_HIGH << 32u) |
        (uint32_t)set->table_memory.gpu_address;
#else
    return set->table_memory.gpu_address;
#endif
}

static VkResult encode_buffer_descriptor(
    const VkPs5DescriptorValue *value, void *destination)
{
    if (!value || !value->buffer.buffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Buffer *buffer = (VkPs5Buffer *)value->buffer.buffer;
    if (!buffer->memory || value->buffer.offset >= buffer->size)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceSize available = buffer->size - value->buffer.offset;
    VkDeviceSize range = value->buffer.range == VK_WHOLE_SIZE ?
        available : value->buffer.range;
    if (!range || range > available || range > UINT32_MAX)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (buffer->memory_offset > UINT64_MAX - value->buffer.offset)
        return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t address = vk_ps5_memory_gpu_address(buffer->memory,
        buffer->memory_offset + value->buffer.offset);
    AgcGfx1013BufferDescriptor descriptor;
    if (agcGfx1013RawBufferDescriptorEncode(&descriptor, address,
            (uint32_t)range) != AGC_OK)
        return VK_ERROR_INITIALIZATION_FAILED;
    memcpy(destination, &descriptor, sizeof(descriptor));
    return VK_SUCCESS;
}

static VkResult encode_image_descriptor(
    const VkPs5DescriptorValue *value, bool storage, void *destination);

static VkResult prepare_compute_resource_tables(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline,
    AgcGfx1013ResourceTableBinding *tables, uint32_t *table_count)
{
    const OpenAgcPsbcMetadata *metadata = &pipeline->stages[0].metadata;
    bool used_sets[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS] = {false};
    *table_count = 0u;
    for (uint32_t i = 0; i < metadata->user_sgpr_count; ++i) {
        if (metadata->user_sgprs[i].kind !=
                OPENAGC_PSBC_USER_SGPR_DESCRIPTOR_SET)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (metadata->user_sgprs[i].index >=
                OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
            !command->compute_sets[metadata->user_sgprs[i].index])
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0; i < metadata->descriptor_mapping_count; ++i) {
        const OpenAgcPsbcDescriptorMapping *mapping =
            &metadata->descriptor_mappings[i];
        if (mapping->set >= OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
            mapping->byte_offset > VK_PS5_DESCRIPTOR_TABLE_SIZE ||
            mapping->byte_stride < sizeof(AgcGfx1013BufferDescriptor) ||
            mapping->array_size >
                (VK_PS5_DESCRIPTOR_TABLE_SIZE - mapping->byte_offset) /
                    mapping->byte_stride)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (mapping->type != OPENAGC_PSBC_DESCRIPTOR_UNIFORM_BUFFER &&
            mapping->type != OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER &&
            mapping->type != OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        VkPs5DescriptorSet *set = command->compute_sets[mapping->set];
        if (!set) return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t array = 0; array < mapping->array_size; ++array) {
            VkDescriptorType layout_type;
            VkPs5DescriptorValue *value = descriptor_value(
                set, mapping->binding, array, &layout_type);
            OpenAgcPsbcDescriptorType psbc_type;
            if (!value || !value->valid ||
                !psbc_descriptor_type(layout_type, &psbc_type) ||
                psbc_type != mapping->type ||
                (mapping->type == OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE ?
                    !value->image.imageView : !value->buffer.buffer))
                return VK_ERROR_INITIALIZATION_FAILED;
            size_t offset = mapping->byte_offset +
                (size_t)array * mapping->byte_stride;
            void *destination =
                (uint8_t *)set->table_memory.cpu_address + offset;
            VkResult encode_result =
                mapping->type == OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE ?
                encode_image_descriptor(value, true, destination) :
                encode_buffer_descriptor(value, destination);
            if (encode_result != VK_SUCCESS) return encode_result;
        }
        used_sets[mapping->set] = true;
    }
    for (uint32_t set_index = 0;
         set_index < OPENAGC_PSBC_MAX_DESCRIPTOR_SETS; ++set_index) {
        if (!used_sets[set_index]) continue;
        VkPs5DescriptorSet *set = command->compute_sets[set_index];
        if (agcGpuMemoryFlush(&set->table_memory, 0,
                VK_PS5_DESCRIPTOR_TABLE_SIZE) != AGC_OK)
            return VK_ERROR_DEVICE_LOST;
        tables[*table_count].placeholder =
            OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(set_index);
        tables[*table_count].address = descriptor_table_gpu_address(set);
        (*table_count)++;
    }
    return VK_SUCCESS;
}

static VkResult image_descriptor_state(
    const VkDescriptorImageInfo *info, bool storage,
    AgcGfx1013Image2DState *state)
{
    VkPs5ImageView *view = info ? (VkPs5ImageView *)info->imageView : NULL;
    VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
    if (!view || !image || !image->memory ||
        !(image->usage & (storage ? VK_IMAGE_USAGE_STORAGE_BIT :
            VK_IMAGE_USAGE_SAMPLED_BIT)) ||
        image->type != VK_IMAGE_TYPE_2D || image->tiling != VK_IMAGE_TILING_LINEAR ||
        image->samples != VK_SAMPLE_COUNT_1_BIT || image->mip_levels != 1u ||
        image->array_layers != 1u ||
        (storage ? info->imageLayout != VK_IMAGE_LAYOUT_GENERAL :
            (info->imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             info->imageLayout != VK_IMAGE_LAYOUT_GENERAL)) ||
        view->components.r != VK_COMPONENT_SWIZZLE_IDENTITY ||
        view->components.g != VK_COMPONENT_SWIZZLE_IDENTITY ||
        view->components.b != VK_COMPONENT_SWIZZLE_IDENTITY ||
        view->components.a != VK_COMPONENT_SWIZZLE_IDENTITY)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint32_t dst_x = 4u, dst_y = 5u, dst_z = 6u, dst_w = 7u;
    if (!storage && view->format == VK_FORMAT_B8G8R8A8_UNORM) {
        dst_x = 6u;
        dst_z = 4u;
    } else if (view->format != VK_FORMAT_R8G8B8A8_UNORM) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    *state = (AgcGfx1013Image2DState){
        .address = vk_ps5_memory_gpu_address(image->memory,
            image->memory_offset),
        .width = image->extent.width,
        .height = image->extent.height,
        .format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM,
        .image_type = AGC_GFX1013_IMAGE_TYPE_2D,
        .dst_sel_x = dst_x,
        .dst_sel_y = dst_y,
        .dst_sel_z = dst_z,
        .dst_sel_w = dst_w,
        .sample_count = 1u,
    };
    return VK_SUCCESS;
}

static VkResult encode_image_descriptor(
    const VkPs5DescriptorValue *value, bool storage, void *destination)
{
    AgcGfx1013Image2DState image_state;
    VkResult result = image_descriptor_state(
        &value->image, storage, &image_state);
    if (result != VK_SUCCESS) return result;
    if (agcGfx1013Image2DDescriptorEncode(destination, &image_state) != AGC_OK)
        return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

static VkResult prepare_graphics_descriptor_tables(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline,
    uint32_t stage_index, AgcGfx1013ResourceTableBinding *tables,
    uint32_t *table_count)
{
    const OpenAgcPsbcMetadata *metadata = &pipeline->stages[stage_index].metadata;
    bool used_sets[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS] = {false};
    *table_count = 0u;
    for (uint32_t i = 0; i < metadata->user_sgpr_count; ++i) {
        const OpenAgcPsbcUserSgpr *sgpr = &metadata->user_sgprs[i];
        if (sgpr->kind != OPENAGC_PSBC_USER_SGPR_DESCRIPTOR_SET) continue;
        if (sgpr->index >= OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
            !command->graphics_sets[sgpr->index])
            return VK_ERROR_INITIALIZATION_FAILED;
        used_sets[sgpr->index] = true;
    }
    for (uint32_t i = 0; i < metadata->descriptor_mapping_count; ++i) {
        const OpenAgcPsbcDescriptorMapping *mapping =
            &metadata->descriptor_mappings[i];
        size_t descriptor_size;
        if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_UNIFORM_BUFFER ||
            mapping->type == OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER)
            descriptor_size = sizeof(AgcGfx1013BufferDescriptor);
        else if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER)
            descriptor_size = sizeof(AgcGfx1013CombinedImageSamplerDescriptor);
        else if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_SAMPLED_IMAGE)
            descriptor_size = sizeof(AgcGfx1013ImageDescriptor);
        else if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE)
            descriptor_size = sizeof(AgcGfx1013ImageDescriptor);
        else if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_SAMPLER)
            descriptor_size = sizeof(AgcSamplerDescriptor);
        else
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (mapping->set >= OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
            mapping->byte_offset > VK_PS5_DESCRIPTOR_TABLE_SIZE ||
            mapping->byte_stride < descriptor_size ||
            mapping->array_size >
                (VK_PS5_DESCRIPTOR_TABLE_SIZE - mapping->byte_offset) /
                    mapping->byte_stride)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        VkPs5DescriptorSet *set = command->graphics_sets[mapping->set];
        if (!set) return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t array = 0; array < mapping->array_size; ++array) {
            VkDescriptorType layout_type;
            VkPs5DescriptorValue *value = descriptor_value(
                set, mapping->binding, array, &layout_type);
            OpenAgcPsbcDescriptorType psbc_type;
            if (!value || !value->valid ||
                !psbc_descriptor_type(layout_type, &psbc_type) ||
                psbc_type != mapping->type)
                return VK_ERROR_INITIALIZATION_FAILED;
            size_t offset = mapping->byte_offset +
                (size_t)array * mapping->byte_stride;
            void *destination =
                (uint8_t *)set->table_memory.cpu_address + offset;
            if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_UNIFORM_BUFFER ||
                mapping->type == OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER) {
                VkResult encode_result = encode_buffer_descriptor(
                    value, destination);
                if (encode_result != VK_SUCCESS) return encode_result;
            } else if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_SAMPLER) {
                VkPs5Sampler *sampler =
                    (VkPs5Sampler *)value->image.sampler;
                if (!sampler) return VK_ERROR_INITIALIZATION_FAILED;
                memcpy(destination, &sampler->descriptor,
                       sizeof(sampler->descriptor));
            } else {
                if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE) {
                    VkResult image_result = encode_image_descriptor(
                        value, true, destination);
                    if (image_result != VK_SUCCESS) return image_result;
                    continue;
                }
                AgcGfx1013Image2DState image_state;
                VkResult image_result = image_descriptor_state(
                    &value->image, false, &image_state);
                if (image_result != VK_SUCCESS) return image_result;
                if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_SAMPLED_IMAGE) {
                    if (agcGfx1013Image2DDescriptorEncode(
                            destination, &image_state) != AGC_OK)
                        return VK_ERROR_INITIALIZATION_FAILED;
                } else {
                    VkPs5Sampler *sampler =
                        (VkPs5Sampler *)value->image.sampler;
                    if (!sampler ||
                        agcGfx1013CombinedImageSamplerDescriptorEncode(
                            destination, &image_state,
                            &sampler->descriptor) != AGC_OK)
                        return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
        }
        used_sets[mapping->set] = true;
    }
    for (uint32_t set_index = 0;
         set_index < OPENAGC_PSBC_MAX_DESCRIPTOR_SETS; ++set_index) {
        if (!used_sets[set_index]) continue;
        VkPs5DescriptorSet *set = command->graphics_sets[set_index];
        if (agcGpuMemoryFlush(&set->table_memory, 0,
                VK_PS5_DESCRIPTOR_TABLE_SIZE) != AGC_OK)
            return VK_ERROR_DEVICE_LOST;
        tables[*table_count].placeholder =
            OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(set_index);
        tables[*table_count].address = descriptor_table_gpu_address(set);
        (*table_count)++;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    VkPs5Pipeline *pipeline = command->bound_compute;
    if (!pipeline || !x || !y || !z ||
        pipeline->stage_types[0] != OPENAGC_PSBC_STAGE_COMPUTE) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    const OpenAgcPsbcMetadata *metadata = &pipeline->stages[0].metadata;
    if (!metadata->local_size_x || !metadata->local_size_y ||
        !metadata->local_size_z) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    AgcGfx1013ResourceTableBinding
        tables[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    uint32_t table_count;
    VkResult prepare_result = prepare_compute_resource_tables(
        command, pipeline, tables, &table_count);
    if (prepare_result != VK_SUCCESS) {
        command->record_error = prepare_result;
        return;
    }
    const VkPs5RuntimeShader *shader = &pipeline->runtime[0];
    if (!command->compute_defaults_emitted) {
        int32_t defaults_result = agcGfx1013ApplyComputeDefaultsV8(
            &command->dcb, NULL);
        if (defaults_result != AGC_OK) {
            command->record_error =
                defaults_result == AGC_ERROR_BUFFER_TOO_SMALL ?
                VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        command->compute_defaults_emitted = VK_TRUE;
    }
    const AgcGfx1013ComputeState state = {
        .record = &shader->record,
        .sh_registers = shader->binding.sh_registers,
        .num_sh_registers = shader->binding.num_sh_registers,
        .code_address = shader->binding.code_address,
        .local_size_x = metadata->local_size_x,
        .local_size_y = metadata->local_size_y,
        .local_size_z = metadata->local_size_z,
        .group_count_x = x,
        .group_count_y = y,
        .group_count_z = z,
        .modifier = AGC_GFX1013_COMPUTE_DISPATCH_WAVE32,
        .resource_tables = tables,
        .num_resource_tables = table_count,
    };
    int32_t result = agcGfx1013DispatchCompute(&command->dcb, &state);
    if (result != AGC_OK)
        command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatchIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o) {
    IGNORE(c); IGNORE(b); IGNORE(o);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetEvent(VkCommandBuffer c, VkEvent e, VkPipelineStageFlags s) {
    IGNORE(c); IGNORE(e); IGNORE(s);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResetEvent(VkCommandBuffer c, VkEvent e, VkPipelineStageFlags s) {
    IGNORE(c); IGNORE(e); IGNORE(s);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdWaitEvents(VkCommandBuffer c, uint32_t n, const VkEvent *e,
                VkPipelineStageFlags s, VkPipelineStageFlags d, uint32_t mn,
                const VkMemoryBarrier *m, uint32_t bn,
                const VkBufferMemoryBarrier *b, uint32_t in,
                const VkImageMemoryBarrier *i) {
    IGNORE(c); IGNORE(n); IGNORE(e); IGNORE(s); IGNORE(d); IGNORE(mn); IGNORE(m);
    IGNORE(bn); IGNORE(b); IGNORE(in); IGNORE(i);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdPushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s,
                   uint32_t o, uint32_t n, const void *v) {
    IGNORE(c); IGNORE(l); IGNORE(s); IGNORE(o); IGNORE(n); IGNORE(v);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetViewport(VkCommandBuffer c, uint32_t f, uint32_t n, const VkViewport *v) {
    IGNORE(c); IGNORE(f); IGNORE(n); IGNORE(v);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetScissor(VkCommandBuffer c, uint32_t f, uint32_t n, const VkRect2D *r) {
    IGNORE(c); IGNORE(f); IGNORE(n); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetLineWidth(VkCommandBuffer c, float w) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!(w >= 1.0f) || !(w <= 64.0f)) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    command->dynamic_line_width = w;
    command->dynamic_line_width_set = VK_TRUE;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDepthBias(VkCommandBuffer c, float constantFactor,
                  float clamp, float slopeFactor) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    command->dynamic_depth_bias = (AgcGfx1013DepthBiasState){
        .constant_factor = constantFactor,
        .clamp = clamp,
        .slope_factor = slopeFactor,
    };
    command->dynamic_depth_bias_set = VK_TRUE;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetBlendConstants(VkCommandBuffer c, const float v[4]) { IGNORE(c); IGNORE(v); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDepthBounds(VkCommandBuffer c, float a, float b) { IGNORE(c); IGNORE(a); IGNORE(b); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilCompareMask(VkCommandBuffer c, VkStencilFaceFlags f, uint32_t m) {
    IGNORE(c); IGNORE(f); IGNORE(m);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilWriteMask(VkCommandBuffer c, VkStencilFaceFlags f, uint32_t m) {
    IGNORE(c); IGNORE(f); IGNORE(m);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilReference(VkCommandBuffer c, VkStencilFaceFlags f, uint32_t r) {
    IGNORE(c); IGNORE(f); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindIndexBuffer(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkIndexType t) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *buffer = (VkPs5Buffer *)b;
    uint32_t element_size = t == VK_INDEX_TYPE_UINT16 ? 2u :
        t == VK_INDEX_TYPE_UINT32 ? 4u : 0u;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !buffer ||
        !buffer->memory || !(buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
        !element_size || o >= buffer->size || (o & (element_size - 1u))) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = t == VK_INDEX_TYPE_UINT8_EXT ?
                VK_ERROR_FEATURE_NOT_PRESENT : VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    command->index_buffer = buffer;
    command->index_offset = o;
    command->index_type = t;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t f, uint32_t n,
                       const VkBuffer *b, const VkDeviceSize *o) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        f > VK_PS5_MAX_VERTEX_BINDINGS ||
        n > VK_PS5_MAX_VERTEX_BINDINGS - f || (n && (!b || !o))) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        VkPs5Buffer *buffer = (VkPs5Buffer *)b[i];
        if (!buffer || !buffer->memory ||
            !(buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            o[i] >= buffer->size) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        command->vertex_buffers[f + i] = buffer;
        command->vertex_offsets[f + i] = o[i];
    }
}

static VkResult prepare_vertex_table(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline,
    AgcGfx1013ResourceTableBinding *table)
{
    if (command->vertex_table_offset >
            VK_PS5_VERTEX_TABLE_SIZE - VK_PS5_VERTEX_TABLE_SLICE)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    size_t table_offset = command->vertex_table_offset;
    AgcGfx1013BufferDescriptor *descriptors =
        (AgcGfx1013BufferDescriptor *)
        ((uint8_t *)command->vertex_table_memory.cpu_address + table_offset);
    memset(descriptors, 0, VK_PS5_VERTEX_TABLE_SLICE);
    uint32_t descriptor_index = 0u;
    if (pipeline->robust_buffer_access) {
        for (uint32_t location = 0;
             location < OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES; ++location) {
            if (!(pipeline->vertex_attribute_mask & (1u << location))) continue;
            uint32_t binding = pipeline->vertex_attribute_bindings[location];
            VkPs5Buffer *buffer = command->vertex_buffers[binding];
            uint32_t stride = pipeline->vertex_strides[binding];
            uint32_t attribute_offset =
                pipeline->vertex_attribute_offsets[location];
            uint32_t attribute_size = pipeline->vertex_attribute_sizes[location];
            if (!buffer || !buffer->memory || !stride || !attribute_size ||
                command->vertex_offsets[binding] >= buffer->size ||
                buffer->memory_offset >
                    UINT64_MAX - command->vertex_offsets[binding])
                return VK_ERROR_INITIALIZATION_FAILED;
            VkDeviceSize available =
                buffer->size - command->vertex_offsets[binding];
            VkDeviceSize attribute_available =
                attribute_offset < available ? available - attribute_offset : 0u;
            uint64_t record_count = attribute_available >= attribute_size ?
                1u + (attribute_available - attribute_size) / stride : 0u;
            if (record_count > UINT32_MAX)
                return VK_ERROR_INITIALIZATION_FAILED;
            VkDeviceSize descriptor_offset = command->vertex_offsets[binding];
            if (attribute_offset < available)
                descriptor_offset += attribute_offset;
            if (buffer->memory_offset > UINT64_MAX - descriptor_offset)
                return VK_ERROR_INITIALIZATION_FAILED;
            uint64_t address = vk_ps5_memory_gpu_address(buffer->memory,
                buffer->memory_offset + descriptor_offset);
            if (agcGfx1013BufferDescriptorEncode(
                    &descriptors[descriptor_index++], address, stride,
                    (uint32_t)record_count) != AGC_OK)
                return VK_ERROR_INITIALIZATION_FAILED;
        }
    } else {
        for (uint32_t binding = 0;
             binding < VK_PS5_MAX_VERTEX_BINDINGS; ++binding) {
            if (!(pipeline->vertex_binding_mask & (1u << binding))) continue;
            VkPs5Buffer *buffer = command->vertex_buffers[binding];
            uint32_t stride = pipeline->vertex_strides[binding];
            if (!buffer || !buffer->memory || !stride ||
                command->vertex_offsets[binding] >= buffer->size)
                return VK_ERROR_INITIALIZATION_FAILED;
            VkDeviceSize available =
                buffer->size - command->vertex_offsets[binding];
            if (available / stride == 0u || available / stride > UINT32_MAX ||
                buffer->memory_offset >
                    UINT64_MAX - command->vertex_offsets[binding])
                return VK_ERROR_INITIALIZATION_FAILED;
            uint64_t address = vk_ps5_memory_gpu_address(buffer->memory,
                buffer->memory_offset + command->vertex_offsets[binding]);
            if (agcGfx1013BufferDescriptorEncode(
                    &descriptors[descriptor_index++], address, stride,
                    (uint32_t)(available / stride)) != AGC_OK)
                return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    if (agcGpuMemoryFlush(&command->vertex_table_memory, table_offset,
            VK_PS5_VERTEX_TABLE_SLICE) != AGC_OK)
        return VK_ERROR_DEVICE_LOST;
    uint64_t address = command->vertex_table_memory.gpu_address + table_offset;
#if defined(OPENAGC_GENERIC)
    address = ((uint64_t)AGC_GFX1013_ADDRESS32_HIGH << 32u) |
        (uint32_t)address;
#endif
    table->placeholder = OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER;
    table->address = address;
    command->vertex_table_offset += VK_PS5_VERTEX_TABLE_SLICE;
    return VK_SUCCESS;
}

static VkResult prepare_indirect_descriptor_table(
    VkPs5CommandBuffer *command, VkPs5DescriptorSet *const *sets,
    uint32_t *table_address)
{
    if (!sets || !table_address ||
        command->vertex_table_offset >
            VK_PS5_VERTEX_TABLE_SIZE -
                VK_PS5_INDIRECT_DESCRIPTOR_TABLE_SLICE)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    size_t table_offset = command->vertex_table_offset;
    uint32_t *set_addresses = (uint32_t *)(
        (uint8_t *)command->vertex_table_memory.cpu_address + table_offset);
    memset(set_addresses, 0, VK_PS5_INDIRECT_DESCRIPTOR_TABLE_SLICE);
    for (uint32_t set = 0; set < OPENAGC_PSBC_MAX_DESCRIPTOR_SETS; ++set) {
        if (!sets[set])
            continue;
        uint64_t set_address = descriptor_table_gpu_address(sets[set]);
        if ((uint32_t)(set_address >> 32u) !=
                AGC_GFX1013_ADDRESS32_HIGH || !(uint32_t)set_address)
            return VK_ERROR_INITIALIZATION_FAILED;
        set_addresses[set] = (uint32_t)set_address;
    }
    if (agcGpuMemoryFlush(&command->vertex_table_memory, table_offset,
            VK_PS5_INDIRECT_DESCRIPTOR_TABLE_SLICE) != AGC_OK)
        return VK_ERROR_DEVICE_LOST;
    uint64_t address = command->vertex_table_memory.gpu_address + table_offset;
#if defined(OPENAGC_GENERIC)
    address = ((uint64_t)AGC_GFX1013_ADDRESS32_HIGH << 32u) |
        (uint32_t)address;
#endif
    if ((uint32_t)(address >> 32u) != AGC_GFX1013_ADDRESS32_HIGH)
        return VK_ERROR_INITIALIZATION_FAILED;
    *table_address = (uint32_t)address;
    if (!*table_address)
        return VK_ERROR_INITIALIZATION_FAILED;
    command->last_indirect_descriptor_table = *table_address;
    command->last_indirect_descriptor_table_offset = table_offset;
    command->vertex_table_offset +=
        VK_PS5_INDIRECT_DESCRIPTOR_TABLE_SLICE;
    return VK_SUCCESS;
}

static VkResult prepare_graphics_stage_user_data(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline,
    uint32_t stage_index, bool allow_vertex_table, bool indexed,
    uint32_t first_element, int32_t vertex_offset, uint32_t first_instance,
    bool indirect_draw, uint32_t *base_vertex_location,
    uint32_t *start_instance_location, uint32_t *draw_index_location,
    AgcGfx1013ResourceTableBinding *resource_tables,
    uint32_t *resource_table_count, AgcRegisterValue *user_data,
    uint32_t *user_data_count, uint32_t user_data_capacity)
{
    const OpenAgcPsbcMetadata *metadata =
        &pipeline->stages[stage_index].metadata;
    for (uint32_t n = 0; n < metadata->user_sgpr_count; ++n) {
        const OpenAgcPsbcUserSgpr *sgpr = &metadata->user_sgprs[n];
        if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_DESCRIPTOR_SET)
            continue;
        if (sgpr->kind ==
                OPENAGC_PSBC_USER_SGPR_INDIRECT_DESCRIPTOR_SETS) {
            if (sgpr->dword_count != 1u ||
                *user_data_count == user_data_capacity)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            uint32_t address;
            VkResult result = prepare_indirect_descriptor_table(
                command, command->graphics_sets, &address);
            if (result != VK_SUCCESS)
                return result;
            user_data[(*user_data_count)++] = (AgcRegisterValue){
                sgpr->register_offset, address,
            };
            command->last_indirect_descriptor_register =
                sgpr->register_offset;
            continue;
        }
        if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_VERTEX_BUFFER_TABLE) {
            if (!allow_vertex_table || *resource_table_count != 0u ||
                sgpr->dword_count != 1u)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            VkResult result = prepare_vertex_table(command, pipeline,
                &resource_tables[*resource_table_count]);
            if (result != VK_SUCCESS)
                return result;
            ++*resource_table_count;
            continue;
        }
        uint32_t value;
        if (indirect_draw &&
            (sgpr->kind == OPENAGC_PSBC_USER_SGPR_BASE_VERTEX ||
             sgpr->kind == OPENAGC_PSBC_USER_SGPR_START_INSTANCE ||
             sgpr->kind == OPENAGC_PSBC_USER_SGPR_DRAW_INDEX)) {
            uint32_t *location = draw_index_location;
            if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_BASE_VERTEX)
                location = base_vertex_location;
            else if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_START_INSTANCE)
                location = start_instance_location;
            if (!location || *location != UINT32_MAX ||
                sgpr->dword_count != 1u)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            *location = sgpr->register_offset;
            continue;
        }
        if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_BASE_VERTEX)
            value = indexed ? (uint32_t)vertex_offset : first_element;
        else if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_START_INSTANCE)
            value = first_instance;
        else if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_DRAW_INDEX)
            value = 0u;
        else if (sgpr->kind == OPENAGC_PSBC_USER_SGPR_PUSH_CONSTANT_POINTER &&
                 metadata->push_constant_size == 0u)
            value = 0u;
        else
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (sgpr->dword_count != 1u ||
            *user_data_count == user_data_capacity)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        user_data[(*user_data_count)++] = (AgcRegisterValue){
            sgpr->register_offset, value,
        };
    }
    return VK_SUCCESS;
}

static bool record_color_blend(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline)
{
    int32_t result = agcGfx1013SetColorBlendState(
        &command->dcb, &pipeline->color_blend);
    if (result == AGC_OK)
        return true;
    command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
        VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    return false;
}

static bool record_raster_state(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline,
    AgcGfx1013FrameState *frame)
{
    AgcGfx1013PrimitiveSizeState primitive_size = pipeline->primitive_size;
    if (pipeline->line_width_dynamic) {
        if (!command->dynamic_line_width_set) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return false;
        }
        primitive_size.line_width = command->dynamic_line_width;
    }
    int32_t result = agcGfx1013SetPrimitiveSizeState(
        &command->dcb, &primitive_size);
    if (result != AGC_OK) {
        command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
        return false;
    }
    result = agcGfx1013ApplyPolygonMode(
        frame, pipeline->polygon_mode);
    if (result != AGC_OK) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return false;
    }
    if (!pipeline->depth_bias_enable)
        return true;
    AgcGfx1013DepthBiasState state = pipeline->depth_bias;
    if (pipeline->depth_bias_dynamic) {
        if (!command->dynamic_depth_bias_set) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return false;
        }
        state = command->dynamic_depth_bias;
    }
    frame->raster_mode_control |= AGC_GFX1013_DEPTH_BIAS_RASTER_MODE;
    if (!pipeline->has_depth_stencil ||
        command->depth_surface_state.format ==
            AGC_GFX1013_DEPTH_FORMAT_S8_UINT)
        return true;
    state.format = command->depth_surface_state.format;
    result = agcGfx1013SetDepthBiasState(&command->dcb, &state);
    if (result == AGC_OK)
        return true;
    command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
        VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    return false;
}

static void record_tessellation_draw(
    VkPs5CommandBuffer *command, VkPs5Pipeline *pipeline,
    uint32_t element_count, uint32_t instance_count,
    uint32_t first_element, uint32_t first_instance, bool indexed)
{
    if (indexed || !pipeline->tessellation ||
        !pipeline->tess_ring_descriptor_address) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    AgcRegisterValue user_data[3u * OPENAGC_PSBC_MAX_USER_SGPRS];
    uint32_t user_data_count = 0u;
    AgcGfx1013ResourceTableBinding
        hull_tables[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS + 1u];
    AgcGfx1013ResourceTableBinding
        primitive_tables[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS + 1u];
    AgcGfx1013ResourceTableBinding
        pixel_tables[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    uint32_t hull_table_count = 0u;
    uint32_t primitive_table_count = 0u;
    uint32_t pixel_table_count = 0u;
    VkResult result = prepare_graphics_stage_user_data(
        command, pipeline, 0u, true, false, first_element, 0,
        first_instance, false, NULL, NULL, NULL,
        hull_tables, &hull_table_count, user_data,
        &user_data_count, 3u * OPENAGC_PSBC_MAX_USER_SGPRS);
    if (result == VK_SUCCESS)
        result = prepare_graphics_stage_user_data(
            command, pipeline, 1u, true, false, first_element, 0,
            first_instance, false, NULL, NULL, NULL,
            primitive_tables, &primitive_table_count,
            user_data, &user_data_count,
            3u * OPENAGC_PSBC_MAX_USER_SGPRS);
    if (result == VK_SUCCESS)
        result = prepare_graphics_stage_user_data(
            command, pipeline, 2u, false, false, first_element, 0,
            first_instance, false, NULL, NULL, NULL,
            pixel_tables, &pixel_table_count, user_data,
            &user_data_count, 3u * OPENAGC_PSBC_MAX_USER_SGPRS);
    uint32_t descriptor_count = 0u;
    if (result == VK_SUCCESS)
        result = prepare_graphics_descriptor_tables(command, pipeline, 0u,
            &hull_tables[hull_table_count], &descriptor_count);
    hull_table_count += descriptor_count;
    descriptor_count = 0u;
    if (result == VK_SUCCESS)
        result = prepare_graphics_descriptor_tables(command, pipeline, 1u,
            &primitive_tables[primitive_table_count], &descriptor_count);
    primitive_table_count += descriptor_count;
    descriptor_count = 0u;
    if (result == VK_SUCCESS)
        result = prepare_graphics_descriptor_tables(command, pipeline, 2u,
            pixel_tables, &descriptor_count);
    pixel_table_count = descriptor_count;
    if (result != VK_SUCCESS) {
        command->record_error = result;
        return;
    }
    if (pipeline->viewport.width > command->active_framebuffer->width ||
        pipeline->viewport.height > command->active_framebuffer->height ||
        pipeline->scissor.right > command->active_framebuffer->width ||
        pipeline->scissor.bottom > command->active_framebuffer->height) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    AgcGfx1013FrameState draw_frame = command->frame_state;
    if (pipeline->depth_clamp_enable)
        draw_frame.clip_control = AGC_GFX1013_DEPTH_CLAMP_CLIP_CONTROL;
    draw_frame.viewport = pipeline->viewport;
    draw_frame.scissor = pipeline->scissor;
    const VkPs5RuntimeShader *hull = &pipeline->runtime[0];
    const VkPs5RuntimeShader *primitive = &pipeline->runtime[1];
    const VkPs5RuntimeShader *pixel = &pipeline->runtime[2];
    const AgcGfx1013TessDrawState draw = {
        .shaders = {
            .hull = hull->binding,
            .primitive = primitive->binding,
            .pixel = pixel->binding,
            .hull_back_code_address = hull->binding.code_address,
            .primitive_back_code_address = primitive->binding.code_address,
            .ring_descriptor_address =
                pipeline->tess_ring_descriptor_address,
            .tcs_offchip_layout = pipeline->tcs_offchip_layout,
            .tes_offchip_layout = pipeline->tes_offchip_layout,
            .hull_lds_size =
                pipeline->stages[0].metadata.tessellation_lds_size,
            .primitive_type = pipeline->primitive_type,
        },
        .frame = &draw_frame,
        .tessellation = pipeline->tessellation,
        .depth_surface_state = pipeline->has_depth_stencil ?
            &command->depth_surface_state : NULL,
        .depth_stencil_state = pipeline->has_depth_stencil ?
            &pipeline->depth_stencil : NULL,
        .hull_resource_tables = hull_tables,
        .num_hull_resource_tables = hull_table_count,
        .primitive_resource_tables = primitive_tables,
        .num_primitive_resource_tables = primitive_table_count,
        .pixel_resource_tables = pixel_tables,
        .num_pixel_resource_tables = pixel_table_count,
        .post_bind_sh_registers = user_data,
        .num_post_bind_sh_registers = user_data_count,
        .instance_count = instance_count,
        .vertex_count = element_count,
        .draw_modifier = 0x40000000u,
    };
    if (!record_color_blend(command, pipeline) ||
        !record_raster_state(command, pipeline, &draw_frame))
        return;
    int32_t draw_result = agcGfx1013SetTessellationRings(
        &command->dcb, pipeline->tessellation);
    if (draw_result == AGC_OK)
        draw_result = agcGfx1013DrawTessIndexAuto(&command->dcb, &draw);
    if (draw_result != AGC_OK)
        command->record_error = draw_result == AGC_ERROR_BUFFER_TOO_SMALL ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
}

typedef struct VkPs5PreparedBaselineDraw {
    AgcRegisterValue user_data[OPENAGC_PSBC_MAX_USER_SGPRS];
    AgcGfx1013ResourceTableBinding
        primitive_tables[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS + 1u];
    AgcGfx1013ResourceTableBinding
        pixel_tables[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    AgcGfx1013FrameState frame;
    AgcGfx1013BaselineDrawState draw;
    uint32_t base_vertex_location;
    uint32_t start_instance_location;
    uint32_t draw_index_location;
} VkPs5PreparedBaselineDraw;

static VkResult prepare_baseline_draw(
    VkPs5CommandBuffer *command, VkPs5Pipeline *pipeline, bool indexed,
    uint32_t element_count, uint32_t instance_count,
    uint32_t first_element, int32_t vertex_offset, uint32_t first_instance,
    bool indirect_draw, VkPs5PreparedBaselineDraw *prepared)
{
    memset(prepared, 0, sizeof(*prepared));
    prepared->base_vertex_location = UINT32_MAX;
    prepared->start_instance_location = UINT32_MAX;
    prepared->draw_index_location = UINT32_MAX;
    uint32_t primitive_table_count = 0u;
    uint32_t pixel_table_count = 0u;
    uint32_t user_data_count = 0u;
    VkResult result = prepare_graphics_stage_user_data(
        command, pipeline, 0u, true, indexed, first_element, vertex_offset,
        first_instance, indirect_draw, &prepared->base_vertex_location,
        &prepared->start_instance_location, &prepared->draw_index_location,
        prepared->primitive_tables,
        &primitive_table_count, prepared->user_data, &user_data_count,
        OPENAGC_PSBC_MAX_USER_SGPRS);
    if (result != VK_SUCCESS)
        return result;
    for (uint32_t n = 0;
         n < pipeline->stages[1].metadata.user_sgpr_count; ++n) {
        if (pipeline->stages[1].metadata.user_sgprs[n].kind !=
                OPENAGC_PSBC_USER_SGPR_DESCRIPTOR_SET)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    uint32_t descriptor_table_count = 0u;
    result = prepare_graphics_descriptor_tables(command, pipeline, 0u,
        &prepared->primitive_tables[primitive_table_count],
        &descriptor_table_count);
    if (result != VK_SUCCESS)
        return result;
    primitive_table_count += descriptor_table_count;
    result = prepare_graphics_descriptor_tables(command, pipeline, 1u,
        prepared->pixel_tables, &pixel_table_count);
    if (result != VK_SUCCESS)
        return result;
    if (pipeline->viewport.width > command->active_framebuffer->width ||
        pipeline->viewport.height > command->active_framebuffer->height ||
        pipeline->scissor.right > command->active_framebuffer->width ||
        pipeline->scissor.bottom > command->active_framebuffer->height)
        return VK_ERROR_INITIALIZATION_FAILED;

    prepared->frame = command->frame_state;
    if (pipeline->depth_clamp_enable)
        prepared->frame.clip_control =
            AGC_GFX1013_DEPTH_CLAMP_CLIP_CONTROL;
    prepared->frame.viewport = pipeline->viewport;
    prepared->frame.scissor = pipeline->scissor;
    const VkPs5RuntimeShader *primitive = &pipeline->runtime[0];
    const VkPs5RuntimeShader *pixel = &pipeline->runtime[1];
    prepared->draw = (AgcGfx1013BaselineDrawState){
        .shaders = {
            .primitive = primitive->binding,
            .pixel = pixel->binding,
            .primitive_back_code_address = primitive->binding.code_address,
            .primitive_type = pipeline->primitive_type,
        },
        .frame = &prepared->frame,
        .depth_surface_state = pipeline->has_depth_stencil ?
            &command->depth_surface_state : NULL,
        .depth_stencil_state = pipeline->has_depth_stencil ?
            &pipeline->depth_stencil : NULL,
        .primitive_resource_tables = prepared->primitive_tables,
        .num_primitive_resource_tables = primitive_table_count,
        .pixel_resource_tables = prepared->pixel_tables,
        .num_pixel_resource_tables = pixel_table_count,
        .post_bind_sh_registers = prepared->user_data,
        .num_post_bind_sh_registers = user_data_count,
        .index_type = indexed && command->index_type == VK_INDEX_TYPE_UINT32 ?
            kAgcIndexSize32 : kAgcIndexSize16,
        .instance_count = indirect_draw ? 1u : instance_count,
        .vertex_count = indirect_draw ? 1u : element_count,
        .draw_modifier = 0x40000000u,
    };
    return VK_SUCCESS;
}

static void record_graphics_draw(
    VkPs5CommandBuffer *command, uint32_t element_count,
    uint32_t instance_count, uint32_t first_element, int32_t vertex_offset,
    uint32_t first_instance, bool indexed)
{
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    VkPs5Pipeline *pipeline = command->bound_graphics;
    bool baseline = pipeline && pipeline->stage_count == 2u &&
        (pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_VERTEX ||
         pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_GEOMETRY) &&
        pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_FRAGMENT;
    bool tessellation = pipeline && pipeline->stage_count == 3u &&
        pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_TESS_CONTROL &&
        (pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_TESS_EVALUATION ||
         pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_GEOMETRY) &&
        pipeline->stage_types[2] == OPENAGC_PSBC_STAGE_FRAGMENT;
    if (!pipeline || !command->active_render_pass ||
        (!baseline && !tessellation) || !element_count || !instance_count) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    if (tessellation) {
        record_tessellation_draw(command, pipeline, element_count,
            instance_count, first_element, first_instance, indexed);
        return;
    }
    VkPs5PreparedBaselineDraw prepared;
    VkResult prepare_result = prepare_baseline_draw(command, pipeline, indexed,
        element_count, instance_count, first_element, vertex_offset,
        first_instance, false, &prepared);
    if (prepare_result != VK_SUCCESS) {
        command->record_error = prepare_result;
        return;
    }
    if (!record_color_blend(command, pipeline) ||
        !record_raster_state(command, pipeline, &prepared.frame))
        return;
    int32_t result;
    if (indexed) {
        VkPs5Buffer *index = command->index_buffer;
        uint32_t element_size = command->index_type == VK_INDEX_TYPE_UINT32 ?
            4u : 2u;
        if (!index || !index->memory || command->index_offset >= index->size ||
            index->memory_offset > UINT64_MAX - command->index_offset) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        VkDeviceSize available = index->size - command->index_offset;
        if (available / element_size > UINT32_MAX) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        const AgcGfx1013IndexedDrawState indexed_draw = {
            .draw = prepared.draw,
            .index_buffer_address = vk_ps5_memory_gpu_address(index->memory,
                index->memory_offset + command->index_offset),
            .index_buffer_count = (uint32_t)(available / element_size),
            .first_index = first_element,
            .index_count = element_count,
            .draw_initiator = 0u,
        };
        result = agcGfx1013DrawBaselineIndexed(&command->dcb, &indexed_draw);
    } else {
        result = agcGfx1013DrawBaselineIndexAuto(
            &command->dcb, &prepared.draw);
    }
    if (result != AGC_OK)
        command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) {
    record_graphics_draw((VkPs5CommandBuffer *)c, v, i, fv, 0, fi, false);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexed(VkCommandBuffer c, uint32_t i, uint32_t n, uint32_t f,
                 int32_t v, uint32_t fi) {
    record_graphics_draw((VkPs5CommandBuffer *)c, i, n, f, v, fi, true);
}

static void record_graphics_indirect(
    VkPs5CommandBuffer *command, VkPs5Buffer *arguments,
    VkDeviceSize offset, uint32_t draw_count, uint32_t stride, bool indexed)
{
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (draw_count == 0u)
        return;
    VkPs5Pipeline *pipeline = command->bound_graphics;
    bool baseline = pipeline && pipeline->stage_count == 2u &&
        (pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_VERTEX ||
         pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_GEOMETRY) &&
        pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_FRAGMENT;
    uint32_t argument_size = indexed ?
        (uint32_t)sizeof(VkDrawIndexedIndirectCommand) :
        (uint32_t)sizeof(VkDrawIndirectCommand);
    if (!baseline || !command->active_render_pass || !arguments ||
        !arguments->memory ||
        !(arguments->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ||
        (offset & 3u) != 0u ||
        (draw_count > 1u &&
         (stride < argument_size || (stride & 3u) != 0u))) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    VkDeviceSize last_offset = offset;
    if (draw_count > 1u) {
        uint64_t extra_count = (uint64_t)draw_count - 1u;
        if (extra_count > (UINT64_MAX - last_offset) / stride) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        last_offset += extra_count * stride;
    }
    if (last_offset > arguments->size ||
        argument_size > arguments->size - last_offset ||
        arguments->memory_offset > UINT64_MAX - offset) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    VkPs5PreparedBaselineDraw prepared;
    VkResult prepare_result = prepare_baseline_draw(command, pipeline, indexed,
        1u, 1u, 0u, 0, 0u, true, &prepared);
    if (prepare_result != VK_SUCCESS) {
        command->record_error = prepare_result;
        return;
    }
    uint64_t argument_address = vk_ps5_memory_gpu_address(arguments->memory,
        arguments->memory_offset + offset);
    bool expand_draw_index = prepared.draw_index_location != UINT32_MAX;
    if (expand_draw_index) {
        if (prepared.draw.num_post_bind_sh_registers >=
                OPENAGC_PSBC_MAX_USER_SGPRS) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        prepared.user_data[prepared.draw.num_post_bind_sh_registers++] =
            (AgcRegisterValue){ prepared.draw_index_location, 0u };
    }
    AgcGfx1013IndirectDrawState draw = {
        .draw = prepared.draw,
        .argument_buffer_address = argument_address & ~UINT64_C(7),
        .argument_offset = (uint32_t)(argument_address & UINT64_C(7)),
        .draw_count = expand_draw_index ? 1u : draw_count,
        .stride = stride,
        .base_vertex_location = prepared.base_vertex_location == UINT32_MAX ?
            0u : prepared.base_vertex_location,
        .start_instance_location =
            prepared.start_instance_location == UINT32_MAX ?
                0u : prepared.start_instance_location,
        .draw_initiator = indexed ? 0u : 2u,
        .indexed = indexed ? 1u : 0u,
    };
    if (indexed) {
        VkPs5Buffer *index = command->index_buffer;
        uint32_t element_size = command->index_type == VK_INDEX_TYPE_UINT32 ?
            4u : 2u;
        if (!index || !index->memory || command->index_offset >= index->size ||
            index->memory_offset > UINT64_MAX - command->index_offset) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        VkDeviceSize available = index->size - command->index_offset;
        if (available / element_size == 0u ||
            available / element_size > UINT32_MAX) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        draw.index_buffer_address = vk_ps5_memory_gpu_address(index->memory,
            index->memory_offset + command->index_offset);
        draw.index_buffer_count = (uint32_t)(available / element_size);
    }
    if (!record_color_blend(command, pipeline) ||
        !record_raster_state(command, pipeline, &prepared.frame))
        return;
    uint32_t packet_count = expand_draw_index ? draw_count : 1u;
    for (uint32_t n = 0u; n < packet_count; ++n) {
        if (expand_draw_index) {
            uint64_t draw_address = argument_address + (uint64_t)n * stride;
            draw.argument_buffer_address = draw_address & ~UINT64_C(7);
            draw.argument_offset = (uint32_t)(draw_address & UINT64_C(7));
            prepared.user_data[prepared.draw.num_post_bind_sh_registers - 1u]
                .value = n;
        }
        int32_t result = agcGfx1013DrawBaselineIndirect(&command->dcb, &draw);
        if (result != AGC_OK) {
            command->record_error = result == AGC_ERROR_BUFFER_TOO_SMALL ?
                VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, uint32_t n, uint32_t s) {
    record_graphics_indirect((VkPs5CommandBuffer *)c, (VkPs5Buffer *)b,
        o, n, s, false);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexedIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o,
                         uint32_t n, uint32_t s) {
    record_graphics_indirect((VkPs5CommandBuffer *)c, (VkPs5Buffer *)b,
        o, n, s, true);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
               VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f) {
    IGNORE(c); IGNORE(s); IGNORE(sl); IGNORE(d); IGNORE(dl); IGNORE(n); IGNORE(r); IGNORE(f);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearDepthStencilImage(VkCommandBuffer c, VkImage i, VkImageLayout l,
                            const VkClearDepthStencilValue *v, uint32_t n,
                            const VkImageSubresourceRange *r) {
    IGNORE(c); IGNORE(i); IGNORE(l); IGNORE(v); IGNORE(n); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearAttachments(VkCommandBuffer c, uint32_t n, const VkClearAttachment *a,
                      uint32_t rn, const VkClearRect *r) {
    IGNORE(c); IGNORE(n); IGNORE(a); IGNORE(rn); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResolveImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
                  VkImageLayout dl, uint32_t n, const VkImageResolve *r) {
    IGNORE(c); IGNORE(s); IGNORE(sl); IGNORE(d); IGNORE(dl); IGNORE(n); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *b,
                     VkSubpassContents s) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!b || b->sType != VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO ||
        !b->renderPass || !b->framebuffer || command->active_render_pass ||
        s != VK_SUBPASS_CONTENTS_INLINE) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    VkPs5RenderPass *render_pass = (VkPs5RenderPass *)b->renderPass;
    VkPs5Framebuffer *framebuffer = (VkPs5Framebuffer *)b->framebuffer;
    uint32_t color_count = render_pass->subpasses[0].color_attachment_count;
    if (framebuffer->render_pass != render_pass || !render_pass->subpass_count ||
        !color_count || color_count > AGC_GFX1013_MAX_COLOR_TARGETS ||
        b->renderArea.offset.x < 0 || b->renderArea.offset.y < 0 ||
        !b->renderArea.extent.width || !b->renderArea.extent.height ||
        (uint32_t)b->renderArea.offset.x > framebuffer->width ||
        b->renderArea.extent.width >
            framebuffer->width - (uint32_t)b->renderArea.offset.x ||
        (uint32_t)b->renderArea.offset.y > framebuffer->height ||
        b->renderArea.extent.height >
            framebuffer->height - (uint32_t)b->renderArea.offset.y) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    AgcGfx1013FrameState frame = {0};
    frame.color_target_count = color_count;
    for (uint32_t slot = 0; slot < color_count; ++slot) {
        uint32_t attachment_index =
            render_pass->subpasses[0].color_attachments[slot];
        if (attachment_index == VK_ATTACHMENT_UNUSED ||
            attachment_index >= framebuffer->attachment_count) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        const VkAttachmentDescription *attachment =
            &render_pass->attachments[attachment_index];
        VkPs5ImageView *view = framebuffer->attachments[attachment_index];
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        AgcGfx1013ColorTargetFormat target_format;
        AgcGfx1013ResourceUsage before;
        if (attachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
            !image || !image->memory ||
            !(image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            image->tiling != VK_IMAGE_TILING_LINEAR ||
            !color_target_format(view->format, &target_format) ||
            !layout_resource_usage(attachment->initialLayout, &before)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        AgcGfx1013ColorTargetState *target = slot == 0u ?
            &frame.color_target : &frame.additional_color_targets[slot - 1u];
        uint64_t address = vk_ps5_memory_gpu_address(
            image->memory, image->memory_offset);
        if (agcGfx1013InitColorTarget(target, address,
                framebuffer->width, framebuffer->height,
                target_format) != AGC_OK) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        const AgcGfx1013ResourceTransition transition = {
            .before = before,
            .after = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET,
        };
        if (agcGfx1013TransitionResource(
                &command->dcb, &transition) != AGC_OK) {
            command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
    }
    frame.viewport.width = b->renderArea.extent.width;
    frame.viewport.height = b->renderArea.extent.height;
    frame.viewport.depth_clip_space = AGC_GFX1013_CLIP_SPACE_ZERO_TO_ONE;
    frame.scissor.left = (uint32_t)b->renderArea.offset.x;
    frame.scissor.top = (uint32_t)b->renderArea.offset.y;
    frame.scissor.right = frame.scissor.left + b->renderArea.extent.width;
    frame.scissor.bottom = frame.scissor.top + b->renderArea.extent.height;
    frame.target_mask = color_count == AGC_GFX1013_MAX_COLOR_TARGETS ?
        UINT32_MAX : (1u << (color_count * 4u)) - 1u;
    frame.context_load_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE;
    frame.context_shadow_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE;
    frame.max_vertex_index = UINT32_MAX;
    frame.ngg_mode_control = AGC_GFX1013_NGG_MODE_CONTROL;
    frame.vertex_reuse_block_control = AGC_GFX1013_VERTEX_REUSE_BLOCK;
    frame.instance_step_rate = 1u;
    frame.clip_control = AGC_GFX1013_VULKAN_CLIP_CONTROL;
    AgcGfx1013GraphicsDefaultStats stats;
    if (agcGfx1013BuildFramePrologue(
            &command->dcb, &frame, &stats) != AGC_OK) {
        command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    memset(&command->depth_surface_state, 0,
           sizeof(command->depth_surface_state));
    uint32_t depth_index =
        render_pass->subpasses[0].depth_stencil_attachment;
    if (depth_index != VK_ATTACHMENT_UNUSED) {
        const VkAttachmentDescription *depth_attachment =
            &render_pass->attachments[depth_index];
        VkPs5ImageView *depth_view = framebuffer->attachments[depth_index];
        VkPs5Image *depth_image = depth_view ?
            (VkPs5Image *)depth_view->image : NULL;
        AgcGfx1013ResourceUsage depth_before;
        if (depth_attachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
            depth_attachment->stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
            !depth_image || !depth_image->memory ||
            !depth_image->is_depth_surface ||
            !(depth_image->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
            depth_image->extent.width < framebuffer->width ||
            depth_image->extent.height < framebuffer->height ||
            !layout_resource_usage(
                depth_attachment->initialLayout, &depth_before)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        uint64_t depth_base = vk_ps5_memory_gpu_address(
            depth_image->memory, depth_image->memory_offset);
        const bool has_depth = depth_image->format != VK_FORMAT_S8_UINT;
        const bool has_stencil = depth_image->format == VK_FORMAT_S8_UINT ||
            depth_image->format == VK_FORMAT_D16_UNORM_S8_UINT ||
            depth_image->format == VK_FORMAT_D32_SFLOAT_S8_UINT;
        command->depth_surface_state = (AgcGfx1013DepthSurfaceState){
            .depth_read_address = has_depth ?
                depth_base + depth_image->depth_plane_offset : 0u,
            .depth_write_address = has_depth ?
                depth_base + depth_image->depth_plane_offset : 0u,
            .stencil_read_address = has_stencil ?
                depth_base + depth_image->stencil_plane_offset : 0u,
            .stencil_write_address = has_stencil ?
                depth_base + depth_image->stencil_plane_offset : 0u,
            .width = framebuffer->width,
            .height = framebuffer->height,
            .format = depth_image->depth_format,
            .depth_swizzle_mode = has_depth ?
                AGC_GFX1013_SWIZZLE_64KB_Z_X : 0u,
            .stencil_swizzle_mode = has_stencil ?
                AGC_GFX1013_SWIZZLE_64KB_Z_X : 0u,
            .mip_level_count = 1u,
            .last_layer = framebuffer->layers - 1u,
            .sample_count = 1u,
            .depth_read_only = render_pass->subpasses[0].depth_stencil_layout ==
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            .stencil_read_only = render_pass->subpasses[0].depth_stencil_layout ==
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        };
        const AgcGfx1013ResourceTransition depth_transition = {
            .before = depth_before,
            .after = render_pass->subpasses[0].depth_stencil_layout ==
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ?
                AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ :
                AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE,
        };
        if (agcGfx1013TransitionResource(
                &command->dcb, &depth_transition) != AGC_OK) {
            command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
    }
    command->frame_state = frame;
    command->active_render_pass = render_pass;
    command->active_framebuffer = framebuffer;
    command->active_subpass = 0u;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdNextSubpass(VkCommandBuffer c, VkSubpassContents s) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!command->active_render_pass || s != VK_SUBPASS_CONTENTS_INLINE ||
        command->active_subpass + 1u >= command->active_render_pass->subpass_count) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndRenderPass(VkCommandBuffer c) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!command->active_render_pass || !command->active_framebuffer) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (command->active_query_pool) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    uint32_t color_count = command->active_render_pass->
        subpasses[command->active_subpass].color_attachment_count;
    for (uint32_t slot = 0; slot < color_count; ++slot) {
        uint32_t attachment_index = command->active_render_pass->
            subpasses[command->active_subpass].color_attachments[slot];
        const VkAttachmentDescription *attachment =
            &command->active_render_pass->attachments[attachment_index];
        AgcGfx1013ResourceUsage after;
        if (!layout_resource_usage(attachment->finalLayout, &after)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        const AgcGfx1013ResourceTransition transition = {
            .before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET,
            .after = after,
        };
        if (agcGfx1013TransitionResource(
                &command->dcb, &transition) != AGC_OK) {
            command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
    }
    uint32_t depth_index = command->active_render_pass->
        subpasses[command->active_subpass].depth_stencil_attachment;
    if (depth_index != VK_ATTACHMENT_UNUSED) {
        const VkAttachmentDescription *depth_attachment =
            &command->active_render_pass->attachments[depth_index];
        AgcGfx1013ResourceUsage depth_after;
        if (!layout_resource_usage(
                depth_attachment->finalLayout, &depth_after)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        const AgcGfx1013ResourceTransition depth_transition = {
            .before = command->active_render_pass->
                subpasses[command->active_subpass].depth_stencil_layout ==
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ?
                AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ :
                AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE,
            .after = depth_after,
        };
        if (agcGfx1013TransitionResource(
                &command->dcb, &depth_transition) != AGC_OK) {
            command->record_error = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
    }
    command->active_render_pass = NULL;
    command->active_framebuffer = NULL;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDeviceMask(VkCommandBuffer c, uint32_t m) { IGNORE(c); IGNORE(m); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatchBase(VkCommandBuffer c, uint32_t bx, uint32_t by, uint32_t bz,
                  uint32_t x, uint32_t y, uint32_t z) {
    IGNORE(c); IGNORE(bx); IGNORE(by); IGNORE(bz); IGNORE(x); IGNORE(y); IGNORE(z);
}
