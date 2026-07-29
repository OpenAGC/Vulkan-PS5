#include "vulkan_ps5_internal.h"

#include <agc_error.h>
#include <agc_videoout.h>

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
    AgcVideoOut *video_out;
    VkImage images[VK_PS5_SWAPCHAIN_IMAGE_COUNT];
    VkDeviceMemory memories[VK_PS5_SWAPCHAIN_IMAGE_COUNT];
    atomic_bool acquired[VK_PS5_SWAPCHAIN_IMAGE_COUNT];
    atomic_flag lock;
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

static uint64_t monotonic_nanoseconds(void) {
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void wait_for_acquire_progress(uint64_t remaining_nanoseconds) {
    const uint64_t poll_nanoseconds = 100000ull;
    uint64_t delay = remaining_nanoseconds < poll_nanoseconds ?
        remaining_nanoseconds : poll_nanoseconds;
    const struct timespec sleep_time = {
        .tv_sec = 0,
        .tv_nsec = (long)delay,
    };
    nanosleep(&sleep_time, NULL);
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
    agcVideoOutClose(swapchain->video_out);
    swapchain->video_out = NULL;
    for (uint32_t i = 0; i < VK_PS5_SWAPCHAIN_IMAGE_COUNT; ++i) {
        if (swapchain->images[i])
            vkDestroyImage(swapchain->device, swapchain->images[i], allocator);
        if (swapchain->memories[i])
            vkFreeMemory(swapchain->device, swapchain->memories[i], allocator);
    }
}

static VkResult validate_swapchain_create_info(
    const VkSwapchainCreateInfoKHR *info) {
    const VkImageUsageFlags supported_usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!info || info->sType != VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR ||
        !info->surface || info->flags != 0 ||
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
        lock_flag(&old_swapchain->lock);
        old_swapchain->retired = VK_TRUE;
        unlock_flag(&old_swapchain->lock);
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
    atomic_flag_clear(&swapchain->lock);
    for (uint32_t i = 0; i < VK_PS5_SWAPCHAIN_IMAGE_COUNT; ++i)
        atomic_init(&swapchain->acquired[i], false);

    void *buffer_addresses[VK_PS5_SWAPCHAIN_IMAGE_COUNT] = {0};
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
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
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, swapchain->images[i], &requirements);
        const VkMemoryAllocateInfo allocation_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = 1,
        };
        result = vkAllocateMemory(device, &allocation_info, pAllocator,
                                  &swapchain->memories[i]);
        if (result != VK_SUCCESS) goto fail;
        result = vkBindImageMemory(device, swapchain->images[i],
                                   swapchain->memories[i], 0);
        if (result != VK_SUCCESS) goto fail;
        result = vkMapMemory(device, swapchain->memories[i], 0,
                             VK_WHOLE_SIZE, 0, &buffer_addresses[i]);
        if (result != VK_SUCCESS) goto fail;
        memset(buffer_addresses[i], 0, (size_t)requirements.size);
        const VkMappedMemoryRange flush_range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = swapchain->memories[i],
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        result = vkFlushMappedMemoryRanges(device, 1, &flush_range);
        if (result != VK_SUCCESS) goto fail;
    }

    const AgcVideoOutCreateInfo video_info = {
        .width = 1920,
        .height = 1080,
        .pitch_pixels = 1920,
        .buffer_count = VK_PS5_SWAPCHAIN_IMAGE_COUNT,
        .buffers = buffer_addresses,
        .format = AGC_VIDEO_OUT_FORMAT_BGRA8_SRGB,
    };
    if (agcVideoOutOpen(&video_info, &swapchain->video_out) != AGC_OK) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        goto fail;
    }

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
    const uint64_t start = monotonic_nanoseconds();
    if (start == 0) return VK_ERROR_DEVICE_LOST;
    for (;;) {
        lock_flag(&swapchain->lock);
        if (swapchain->retired) {
            unlock_flag(&swapchain->lock);
            return VK_ERROR_OUT_OF_DATE_KHR;
        }
        for (uint32_t offset = 0; offset < VK_PS5_SWAPCHAIN_IMAGE_COUNT; ++offset) {
            uint32_t index = (swapchain->next_image + offset) %
                VK_PS5_SWAPCHAIN_IMAGE_COUNT;
            if (!atomic_load(&swapchain->acquired[index])) {
                atomic_store(&swapchain->acquired[index], true);
                swapchain->next_image = (index + 1) % VK_PS5_SWAPCHAIN_IMAGE_COUNT;
                *image_index = index;
                VkResult result = vk_ps5_signal_acquire(semaphore, fence);
                unlock_flag(&swapchain->lock);
                return result;
            }
        }
        unlock_flag(&swapchain->lock);

        if (timeout == 0) return VK_NOT_READY;
        uint64_t now = monotonic_nanoseconds();
        if (now == 0) return VK_ERROR_DEVICE_LOST;
        uint64_t elapsed = now >= start ? now - start : 0;
        if (timeout != UINT64_MAX && elapsed >= timeout) return VK_TIMEOUT;
        uint64_t remaining = timeout == UINT64_MAX ? 100000ull : timeout - elapsed;
        wait_for_acquire_progress(remaining);
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
            lock_flag(&swapchain->lock);
            uint64_t frame_id = swapchain->frame_id++;
            if (!atomic_load(&swapchain->acquired[index])) {
                item_result = VK_ERROR_INITIALIZATION_FAILED;
            } else {
                unlock_flag(&swapchain->lock);
                if (agcVideoOutPresent(swapchain->video_out, index, frame_id,
                        VK_PS5_PRESENT_TIMEOUT_US) != AGC_OK)
                    item_result = VK_ERROR_SURFACE_LOST_KHR;
                lock_flag(&swapchain->lock);
            }
            if (item_result == VK_SUCCESS)
                atomic_store(&swapchain->acquired[index], false);
            unlock_flag(&swapchain->lock);
        }
        if (pPresentInfo->pResults) pPresentInfo->pResults[i] = item_result;
        if (overall == VK_SUCCESS && item_result != VK_SUCCESS)
            overall = item_result;
    }
    return overall;
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
