#include "vulkan_ps5_internal.h"

#include <agc_error.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define VK_PS5_SWAPCHAIN_IMAGE_COUNT 3u

typedef struct VkPs5Surface {
    VkInstance instance;
    atomic_flag lock;
    uintptr_t active_swapchain;
} VkPs5Surface;

typedef struct VkPs5Swapchain {
    VkDevice device;
    VkPs5Surface *surface;
    AgcPresentChain present_chain;
    VkImage images[VK_PS5_SWAPCHAIN_IMAGE_COUNT];
    VkDeviceMemory memories[VK_PS5_SWAPCHAIN_IMAGE_COUNT];
    bool acquired[VK_PS5_SWAPCHAIN_IMAGE_COUNT];
    pthread_mutex_t lock;
    pthread_cond_t available;
    bool lock_initialized;
    bool available_initialized;
    uint32_t next_image;
    uint64_t frame_id;
    VkBool32 retired;
} VkPs5Swapchain;

static VkPs5Surface *surface_from_handle(VkSurfaceKHR handle) {
    return (VkPs5Surface *)(uintptr_t)handle;
}

static VkPs5Swapchain *swapchain_from_handle(VkSwapchainKHR handle) {
    return (VkPs5Swapchain *)(uintptr_t)handle;
}

static void lock_flag(atomic_flag *lock) {
    while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {}
}

static void unlock_flag(atomic_flag *lock) {
    atomic_flag_clear_explicit(lock, memory_order_release);
}

static bool realtime_deadline(uint64_t timeout, struct timespec *deadline) {
    if (!deadline || clock_gettime(CLOCK_REALTIME, deadline) != 0)
        return false;
    uint64_t seconds = timeout / UINT64_C(1000000000);
    uint64_t nanoseconds = timeout % UINT64_C(1000000000);
    if (seconds > (uint64_t)INT64_MAX - (uint64_t)deadline->tv_sec)
        return false;
    deadline->tv_sec += (time_t)seconds;
    deadline->tv_nsec += (long)nanoseconds;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= 1000000000L;
    }
    return true;
}

static VkResult enumerate_surface_formats(uint32_t *count,
                                           VkSurfaceFormatKHR *formats) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    const VkSurfaceFormatKHR format = {
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };
    if (!formats) {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0) return VK_INCOMPLETE;
    formats[0] = format;
    *count = 1;
    return VK_SUCCESS;
}

static VkResult enumerate_present_modes(uint32_t *count,
                                         VkPresentModeKHR *modes) {
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    if (!modes) {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count == 0) return VK_INCOMPLETE;
    modes[0] = VK_PRESENT_MODE_FIFO_KHR;
    *count = 1;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateHeadlessSurfaceEXT(VkInstance instance,
    const VkHeadlessSurfaceCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
    if (!instance || !pCreateInfo || !pSurface ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT ||
        pCreateInfo->flags != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Surface *surface = vk_ps5_instance_alloc(instance, pAllocator,
        sizeof(*surface), _Alignof(VkPs5Surface),
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (!surface) return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(surface, 0, sizeof(*surface));
    surface->instance = instance;
    atomic_flag_clear(&surface->lock);
    *pSurface = (VkSurfaceKHR)(uintptr_t)surface;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface_handle,
                    const VkAllocationCallbacks *pAllocator) {
    VkPs5Surface *surface = surface_from_handle(surface_handle);
    if (surface)
        vk_ps5_instance_free(instance ? instance : surface->instance,
                             pAllocator, surface);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported) {
    if (!physicalDevice || !surface || !pSupported)
        return VK_ERROR_SURFACE_LOST_KHR;
    *pSupported = queueFamilyIndex == 0 ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
    if (!physicalDevice || !surface || !pSurfaceCapabilities)
        return VK_ERROR_SURFACE_LOST_KHR;
    memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
    pSurfaceCapabilities->minImageCount = VK_PS5_SWAPCHAIN_IMAGE_COUNT;
    pSurfaceCapabilities->maxImageCount = VK_PS5_SWAPCHAIN_IMAGE_COUNT;
    pSurfaceCapabilities->currentExtent = (VkExtent2D){1920, 1080};
    pSurfaceCapabilities->minImageExtent = pSurfaceCapabilities->currentExtent;
    pSurfaceCapabilities->maxImageExtent = pSurfaceCapabilities->currentExtent;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms =
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount,
    VkSurfaceFormatKHR *pSurfaceFormats) {
    if (!physicalDevice || !surface) return VK_ERROR_SURFACE_LOST_KHR;
    return enumerate_surface_formats(pSurfaceFormatCount, pSurfaceFormats);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface, uint32_t *pPresentModeCount,
    VkPresentModeKHR *pPresentModes) {
    if (!physicalDevice || !surface) return VK_ERROR_SURFACE_LOST_KHR;
    return enumerate_present_modes(pPresentModeCount, pPresentModes);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface, uint32_t *pRectCount, VkRect2D *pRects) {
    if (!physicalDevice || !surface || !pRectCount)
        return VK_ERROR_SURFACE_LOST_KHR;
    if (!pRects) {
        *pRectCount = 1;
        return VK_SUCCESS;
    }
    if (*pRectCount == 0) return VK_INCOMPLETE;
    pRects[0] = (VkRect2D){{0, 0}, {1920, 1080}};
    *pRectCount = 1;
    return VK_SUCCESS;
}

static void destroy_swapchain_storage(VkPs5Swapchain *swapchain,
                                      const VkAllocationCallbacks *allocator) {
    if (!swapchain) return;
    if (swapchain->present_chain)
        (void)agcDestroyPresentChain(swapchain->present_chain);
    swapchain->present_chain = NULL;
    for (uint32_t i = 0; i < VK_PS5_SWAPCHAIN_IMAGE_COUNT; ++i) {
        if (swapchain->images[i])
            vkDestroyImage(swapchain->device, swapchain->images[i], allocator);
        if (swapchain->memories[i])
            vkFreeMemory(swapchain->device, swapchain->memories[i], allocator);
    }
    if (swapchain->available_initialized)
        (void)pthread_cond_destroy(&swapchain->available);
    if (swapchain->lock_initialized)
        (void)pthread_mutex_destroy(&swapchain->lock);
}

static VkResult validate_swapchain_create_info(
    const VkSwapchainCreateInfoKHR *info) {
    const VkImageUsageFlags supported_usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!info || info->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR ||
        !info->surface ||
        (info->flags & ~VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) ||
        info->minImageCount > VK_PS5_SWAPCHAIN_IMAGE_COUNT ||
        info->minImageCount == 0 || info->imageFormat != VK_FORMAT_B8G8R8A8_SRGB ||
        info->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
        info->imageExtent.width != 1920 || info->imageExtent.height != 1080 ||
        info->imageArrayLayers != 1 || !info->imageUsage ||
        (info->imageUsage & ~supported_usage) ||
        info->imageSharingMode != VK_SHARING_MODE_EXCLUSIVE ||
        info->preTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ||
        info->compositeAlpha != VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ||
        info->presentMode != VK_PRESENT_MODE_FIFO_KHR)
        return VK_ERROR_INITIALIZATION_FAILED;
    const VkImageFormatListCreateInfo *format_list = NULL;
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)info->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
            format_list = (const VkImageFormatListCreateInfo *)next;
    }
    if (info->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) {
        if (!format_list || !format_list->viewFormatCount ||
            !format_list->pViewFormats)
            return VK_ERROR_INITIALIZATION_FAILED;
        VkBool32 includes_surface_format = VK_FALSE;
        for (uint32_t i = 0; i < format_list->viewFormatCount; ++i) {
            VkFormat format = format_list->pViewFormats[i];
            if (format != VK_FORMAT_B8G8R8A8_SRGB &&
                format != VK_FORMAT_B8G8R8A8_UNORM)
                return VK_ERROR_FORMAT_NOT_SUPPORTED;
            includes_surface_format |= format == info->imageFormat;
        }
        if (!includes_surface_format) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    } else if (format_list) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
    if (!device || !pSwapchain) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = validate_swapchain_create_info(pCreateInfo);
    if (result != VK_SUCCESS) return result;

    VkPs5Surface *surface = surface_from_handle(pCreateInfo->surface);
    VkPs5Swapchain *old_swapchain = swapchain_from_handle(pCreateInfo->oldSwapchain);
    lock_flag(&surface->lock);
    if (surface->active_swapchain &&
        surface->active_swapchain != (uintptr_t)old_swapchain) {
        unlock_flag(&surface->lock);
        return VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    }
    if (old_swapchain) {
        (void)pthread_mutex_lock(&old_swapchain->lock);
        old_swapchain->retired = VK_TRUE;
        (void)pthread_cond_broadcast(&old_swapchain->available);
        (void)pthread_mutex_unlock(&old_swapchain->lock);
        surface->active_swapchain = 0;
    }

    VkPs5Swapchain *swapchain = vk_ps5_device_alloc(device, pAllocator,
        sizeof(*swapchain), _Alignof(VkPs5Swapchain),
        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (!swapchain) {
        unlock_flag(&surface->lock);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(swapchain, 0, sizeof(*swapchain));
    swapchain->device = device;
    swapchain->surface = surface;
    if (pthread_mutex_init(&swapchain->lock, NULL) != 0) {
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto fail;
    }
    swapchain->lock_initialized = true;
    if (pthread_cond_init(&swapchain->available, NULL) != 0) {
        result = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto fail;
    }
    swapchain->available_initialized = true;

    AgcImage native_images[VK_PS5_SWAPCHAIN_IMAGE_COUNT] = {NULL};
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = pCreateInfo->pNext,
        .flags = (pCreateInfo->flags &
                  VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) ?
            VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = pCreateInfo->imageFormat,
        .extent = {pCreateInfo->imageExtent.width,
                   pCreateInfo->imageExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = pCreateInfo->imageUsage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    for (uint32_t i = 0; i < VK_PS5_SWAPCHAIN_IMAGE_COUNT; ++i) {
        result = vkCreateImage(device, &image_info, pAllocator,
                               &swapchain->images[i]);
        if (result != VK_SUCCESS) goto fail;
        result = vk_ps5_enable_image_scanout(swapchain->images[i]);
        if (result != VK_SUCCESS) goto fail;
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, swapchain->images[i], &requirements);
        const VkMemoryDedicatedAllocateInfo dedicated_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .image = swapchain->images[i],
        };
        const VkMemoryAllocateInfo allocation_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &dedicated_info,
            .allocationSize = requirements.size,
            .memoryTypeIndex = 1,
        };
        result = vkAllocateMemory(device, &allocation_info, pAllocator,
                                  &swapchain->memories[i]);
        if (result != VK_SUCCESS) goto fail;
        result = vkBindImageMemory(device, swapchain->images[i],
                                   swapchain->memories[i], 0);
        if (result != VK_SUCCESS) goto fail;
        void *mapped = NULL;
        result = vkMapMemory(device, swapchain->memories[i], 0,
                             VK_WHOLE_SIZE, 0, &mapped);
        if (result != VK_SUCCESS) goto fail;
        memset(mapped, 0, (size_t)requirements.size);
        const VkMappedMemoryRange flush_range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = swapchain->memories[i],
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        result = vkFlushMappedMemoryRanges(device, 1, &flush_range);
        if (result != VK_SUCCESS) goto fail;
        native_images[i] = vk_ps5_native_image(swapchain->images[i]);
        if (!native_images[i]) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            goto fail;
        }
    }

    AgcPresentChainDesc present_desc = AGC_PRESENT_CHAIN_DESC_INIT;
    present_desc.image_count = VK_PS5_SWAPCHAIN_IMAGE_COUNT;
    present_desc.images = native_images;
    if (agcCreatePresentChain(vk_ps5_native_device(device), &present_desc,
            &swapchain->present_chain) != AGC_OK) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        goto fail;
    }
    result = vk_ps5_device_initialize_present_images(device,
        VK_PS5_SWAPCHAIN_IMAGE_COUNT, native_images);
    if (result != VK_SUCCESS) goto fail;
    for (uint32_t i = 0; i < VK_PS5_SWAPCHAIN_IMAGE_COUNT; ++i)
        vk_ps5_set_image_native_usage(swapchain->images[i],
            kAgcResourceUsageVideoOutScanout);

    surface->active_swapchain = (uintptr_t)swapchain;
    *pSwapchain = (VkSwapchainKHR)(uintptr_t)swapchain;
    unlock_flag(&surface->lock);
    return VK_SUCCESS;

fail:
    destroy_swapchain_storage(swapchain, pAllocator);
    vk_ps5_device_free(device, pAllocator, swapchain);
    unlock_flag(&surface->lock);
    return result;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain_handle,
                      const VkAllocationCallbacks *pAllocator) {
    VkPs5Swapchain *swapchain = swapchain_from_handle(swapchain_handle);
    if (!swapchain) return;
    lock_flag(&swapchain->surface->lock);
    if (swapchain->surface->active_swapchain == (uintptr_t)swapchain)
        swapchain->surface->active_swapchain = 0;
    unlock_flag(&swapchain->surface->lock);
    destroy_swapchain_storage(swapchain, pAllocator);
    vk_ps5_device_free(device, pAllocator, swapchain);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain_handle,
    uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages) {
    (void)device;
    VkPs5Swapchain *swapchain = swapchain_from_handle(swapchain_handle);
    if (!swapchain || !pSwapchainImageCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!pSwapchainImages) {
        *pSwapchainImageCount = VK_PS5_SWAPCHAIN_IMAGE_COUNT;
        return VK_SUCCESS;
    }
    uint32_t written = *pSwapchainImageCount < VK_PS5_SWAPCHAIN_IMAGE_COUNT
        ? *pSwapchainImageCount : VK_PS5_SWAPCHAIN_IMAGE_COUNT;
    if (written)
        memcpy(pSwapchainImages, swapchain->images, written * sizeof(VkImage));
    *pSwapchainImageCount = written;
    return written < VK_PS5_SWAPCHAIN_IMAGE_COUNT ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult acquire_next_image(VkPs5Swapchain *swapchain, uint64_t timeout,
                                   VkSemaphore semaphore, VkFence fence,
                                   uint32_t *image_index) {
    if (!swapchain || !image_index || (!semaphore && !fence))
        return VK_ERROR_INITIALIZATION_FAILED;
    struct timespec deadline = {0};
    if (timeout != 0 && timeout != UINT64_MAX &&
        !realtime_deadline(timeout, &deadline))
        return VK_ERROR_DEVICE_LOST;
    if (pthread_mutex_lock(&swapchain->lock) != 0)
        return VK_ERROR_DEVICE_LOST;
    for (;;) {
        if (swapchain->retired) {
            (void)pthread_mutex_unlock(&swapchain->lock);
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        for (uint32_t offset = 0; offset < VK_PS5_SWAPCHAIN_IMAGE_COUNT; ++offset) {
            uint32_t index = (swapchain->next_image + offset) %
                VK_PS5_SWAPCHAIN_IMAGE_COUNT;
            if (!swapchain->acquired[index]) {
                swapchain->acquired[index] = true;
                swapchain->next_image = (index + 1) % VK_PS5_SWAPCHAIN_IMAGE_COUNT;
                VkResult result = vk_ps5_signal_acquire(semaphore, fence);
                if (result == VK_SUCCESS)
                    *image_index = index;
                else
                    swapchain->acquired[index] = false;
                (void)pthread_mutex_unlock(&swapchain->lock);
                return result;
            }
        }
        if (timeout == 0) {
            (void)pthread_mutex_unlock(&swapchain->lock);
            return VK_NOT_READY;
        }
        int wait_result = timeout == UINT64_MAX ?
            pthread_cond_wait(&swapchain->available, &swapchain->lock) :
            pthread_cond_timedwait(&swapchain->available, &swapchain->lock,
                &deadline);
        if (wait_result == ETIMEDOUT) {
            (void)pthread_mutex_unlock(&swapchain->lock);
            return VK_TIMEOUT;
        }
        if (wait_result != 0) {
            (void)pthread_mutex_unlock(&swapchain->lock);
            return VK_ERROR_DEVICE_LOST;
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
    uint64_t timeout, VkSemaphore semaphore, VkFence fence,
    uint32_t *pImageIndex) {
    (void)device;
    return acquire_next_image(swapchain_from_handle(swapchain), timeout,
                              semaphore, fence, pImageIndex);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo,
                       uint32_t *pImageIndex) {
    if (!device || !pAcquireInfo ||
        pAcquireInfo->sType != VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR ||
        pAcquireInfo->deviceMask != 1)
        return VK_ERROR_INITIALIZATION_FAILED;
    return acquire_next_image(swapchain_from_handle(pAcquireInfo->swapchain),
        pAcquireInfo->timeout, pAcquireInfo->semaphore, pAcquireInfo->fence,
        pImageIndex);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    if (!queue || !pPresentInfo ||
        pPresentInfo->sType != VK_STRUCTURE_TYPE_PRESENT_INFO_KHR ||
        (pPresentInfo->swapchainCount &&
         (!pPresentInfo->pSwapchains || !pPresentInfo->pImageIndices)))
        return VK_ERROR_INITIALIZATION_FAILED;
    const VkPresentRegionsKHR *regions = NULL;
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)pPresentInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR)
            regions = (const VkPresentRegionsKHR *)next;
    }
    if (regions) {
        if (regions->swapchainCount != pPresentInfo->swapchainCount ||
            (regions->swapchainCount && !regions->pRegions))
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t i = 0; i < regions->swapchainCount; ++i) {
            const VkPresentRegionKHR *region = &regions->pRegions[i];
            if (region->rectangleCount && !region->pRectangles)
                return VK_ERROR_INITIALIZATION_FAILED;
            for (uint32_t j = 0; j < region->rectangleCount; ++j) {
                const VkRectLayerKHR *rectangle = &region->pRectangles[j];
                if (rectangle->layer != 0 || rectangle->offset.x < 0 ||
                    rectangle->offset.y < 0 || !rectangle->extent.width ||
                    !rectangle->extent.height ||
                    (uint32_t)rectangle->offset.x > 1920u ||
                    rectangle->extent.width >
                        1920u - (uint32_t)rectangle->offset.x ||
                    (uint32_t)rectangle->offset.y > 1080u ||
                    rectangle->extent.height >
                        1080u - (uint32_t)rectangle->offset.y)
                    return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
    }
    VkResult result = vk_ps5_consume_semaphores(
        pPresentInfo->waitSemaphoreCount, pPresentInfo->pWaitSemaphores);
    if (result != VK_SUCCESS) return result;

    VkResult overall = VK_SUCCESS;
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        VkPs5Swapchain *swapchain =
            swapchain_from_handle(pPresentInfo->pSwapchains[i]);
        uint32_t index = pPresentInfo->pImageIndices[i];
        VkResult item_result = VK_SUCCESS;
        if (!swapchain || index >= VK_PS5_SWAPCHAIN_IMAGE_COUNT) {
            item_result = VK_ERROR_INITIALIZATION_FAILED;
        } else {
            if (pthread_mutex_lock(&swapchain->lock) != 0) {
                item_result = VK_ERROR_DEVICE_LOST;
                goto item_done;
            }
            uint64_t frame_id = swapchain->frame_id++;
            if (!swapchain->acquired[index]) {
                item_result = VK_ERROR_INITIALIZATION_FAILED;
            } else {
                (void)pthread_mutex_unlock(&swapchain->lock);
                item_result = vk_ps5_queue_present_native(queue,
                    swapchain->present_chain, index, frame_id,
                    (uint64_t)VK_PS5_PRESENT_TIMEOUT_US * UINT64_C(1000));
                if (pthread_mutex_lock(&swapchain->lock) != 0) {
                    item_result = VK_ERROR_DEVICE_LOST;
                    goto item_done;
                }
            }
            if (item_result == VK_SUCCESS) {
                swapchain->acquired[index] = false;
                (void)pthread_cond_broadcast(&swapchain->available);
            }
            (void)pthread_mutex_unlock(&swapchain->lock);
        }
item_done:
        if (pPresentInfo->pResults) pPresentInfo->pResults[i] = item_result;
        if (overall == VK_SUCCESS && item_result != VK_SUCCESS)
            overall = item_result;
    }
    return overall;
}

VkBool32 vk_ps5_swapchain_has_native_present_chain(
    VkSwapchainKHR swapchain_handle)
{
    VkPs5Swapchain *swapchain = swapchain_from_handle(swapchain_handle);
    return swapchain && swapchain->present_chain ? VK_TRUE : VK_FALSE;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetDeviceGroupPresentCapabilitiesKHR(VkDevice device,
    VkDeviceGroupPresentCapabilitiesKHR *pDeviceGroupPresentCapabilities) {
    if (!device || !pDeviceGroupPresentCapabilities ||
        pDeviceGroupPresentCapabilities->sType !=
            VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_CAPABILITIES_KHR)
        return VK_ERROR_INITIALIZATION_FAILED;
    memset(pDeviceGroupPresentCapabilities->presentMask, 0,
           sizeof(pDeviceGroupPresentCapabilities->presentMask));
    pDeviceGroupPresentCapabilities->presentMask[0] = 1;
    pDeviceGroupPresentCapabilities->modes =
        VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface,
    VkDeviceGroupPresentModeFlagsKHR *pModes) {
    if (!device || !surface || !pModes) return VK_ERROR_SURFACE_LOST_KHR;
    *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;
    return VK_SUCCESS;
}
