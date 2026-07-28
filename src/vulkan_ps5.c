#include "vulkan_ps5_internal.h"

#include "agc_capabilities.h"
#include "agc_cb.h"
#include "agc_error.h"
#include "agc_graphics.h"
#include "agc_memory.h"
#include "agcdriver.h"

#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define VK_PS5_API_VERSION VK_MAKE_API_VERSION(0, 1, 1, 0)
#define VK_PS5_VENDOR_ID 0x1002u
#define VK_PS5_DEVICE_ID 0x163fu

typedef struct VkPs5Instance VkPs5Instance;
typedef struct VkPs5PhysicalDevice VkPs5PhysicalDevice;
typedef struct VkPs5Device VkPs5Device;
typedef struct VkPs5Queue VkPs5Queue;
typedef struct VkPs5Memory VkPs5Memory;

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
    AgcGpuMemory submit_memory;
    atomic_uint_fast32_t next_submission;
    atomic_flag submit_lock;
};

struct VkPs5Device {
    VK_LOADER_DATA loader_data;
    VkAllocationCallbacks allocator;
    VkBool32 has_allocator;
    VkPs5PhysicalDevice *physical_device;
    VkPs5Queue queue;
    AgcGpuMemory tess_offchip_memory;
    AgcGpuMemory tess_factor_memory;
    AgcGpuMemory tess_ring_table_memory;
    AgcGfx1013TessellationState tessellation;
    atomic_flag tessellation_lock;
    VkBool32 tessellation_ready;
    VkBool32 robust_buffer_access;
    atomic_uint memory_allocation_count;
};

struct VkPs5Memory {
    AgcGpuMemory gpu_memory;
    void *data;
    VkDeviceSize size;
    uint32_t memory_type_index;
};

static AgcGfx1013Capabilities ps5_capabilities(void) {
    AgcGfx1013Capabilities capabilities;
    int32_t result = agcGfx1013GetCapabilities(&capabilities);
    if (result != AGC_OK) memset(&capabilities, 0, sizeof(capabilities));
    return capabilities;
}

static VkMemoryPropertyFlags ps5_memory_flags(
    AgcGfx1013MemoryPropertyFlags flags) {
    VkMemoryPropertyFlags result = 0;
    if (flags & AGC_GFX1013_MEMORY_DEVICE_LOCAL_BIT)
        result |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (flags & AGC_GFX1013_MEMORY_HOST_VISIBLE_BIT)
        result |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (flags & AGC_GFX1013_MEMORY_HOST_COHERENT_BIT)
        result |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (flags & AGC_GFX1013_MEMORY_HOST_CACHED_BIT)
        result |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    return result;
}

static int ps5_color_format(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: return AGC_GFX1013_RT_FORMAT_R8_UNORM;
    case VK_FORMAT_R8G8_UNORM: return AGC_GFX1013_RT_FORMAT_RG8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM: return AGC_GFX1013_RT_FORMAT_RGBA8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM: return AGC_GFX1013_RT_FORMAT_BGRA8_UNORM;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM;
    case VK_FORMAT_R16_SFLOAT: return AGC_GFX1013_RT_FORMAT_R16_FLOAT;
    case VK_FORMAT_R16G16_SFLOAT: return AGC_GFX1013_RT_FORMAT_RG16_FLOAT;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT;
    case VK_FORMAT_R32_SFLOAT: return AGC_GFX1013_RT_FORMAT_R32_FLOAT;
    case VK_FORMAT_R32G32_SFLOAT: return AGC_GFX1013_RT_FORMAT_RG32_FLOAT;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT;
    case VK_FORMAT_R8G8B8A8_SRGB: return AGC_GFX1013_RT_FORMAT_RGBA8_SRGB;
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

VkResult vk_ps5_queue_submit_dcb(
    VkQueue queue_handle, const uint32_t *commands, uint32_t dword_count) {
    VkPs5Queue *queue = (VkPs5Queue *)queue_handle;
    if (!queue || (!commands && dword_count) ||
        dword_count > VK_PS5_DCB_SIZE / sizeof(uint32_t) - 8u)
        return VK_ERROR_INITIALIZATION_FAILED;

    while (atomic_flag_test_and_set_explicit(
        &queue->submit_lock, memory_order_acquire)) {}
    VkResult result = VK_SUCCESS;
    uint32_t value = (uint32_t)atomic_fetch_add_explicit(
        &queue->next_submission, 1u, memory_order_relaxed) + 1u;
    volatile uint32_t *label = (volatile uint32_t *)
        ((uint8_t *)queue->submit_memory.cpu_address + VK_PS5_DCB_SIZE);
    *label = 0u;

    SceAgcCb cb;
    agcCbInit(&cb, queue->submit_memory.cpu_address, VK_PS5_DCB_SIZE);
    uint32_t *destination = agcCbAllocDwords(&cb, dword_count);
    if (dword_count && !destination) {
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        goto done;
    }
    if (dword_count) memcpy(destination, commands,
                           (size_t)dword_count * sizeof(uint32_t));
    const AgcGfx1013EopFenceState fence = {
        .address = queue->submit_memory.gpu_address + VK_PS5_DCB_SIZE,
        .value = value,
    };
    if (agcGfx1013SignalEopFence(&cb, &fence) != AGC_OK) {
        result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        goto done;
    }
    uint32_t used_dwords = agcCbUsedDwords(&cb);
    if (agcGpuMemoryFlush(&queue->submit_memory, 0,
            VK_PS5_DCB_SIZE + sizeof(uint32_t)) != AGC_OK) {
        result = VK_ERROR_DEVICE_LOST;
        goto done;
    }
    const AgcCommandBufferSubmit submit = {
        .command_address = (uintptr_t)queue->submit_memory.gpu_address,
        .dword_count = used_dwords,
    };
    if (sceAgcDriverSubmitDcb(&submit) != AGC_OK) {
        result = VK_ERROR_DEVICE_LOST;
        goto done;
    }
#if defined(OPENAGC_GENERIC)
    *label = value;
#endif
    if (agcGpuMemoryWait32(&queue->submit_memory, VK_PS5_DCB_SIZE,
            value, 5000000u) != AGC_OK)
        result = VK_ERROR_DEVICE_LOST;

done:
    atomic_flag_clear_explicit(&queue->submit_lock, memory_order_release);
    return result;
}

VkDeviceSize vk_ps5_memory_size(VkDeviceMemory memory_handle) {
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    return memory ? memory->size : 0;
}

uint64_t vk_ps5_memory_gpu_address(
    VkDeviceMemory memory_handle, VkDeviceSize offset) {
    VkPs5Memory *memory = (VkPs5Memory *)memory_handle;
    if (!memory || offset > memory->size) return 0;
    return memory->gpu_memory.gpu_address + offset;
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
    { VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
      VK_EXT_HOST_QUERY_RESET_SPEC_VERSION },
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
             VK_API_VERSION_MINOR(requested) > 1))
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
    AgcGfx1013Capabilities capabilities = ps5_capabilities();
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
    AgcGfx1013Capabilities capabilities = ps5_capabilities();
    (void)physicalDevice;
    if (!pMemoryProperties) return;
    memset(pMemoryProperties, 0, sizeof(*pMemoryProperties));
    pMemoryProperties->memoryTypeCount = capabilities.memory_profile_count;
    pMemoryProperties->memoryHeapCount = capabilities.memory_profile_count;
    for (uint32_t i = 0; i < capabilities.memory_profile_count; ++i) {
        pMemoryProperties->memoryTypes[i].propertyFlags =
            ps5_memory_flags(capabilities.memory_profiles[i].property_flags);
        pMemoryProperties->memoryTypes[i].heapIndex = i;
        pMemoryProperties->memoryHeaps[i].size = capabilities.memory_profiles[i].size;
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
    AgcGfx1013Capabilities capabilities = ps5_capabilities();
    int color_index = ps5_color_format(format);
    int depth_index = ps5_depth_format(format);
    (void)physicalDevice;
    if (!pFormatProperties) return;
    memset(pFormatProperties, 0, sizeof(*pFormatProperties));
    if ((color_index < 0 || !(capabilities.color_target_format_mask &
                              (1ull << (uint32_t)color_index))) &&
        (depth_index < 0 || !(capabilities.depth_stencil_format_mask &
                              (1u << (uint32_t)depth_index))))
        return;

    const VkFormatFeatureFlags transfer =
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const VkFormatFeatureFlags sampled =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const VkFormatFeatureFlags color = sampled | transfer |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    const VkFormatFeatureFlags storage_color = color |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    const VkFormatFeatureFlags depth = transfer |
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        pFormatProperties->linearTilingFeatures = storage_color;
        pFormatProperties->optimalTilingFeatures = color;
        pFormatProperties->bufferFeatures =
            VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT |
            VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
        break;
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
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
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
        pFormatProperties->linearTilingFeatures = color;
        pFormatProperties->optimalTilingFeatures = color;
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
    AgcGfx1013Capabilities capabilities = ps5_capabilities();
    (void)physicalDevice;
    if (!pImageFormatProperties) return VK_ERROR_INITIALIZATION_FAILED;
    memset(pImageFormatProperties, 0, sizeof(*pImageFormatProperties));
    if (type < VK_IMAGE_TYPE_1D || type > VK_IMAGE_TYPE_3D ||
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
    if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
        !(supported & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
        !(supported & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
        if (type != VK_IMAGE_TYPE_2D ||
            (format != VK_FORMAT_R8G8B8A8_UNORM &&
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
    pImageFormatProperties->maxMipLevels = 1;
    pImageFormatProperties->maxArrayLayers = type == VK_IMAGE_TYPE_3D ? 1 :
        capabilities.max_image_array_layers;
    pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    if (format == VK_FORMAT_R8G8B8A8_UNORM && type == VK_IMAGE_TYPE_2D &&
        tiling == VK_IMAGE_TILING_OPTIMAL &&
        (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        !(usage & ~(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)))
        pImageFormatProperties->sampleCounts |= VK_SAMPLE_COUNT_4_BIT;
    pImageFormatProperties->maxResourceSize = capabilities.memory_profiles[0].size;
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
    AgcGfx1013Capabilities capabilities = ps5_capabilities();
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
            maintenance->maxMemoryAllocationSize = capabilities.memory_profiles[1].size;
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
            v11->maxMemoryAllocationSize = capabilities.memory_profiles[1].size;
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
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT:
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES: {
            const VkPhysicalDeviceVertexAttributeDivisorFeatures *f =
                (const VkPhysicalDeviceVertexAttributeDivisorFeatures *)next;
            if (f->vertexAttributeInstanceRateZeroDivisor)
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
    device->queue.device = device;
    atomic_init(&device->memory_allocation_count, 0);
    atomic_init(&device->queue.next_submission, 0);
    atomic_flag_clear(&device->queue.submit_lock);
    atomic_flag_clear(&device->tessellation_lock);
    if (sce_agc_initialize() != AGC_OK ||
        sce_agc_initialize_internal_memory() != AGC_OK ||
        sceAgcDriverNotifyDefaultStates(0) != AGC_OK ||
        sceAgcDriverSetupAsyncGraphics(1) != AGC_OK ||
        agcGpuMemoryAllocateFlexible(&device->queue.submit_memory,
            VK_PS5_DCB_SIZE + sizeof(uint32_t), 256u,
            "vulkan_ps5_queue") != AGC_OK) {
        agcGpuMemoryFreeFlexible(&device->queue.submit_memory);
        ps5_free(pAllocator, device);
        return VK_ERROR_INITIALIZATION_FAILED;
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
    agcGpuMemoryFreeFlexible(&device->tess_ring_table_memory);
    agcGpuMemoryFreeFlexible(&device->tess_factor_memory);
    agcGpuMemoryFreeFlexible(&device->tess_offchip_memory);
    agcGpuMemoryFreeFlexible(&device->queue.submit_memory);
    ps5_free(allocator, device);
}

VkResult vk_ps5_device_prepare_tessellation(
    VkDevice device_handle, const AgcGfx1013TessellationState **state,
    uint64_t *ring_descriptor_address)
{
    VkPs5Device *device = (VkPs5Device *)device_handle;
    if (!device || !state || !ring_descriptor_address)
        return VK_ERROR_INITIALIZATION_FAILED;
    while (atomic_flag_test_and_set_explicit(
               &device->tessellation_lock, memory_order_acquire)) {}
    VkResult result = VK_SUCCESS;
    if (!device->tessellation_ready) {
        if (agcGpuMemoryAllocateFlexible(&device->tess_offchip_memory,
                AGC_GFX1013_TESS_OFFCHIP_RING_SIZE, 256u,
                "vulkan_ps5_tess_offchip") != AGC_OK ||
            agcGpuMemoryAllocateFlexible(&device->tess_factor_memory,
                AGC_GFX1013_TESS_FACTOR_RING_SIZE, 256u,
                "vulkan_ps5_tess_factor") != AGC_OK ||
            agcGpuMemoryAllocateFlexible(&device->tess_ring_table_memory,
                sizeof(AgcGfx1013TessellationRingTable), 256u,
                "vulkan_ps5_tess_table") != AGC_OK) {
            result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        } else {
            memset(device->tess_offchip_memory.cpu_address, 0,
                AGC_GFX1013_TESS_OFFCHIP_RING_SIZE);
            memset(device->tess_factor_memory.cpu_address, 0,
                AGC_GFX1013_TESS_FACTOR_RING_SIZE);
            device->tessellation = (AgcGfx1013TessellationState){
                .offchip_ring_address =
                    device->tess_offchip_memory.gpu_address,
                .factor_ring_address =
                    device->tess_factor_memory.gpu_address,
                .offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE,
                .factor_ring_size = AGC_GFX1013_TESS_FACTOR_RING_SIZE,
                .offchip_param = AGC_GFX1013_TESS_OFFCHIP_PARAM,
                .max_tess_level = 0x42800000u,
                .min_tess_level = 0u,
                .esgs_ring_itemsize = 1u,
                .distribution = 0xd8181e0cu,
                .tf_param = 0x61u,
            };
            if (agcGfx1013BuildTessellationRingTable(
                    device->tess_ring_table_memory.cpu_address,
                    &device->tessellation) != AGC_OK ||
                agcGpuMemoryFlush(&device->tess_offchip_memory, 0,
                    AGC_GFX1013_TESS_OFFCHIP_RING_SIZE) != AGC_OK ||
                agcGpuMemoryFlush(&device->tess_factor_memory, 0,
                    AGC_GFX1013_TESS_FACTOR_RING_SIZE) != AGC_OK ||
                agcGpuMemoryFlush(&device->tess_ring_table_memory, 0,
                    sizeof(AgcGfx1013TessellationRingTable)) != AGC_OK ||
                sceAgcDriverSetTFRing(
                    (uintptr_t)device->tess_factor_memory.cpu_address,
                    AGC_GFX1013_TESS_FACTOR_RING_SIZE) != AGC_OK) {
                result = VK_ERROR_INITIALIZATION_FAILED;
            } else {
                device->tessellation_ready = VK_TRUE;
            }
        }
        if (result != VK_SUCCESS) {
            agcGpuMemoryFreeFlexible(&device->tess_ring_table_memory);
            agcGpuMemoryFreeFlexible(&device->tess_factor_memory);
            agcGpuMemoryFreeFlexible(&device->tess_offchip_memory);
            memset(&device->tessellation, 0, sizeof(device->tessellation));
        }
    }
    if (result == VK_SUCCESS) {
        *state = &device->tessellation;
        *ring_descriptor_address =
            device->tess_ring_table_memory.gpu_address;
    }
    atomic_flag_clear_explicit(
        &device->tessellation_lock, memory_order_release);
    return result;
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
    AgcGfx1013Capabilities capabilities = ps5_capabilities();
    if (!device || !pAllocateInfo || !pMemory ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO ||
        pAllocateInfo->memoryTypeIndex >= capabilities.memory_profile_count ||
        pAllocateInfo->allocationSize == 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceSize heap_size =
        capabilities.memory_profiles[pAllocateInfo->memoryTypeIndex].size;
    if (pAllocateInfo->allocationSize > heap_size || pAllocateInfo->allocationSize > SIZE_MAX)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
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
    size_t alignment = (size_t)
        capabilities.memory_profiles[pAllocateInfo->memoryTypeIndex].minimum_alignment;
    int32_t memory_result = pAllocateInfo->memoryTypeIndex == 0u ?
        agcGpuMemoryAllocateFlexible(&memory->gpu_memory,
            (size_t)pAllocateInfo->allocationSize,
            alignment > 0x4000u ? 0x4000u : alignment,
            "vulkan_ps5_memory") :
        agcGpuMemoryAllocateDirectWriteCombined(&memory->gpu_memory,
            (size_t)pAllocateInfo->allocationSize, alignment);
    if (memory_result != AGC_OK) {
        ps5_free(allocator, memory);
        atomic_fetch_sub(&device->memory_allocation_count, 1);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    memory->data = memory->gpu_memory.cpu_address;
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
    if (memory->gpu_memory.type == AGC_GPU_MEMORY_TYPE_FLEXIBLE)
        agcGpuMemoryFreeFlexible(&memory->gpu_memory);
    else
        agcGpuMemoryFreeDirect(&memory->gpu_memory);
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
    *ppData = (uint8_t *)memory->data + offset;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    (void)device; (void)memory;
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
            agcGpuMemoryFlush(&memory->gpu_memory,
                (size_t)pMemoryRanges[i].offset, (size_t)size) != AGC_OK)
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
            agcGpuMemoryInvalidate(&memory->gpu_memory,
                (size_t)pMemoryRanges[i].offset, (size_t)size) != AGC_OK)
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
    ENTRY(vkCreateEvent), ENTRY(vkDestroyEvent), ENTRY(vkGetEventStatus),
    ENTRY(vkSetEvent), ENTRY(vkResetEvent),
    ENTRY(vkCreateBuffer), ENTRY(vkDestroyBuffer),
    ENTRY(vkCreateImage), ENTRY(vkDestroyImage),
    ENTRY(vkGetImageSubresourceLayout), ENTRY(vkCreateImageView),
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
    ENTRY(vkResetQueryPoolEXT),
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
    ENTRY(vkCmdBindIndexBuffer), ENTRY(vkCmdBindVertexBuffers), ENTRY(vkCmdDraw),
    ENTRY(vkCmdDrawIndexed), ENTRY(vkCmdDrawIndirect), ENTRY(vkCmdDrawIndexedIndirect),
    ENTRY(vkCmdBlitImage), ENTRY(vkCmdClearDepthStencilImage),
    ENTRY(vkCmdClearAttachments), ENTRY(vkCmdResolveImage),
    ENTRY(vkCmdBeginRenderPass), ENTRY(vkCmdNextSubpass), ENTRY(vkCmdEndRenderPass),
    ENTRY(vkCmdSetDeviceMask), ENTRY(vkCmdDispatchBase),
    ALIAS("vkCmdSetDeviceMaskKHR", vkCmdSetDeviceMask),
    ALIAS("vkCmdDispatchBaseKHR", vkCmdDispatchBase),
    ALIAS("vkCreateDescriptorUpdateTemplateKHR", vkCreateDescriptorUpdateTemplate),
    ALIAS("vkDestroyDescriptorUpdateTemplateKHR", vkDestroyDescriptorUpdateTemplate),
    ALIAS("vkUpdateDescriptorSetWithTemplateKHR", vkUpdateDescriptorSetWithTemplate),
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
