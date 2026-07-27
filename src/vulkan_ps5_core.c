#include "vulkan_ps5_internal.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct VkPs5Fence { atomic_bool signaled; } VkPs5Fence;
typedef struct VkPs5Semaphore { atomic_bool signaled; } VkPs5Semaphore;
typedef struct VkPs5Event { atomic_int status; } VkPs5Event;

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
    VkDeviceSize size;
    VkDeviceMemory memory;
    VkDeviceSize memory_offset;
} VkPs5Image;

typedef struct VkPs5ImageView { VkImage image; VkFormat format; } VkPs5ImageView;
typedef struct VkPs5BufferView { VkBuffer buffer; VkFormat format; } VkPs5BufferView;
typedef struct VkPs5Opaque { uint32_t kind; } VkPs5Opaque;

typedef struct VkPs5ShaderModule {
    size_t code_size;
    uint32_t code[];
} VkPs5ShaderModule;

typedef struct VkPs5PipelineCache {
    size_t data_size;
    uint8_t data[];
} VkPs5PipelineCache;

typedef struct VkPs5QueryPool {
    VkQueryType type;
    uint32_t count;
    uint64_t values[];
} VkPs5QueryPool;

typedef struct VkPs5DescriptorSet VkPs5DescriptorSet;
typedef struct VkPs5DescriptorPool {
    VkDevice device;
    uint32_t max_sets;
    uint32_t allocated_sets;
    VkPs5DescriptorSet *sets;
} VkPs5DescriptorPool;

struct VkPs5DescriptorSet {
    VkPs5DescriptorPool *pool;
    VkPs5DescriptorSet *next;
};

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
    struct VkPs5CommandBuffer *next;
} VkPs5CommandBuffer;

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
            command->state = VK_PS5_COMMAND_EXECUTABLE;
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

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    if (!device || !pCreateInfo || !pImage ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO ||
        !pCreateInfo->extent.width || !pCreateInfo->extent.height ||
        !pCreateInfo->extent.depth || pCreateInfo->mipLevels != 1 ||
        !pCreateInfo->arrayLayers || pCreateInfo->samples != VK_SAMPLE_COUNT_1_BIT ||
        !format_bytes(pCreateInfo->format) ||
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
    uint64_t texels = (uint64_t)image->extent.width * image->extent.height *
        image->extent.depth * image->array_layers;
    if (texels > UINT64_MAX / format_bytes(image->format)) {
        vk_ps5_device_free(device, pAllocator, image);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    image->size = (texels * format_bytes(image->format) + 255u) & ~(VkDeviceSize)255u;
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
    pMemoryRequirements->alignment = 256;
    pMemoryRequirements->memoryTypeBits = 0x3;
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
    if (!image || !memory || memoryOffset % 256 != 0 ||
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
    uint32_t bytes = format_bytes(image->format);
    VkDeviceSize row_pitch = (VkDeviceSize)image->extent.width * bytes;
    VkDeviceSize depth_pitch = row_pitch * image->extent.height;
    pLayout->offset = depth_pitch * image->extent.depth * pSubresource->arrayLayer;
    pLayout->size = depth_pitch * image->extent.depth;
    pLayout->rowPitch = row_pitch;
    pLayout->arrayPitch = depth_pitch * image->extent.depth;
    pLayout->depthPitch = depth_pitch;
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
    for (VkPs5CommandBuffer *command = pool->buffers; command; command = command->next)
        command->state = VK_PS5_COMMAND_INITIAL;
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
        VkResult result = vk_ps5_set_device_loader_data(device, command);
        if (result != VK_SUCCESS) {
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
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING)
        return VK_ERROR_INITIALIZATION_FAILED;
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
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
    if (!device || !pCreateInfo || !pView ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO ||
        !pCreateInfo->image || !format_bytes(pCreateInfo->format))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkPs5ImageView *view = alloc_object(device, pAllocator, sizeof(*view),
                                        _Alignof(VkPs5ImageView));
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->image = pCreateInfo->image;
    view->format = pCreateInfo->format;
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
    size_t size = sizeof(VkPs5QueryPool) +
        (size_t)pCreateInfo->queryCount * sizeof(uint64_t);
    VkPs5QueryPool *pool = alloc_object(device, pAllocator, size,
                                        _Alignof(VkPs5QueryPool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = pCreateInfo->queryType;
    pool->count = pCreateInfo->queryCount;
    *pQueryPool = (VkQueryPool)pool;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                   const VkAllocationCallbacks *pAllocator) {
    if (queryPool) vk_ps5_device_free(device, pAllocator, (void *)queryPool);
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
    for (uint32_t i = 0; i < queryCount; ++i) {
        uint8_t *dst = (uint8_t *)pData + (size_t)i * (size_t)stride;
        memset(dst, 0, entry_size);
    }
    return (flags & VK_QUERY_RESULT_PARTIAL_BIT) ? VK_SUCCESS : VK_NOT_READY;
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

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                         uint32_t createInfoCount,
                         const VkComputePipelineCreateInfo *pCreateInfos,
                         const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    (void)device; (void)pipelineCache; (void)pCreateInfos; (void)pAllocator;
    if (pPipelines)
        for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = VK_NULL_HANDLE;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                          uint32_t createInfoCount,
                          const VkGraphicsPipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    (void)device; (void)pipelineCache; (void)pCreateInfos; (void)pAllocator;
    if (pPipelines)
        for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = VK_NULL_HANDLE;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                  const VkAllocationCallbacks *pAllocator) {
    if (pipeline) vk_ps5_device_free(device, pAllocator, (void *)pipeline);
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

DEFINE_SIMPLE_CREATE(vkCreatePipelineLayout, VkPipelineLayoutCreateInfo, VkPipelineLayout)
DEFINE_SIMPLE_DESTROY(vkDestroyPipelineLayout, VkPipelineLayout)
DEFINE_SIMPLE_CREATE(vkCreateSampler, VkSamplerCreateInfo, VkSampler)
DEFINE_SIMPLE_DESTROY(vkDestroySampler, VkSampler)
DEFINE_SIMPLE_CREATE(vkCreateDescriptorSetLayout, VkDescriptorSetLayoutCreateInfo,
                     VkDescriptorSetLayout)
DEFINE_SIMPLE_DESTROY(vkDestroyDescriptorSetLayout, VkDescriptorSetLayout)
DEFINE_SIMPLE_CREATE(vkCreateFramebuffer, VkFramebufferCreateInfo, VkFramebuffer)
DEFINE_SIMPLE_DESTROY(vkDestroyFramebuffer, VkFramebuffer)
DEFINE_SIMPLE_DESTROY(vkDestroyRenderPass, VkRenderPass)
DEFINE_SIMPLE_CREATE(vkCreateDescriptorUpdateTemplate, VkDescriptorUpdateTemplateCreateInfo,
                     VkDescriptorUpdateTemplate)
DEFINE_SIMPLE_DESTROY(vkDestroyDescriptorUpdateTemplate, VkDescriptorUpdateTemplate)

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
    VkPs5Opaque *object = alloc_object(device, pAllocator, sizeof(*object),
                                        _Alignof(VkPs5Opaque));
    if (!object) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *pRenderPass = (VkRenderPass)object;
    return VK_SUCCESS;
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
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)pAllocateInfo->descriptorPool;
    if (!pool || pAllocateInfo->descriptorSetCount >
        pool->max_sets - pool->allocated_sets) return VK_ERROR_OUT_OF_POOL_MEMORY;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i)
        pDescriptorSets[i] = VK_NULL_HANDLE;
    VkPs5DescriptorSet *old_head = pool->sets;
    uint32_t old_count = pool->allocated_sets;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        VkPs5DescriptorSet *set = alloc_object(device, NULL, sizeof(*set),
                                               _Alignof(VkPs5DescriptorSet));
        if (!set) {
            while (pool->sets != old_head) {
                VkPs5DescriptorSet *rollback = pool->sets;
                pool->sets = rollback->next;
                vk_ps5_device_free(device, NULL, rollback);
            }
            pool->allocated_sets = old_count;
            for (uint32_t j = 0; j < pAllocateInfo->descriptorSetCount; ++j)
                pDescriptorSets[j] = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        set->pool = pool;
        set->next = pool->sets;
        pool->sets = set;
        pool->allocated_sets++;
        pDescriptorSets[i] = (VkDescriptorSet)set;
    }
    return VK_SUCCESS;
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
        vk_ps5_device_free(device, NULL, set);
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                       const VkWriteDescriptorSet *pDescriptorWrites,
                       uint32_t descriptorCopyCount,
                       const VkCopyDescriptorSet *pDescriptorCopies) {
    (void)device; (void)descriptorWriteCount; (void)pDescriptorWrites;
    (void)descriptorCopyCount; (void)pDescriptorCopies;
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
                const VkBufferCopy *r) { IGNORE(c); IGNORE(s); IGNORE(d); IGNORE(n); IGNORE(r); }
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
    IGNORE(c); IGNORE(p); IGNORE(q); IGNORE(f);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndQuery(VkCommandBuffer c, VkQueryPool p, uint32_t q) {
    IGNORE(c); IGNORE(p); IGNORE(q);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResetQueryPool(VkCommandBuffer c, VkQueryPool p, uint32_t f, uint32_t n) {
    IGNORE(c); IGNORE(p); IGNORE(f); IGNORE(n);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdWriteTimestamp(VkCommandBuffer c, VkPipelineStageFlagBits s, VkQueryPool p,
                    uint32_t q) { IGNORE(c); IGNORE(s); IGNORE(p); IGNORE(q); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyQueryPoolResults(VkCommandBuffer c, VkQueryPool p, uint32_t f, uint32_t n,
                          VkBuffer d, VkDeviceSize o, VkDeviceSize s,
                          VkQueryResultFlags flags) {
    IGNORE(c); IGNORE(p); IGNORE(f); IGNORE(n); IGNORE(d); IGNORE(o); IGNORE(s); IGNORE(flags);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdExecuteCommands(VkCommandBuffer c, uint32_t n, const VkCommandBuffer *p) {
    IGNORE(c); IGNORE(n); IGNORE(p);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) {
    IGNORE(c); IGNORE(b); IGNORE(p);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint b, VkPipelineLayout l,
                        uint32_t f, uint32_t n, const VkDescriptorSet *s,
                        uint32_t dn, const uint32_t *d) {
    IGNORE(c); IGNORE(b); IGNORE(l); IGNORE(f); IGNORE(n); IGNORE(s); IGNORE(dn); IGNORE(d);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(VkCommandBuffer c, VkImage i, VkImageLayout l,
                     const VkClearColorValue *v, uint32_t n,
                     const VkImageSubresourceRange *r) {
    IGNORE(c); IGNORE(i); IGNORE(l); IGNORE(v); IGNORE(n); IGNORE(r);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z) {
    IGNORE(c); IGNORE(x); IGNORE(y); IGNORE(z);
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
vkCmdSetLineWidth(VkCommandBuffer c, float w) { IGNORE(c); IGNORE(w); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDepthBias(VkCommandBuffer c, float a, float b, float d) {
    IGNORE(c); IGNORE(a); IGNORE(b); IGNORE(d);
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
    IGNORE(c); IGNORE(b); IGNORE(o); IGNORE(t);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t f, uint32_t n,
                       const VkBuffer *b, const VkDeviceSize *o) {
    IGNORE(c); IGNORE(f); IGNORE(n); IGNORE(b); IGNORE(o);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) {
    IGNORE(c); IGNORE(v); IGNORE(i); IGNORE(fv); IGNORE(fi);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexed(VkCommandBuffer c, uint32_t i, uint32_t n, uint32_t f,
                 int32_t v, uint32_t fi) { IGNORE(c); IGNORE(i); IGNORE(n); IGNORE(f); IGNORE(v); IGNORE(fi); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, uint32_t n, uint32_t s) {
    IGNORE(c); IGNORE(b); IGNORE(o); IGNORE(n); IGNORE(s);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexedIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o,
                         uint32_t n, uint32_t s) { IGNORE(c); IGNORE(b); IGNORE(o); IGNORE(n); IGNORE(s); }
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
                     VkSubpassContents s) { IGNORE(c); IGNORE(b); IGNORE(s); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdNextSubpass(VkCommandBuffer c, VkSubpassContents s) { IGNORE(c); IGNORE(s); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndRenderPass(VkCommandBuffer c) { IGNORE(c); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDeviceMask(VkCommandBuffer c, uint32_t m) { IGNORE(c); IGNORE(m); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatchBase(VkCommandBuffer c, uint32_t bx, uint32_t by, uint32_t bz,
                  uint32_t x, uint32_t y, uint32_t z) {
    IGNORE(c); IGNORE(bx); IGNORE(by); IGNORE(bz); IGNORE(x); IGNORE(y); IGNORE(z);
}
