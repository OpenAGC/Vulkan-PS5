#include <vulkan/vulkan.h>
#include <agc_error.h>
#include <agc_videoout.h>

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

VkBool32 vk_ps5_swapchain_has_native_present_chain(VkSwapchainKHR swapchain);
VkFormat vk_ps5_image_blit_format(VkImage image);
uint32_t vk_ps5_device_wsi_ownership_count(VkDevice device);

#define CHECK(call) do { \
    VkResult check_result = (call); \
    if (check_result != VK_SUCCESS) { \
        fprintf(stderr, "%s failed: %d\n", #call, check_result); \
        return 1; \
    } \
} while (0)

static int has_extension(const VkExtensionProperties *extensions,
                         uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(extensions[i].extensionName, name) == 0) return 1;
    return 0;
}

typedef struct PresentThreadArgs {
    VkQueue queue;
    VkSwapchainKHR swapchain;
    VkSemaphore semaphore;
    uint32_t image_index;
    VkResult result;
} PresentThreadArgs;

static void *delayed_present(void *opaque) {
    PresentThreadArgs *args = opaque;
    const struct timespec delay = {.tv_nsec = 10000000};
    nanosleep(&delay, NULL);
    const VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &args->semaphore,
        .swapchainCount = 1,
        .pSwapchains = &args->swapchain,
        .pImageIndices = &args->image_index,
    };
    args->result = vkQueuePresentKHR(args->queue, &present);
    return NULL;
}

int main(void) {
    uint32_t extension_count = 0;
    CHECK(vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL));
    VkExtensionProperties instance_extensions[16];
    if (extension_count > 16) return 1;
    CHECK(vkEnumerateInstanceExtensionProperties(NULL, &extension_count,
                                                  instance_extensions));
    if (!has_extension(instance_extensions, extension_count,
                       VK_KHR_SURFACE_EXTENSION_NAME) ||
        !has_extension(instance_extensions, extension_count,
                       VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME))
        return 1;

    const char *enabled_instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
    };
    const VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = enabled_instance_extensions,
    };
    VkInstance instance = VK_NULL_HANDLE;
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));

    const VkHeadlessSurfaceCreateInfoEXT surface_info = {
        .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    CHECK(vkCreateHeadlessSurfaceEXT(instance, &surface_info, NULL, &surface));

    uint32_t physical_count = 1;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical_device));
    VkBool32 supported = VK_FALSE;
    CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, 0, surface,
                                                &supported));
    if (!supported) return 1;
    CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, 1, surface,
                                                &supported));
    if (supported) return 1;

    VkSurfaceCapabilitiesKHR capabilities;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                                                      &capabilities));
    if (capabilities.minImageCount != 3 || capabilities.maxImageCount != 3 ||
        capabilities.currentExtent.width != 1920 ||
        capabilities.currentExtent.height != 1080)
        return 1;

    uint32_t format_count = 0;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                                &format_count, NULL));
    VkSurfaceFormatKHR formats[2];
    uint32_t zero = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &zero,
                                              formats) != VK_INCOMPLETE)
        return 1;
    uint32_t one = 1;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &one,
                                              formats) != VK_INCOMPLETE ||
        one != 1 || formats[0].format != VK_FORMAT_B8G8R8A8_SRGB)
        return 1;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                                &format_count, formats));
    if (format_count != 2 ||
        formats[0].format != VK_FORMAT_B8G8R8A8_SRGB ||
        formats[1].format != VK_FORMAT_B8G8R8A8_UNORM)
        return 1;

    uint32_t mode_count = 1;
    VkPresentModeKHR mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                                     &mode_count, &mode));
    if (mode != VK_PRESENT_MODE_FIFO_KHR) return 1;

    extension_count = 0;
    CHECK(vkEnumerateDeviceExtensionProperties(physical_device, NULL,
                                                &extension_count, NULL));
VkExtensionProperties device_extensions[32];
if (extension_count > 32) return 1;
    CHECK(vkEnumerateDeviceExtensionProperties(physical_device, NULL,
                                                &extension_count,
                                                device_extensions));
    if (!has_extension(device_extensions, extension_count,
                       VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
        !has_extension(device_extensions, extension_count,
                       VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME) ||
        !has_extension(device_extensions, extension_count,
                       VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME) ||
        !has_extension(device_extensions, extension_count,
                       VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME))
        return 1;

    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const char *enabled_device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
        VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 4,
        .ppEnabledExtensionNames = enabled_device_extensions,
    };
    VkDevice device = VK_NULL_HANDLE;
    CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    const VkFormat mutable_formats[] = {
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,
    };
    const VkImageFormatListCreateInfo format_list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2,
        .pViewFormats = mutable_formats,
    };
    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = &format_list,
        .flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR,
        .surface = surface,
        .minImageCount = 3,
        .imageFormat = formats[0].format,
        .imageColorSpace = formats[0].colorSpace,
        .imageExtent = capabilities.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
    if (!vk_ps5_swapchain_has_native_present_chain(swapchain)) return 1;

    uint32_t image_count = 0;
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL));
    if (image_count != 3) return 1;
    VkImage images[3];
    uint32_t short_count = 2;
    if (vkGetSwapchainImagesKHR(device, swapchain, &short_count, images) !=
        VK_INCOMPLETE || short_count != 2)
        return 1;
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));
    for (uint32_t i = 0; i < image_count; ++i) {
        if (vk_ps5_image_blit_format(images[i]) !=
            VK_FORMAT_B8G8R8A8_SRGB)
            return 1;
    }
    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VkCommandPool command_pool = VK_NULL_HANDLE;
    CHECK(vkCreateCommandPool(device, &command_pool_info, NULL,
                              &command_pool));
    VkCommandBuffer present_commands[3] = {VK_NULL_HANDLE};
    const VkCommandBufferAllocateInfo command_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 3,
    };
    CHECK(vkAllocateCommandBuffers(device, &command_allocate_info,
                                   present_commands));
    for (uint32_t i = 0; i < 3; ++i) {
        const VkCommandBufferBeginInfo begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };
        CHECK(vkBeginCommandBuffer(present_commands[i], &begin));
        VkImageMemoryBarrier barriers[3] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i],
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = i == 1 ? VK_ACCESS_TRANSFER_READ_BIT :
                                         VK_ACCESS_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = i == 1 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL :
                                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i],
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i],
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            },
        };
        vkCmdPipelineBarrier(present_commands[i],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
            1, &barriers[0]);
        vkCmdPipelineBarrier(present_commands[i],
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
            1, &barriers[1]);
        if (i == 1) {
            vkCmdPipelineBarrier(present_commands[i],
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
                1, &barriers[2]);
        }
        CHECK(vkEndCommandBuffer(present_commands[i]));
    }
    const VkImageViewCreateInfo mutable_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = images[0],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    VkImageView mutable_view = VK_NULL_HANDLE;
    CHECK(vkCreateImageView(device, &mutable_view_info, NULL, &mutable_view));
    vkDestroyImageView(device, mutable_view, NULL);

    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkSemaphore acquire_semaphores[4] = {VK_NULL_HANDLE};
    uint32_t indices[3];
    for (uint32_t i = 0; i < 4; ++i)
        CHECK(vkCreateSemaphore(device, &semaphore_info, NULL,
                                &acquire_semaphores[i]));
    for (uint32_t i = 0; i < 3; ++i)
        CHECK(vkAcquireNextImageKHR(device, swapchain, 0,
                                    acquire_semaphores[i], VK_NULL_HANDLE,
                                    &indices[i]));
    uint32_t unavailable_index = 0;
    if (vkAcquireNextImageKHR(device, swapchain, 0, acquire_semaphores[3],
                              VK_NULL_HANDLE, &unavailable_index) != VK_NOT_READY)
        return 1;
    if (vkAcquireNextImageKHR(device, swapchain, 1000, acquire_semaphores[3],
                              VK_NULL_HANDLE, &unavailable_index) != VK_TIMEOUT)
        return 1;

    PresentThreadArgs thread_args = {
        .queue = queue,
        .swapchain = swapchain,
        .semaphore = acquire_semaphores[0],
        .image_index = indices[0],
        .result = VK_ERROR_UNKNOWN,
    };
    pthread_t present_thread;
    if (pthread_create(&present_thread, NULL, delayed_present, &thread_args) != 0)
        return 1;
    CHECK(vkAcquireNextImageKHR(device, swapchain, 1000000000ull,
                                acquire_semaphores[3], VK_NULL_HANDLE,
                                &unavailable_index));
    if (pthread_join(present_thread, NULL) != 0 ||
        thread_args.result != VK_SUCCESS || unavailable_index != indices[0])
        return 1;

    for (uint32_t i = 0; i < 3; ++i) {
        const VkSemaphore present_semaphore = i == 0 ?
            acquire_semaphores[3] : acquire_semaphores[i];
        VkResult per_swapchain = VK_SUCCESS;
        const VkRectLayerKHR present_rectangle = {
            .offset = {0, 0},
            .extent = capabilities.currentExtent,
            .layer = 0,
        };
        const VkPresentRegionKHR present_region = {
            .rectangleCount = 1,
            .pRectangles = &present_rectangle,
        };
        const VkPresentRegionsKHR present_regions = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
            .swapchainCount = 1,
            .pRegions = &present_region,
        };
        const VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = &present_regions,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &present_semaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &indices[i],
            .pResults = &per_swapchain,
        };
        CHECK(vkQueuePresentKHR(queue, &present_info));
        if (per_swapchain != VK_SUCCESS) return 1;
    }

    for (uint32_t iteration = 0; iteration < 16; ++iteration) {
        uint32_t recycle_index = 0;
        CHECK(vkAcquireNextImageKHR(device, swapchain, 0,
                                    acquire_semaphores[0], VK_NULL_HANDLE,
                                    &recycle_index));
        VkResult recycle_result = VK_SUCCESS;
        const VkPresentInfoKHR recycle_present = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquire_semaphores[0],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &recycle_index,
            .pResults = &recycle_result,
        };
        CHECK(vkQueuePresentKHR(queue, &recycle_present));
        if (recycle_result != VK_SUCCESS) return 1;
    }

    VkDeviceGroupPresentCapabilitiesKHR group_capabilities = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_CAPABILITIES_KHR,
    };
    CHECK(vkGetDeviceGroupPresentCapabilitiesKHR(device, &group_capabilities));
    if (group_capabilities.presentMask[0] != 1 ||
        group_capabilities.modes != VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR)
        return 1;

    uint32_t retired_swapchain_index = 0;
    CHECK(vkAcquireNextImageKHR(device, swapchain, 0, acquire_semaphores[0],
                                VK_NULL_HANDLE, &retired_swapchain_index));

    swapchain_info.oldSwapchain = swapchain;
    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &replacement));
    if (vkAcquireNextImageKHR(device, swapchain, 0, acquire_semaphores[1],
                              VK_NULL_HANDLE, &unavailable_index) !=
        VK_ERROR_OUT_OF_DATE_KHR)
        return 1;

    const VkPresentInfoKHR retired_present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &acquire_semaphores[0],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &retired_swapchain_index,
    };
    if (vkQueuePresentKHR(queue, &retired_present) !=
        VK_ERROR_OUT_OF_DATE_KHR)
        return 1;

    uint32_t replacement_index = 0;
    CHECK(vkAcquireNextImageKHR(device, replacement, 0, acquire_semaphores[0],
                                VK_NULL_HANDLE, &replacement_index));
    const VkPresentInfoKHR replacement_present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &acquire_semaphores[0],
        .swapchainCount = 1,
        .pSwapchains = &replacement,
        .pImageIndices = &replacement_index,
    };
    CHECK(vkQueuePresentKHR(queue, &replacement_present));

    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroySwapchainKHR(device, replacement, NULL);

    const VkFormat zink_mutable_formats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
    };
    const VkImageFormatListCreateInfo zink_format_list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2,
        .pViewFormats = zink_mutable_formats,
    };
    swapchain_info.pNext = &zink_format_list;
    swapchain_info.oldSwapchain = VK_NULL_HANDLE;
    swapchain_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkSwapchainKHR zink_swapchain = VK_NULL_HANDLE;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL,
                               &zink_swapchain));
    uint32_t zink_image_count = 3;
    VkImage zink_images[3] = {VK_NULL_HANDLE};
    CHECK(vkGetSwapchainImagesKHR(device, zink_swapchain,
                                  &zink_image_count, zink_images));
    if (zink_image_count != 3)
        return 1;
    for (uint32_t i = 0; i < zink_image_count; ++i) {
        if (vk_ps5_image_blit_format(zink_images[i]) !=
            VK_FORMAT_B8G8R8A8_SRGB)
            return 1;
    }

    /*
     * Eden presents through a blit from a normal UNORM image into Zink's
     * mutable UNORM swapchain.  The scanout image itself is SRGB, so this
     * specifically verifies that the command recorder selects the native
     * destination format rather than rejecting or misconfiguring the blit.
     */
    const VkImageCreateInfo scanout_blit_source_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {1280u, 720u, 1u},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage scanout_blit_source = VK_NULL_HANDLE;
    CHECK(vkCreateImage(device, &scanout_blit_source_info, NULL,
                        &scanout_blit_source));
    VkMemoryRequirements scanout_blit_source_requirements;
    vkGetImageMemoryRequirements(device, scanout_blit_source,
                                 &scanout_blit_source_requirements);
    const VkMemoryAllocateInfo scanout_blit_source_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = scanout_blit_source_requirements.size,
        .memoryTypeIndex = 0u,
    };
    VkDeviceMemory scanout_blit_source_memory = VK_NULL_HANDLE;
    CHECK(vkAllocateMemory(device, &scanout_blit_source_memory_info, NULL,
                           &scanout_blit_source_memory));
    CHECK(vkBindImageMemory(device, scanout_blit_source,
                            scanout_blit_source_memory, 0u));

    const VkCommandBufferAllocateInfo scanout_blit_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer scanout_blit_command = VK_NULL_HANDLE;
    CHECK(vkAllocateCommandBuffers(device, &scanout_blit_allocate_info,
                                   &scanout_blit_command));
    const VkCommandBufferBeginInfo scanout_blit_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    CHECK(vkBeginCommandBuffer(scanout_blit_command, &scanout_blit_begin));
    const VkImageMemoryBarrier scanout_blit_barriers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = scanout_blit_source,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = zink_images[0],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        },
    };
    vkCmdPipelineBarrier(scanout_blit_command,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u, 0u, NULL, 0u, NULL, 2u, scanout_blit_barriers);
    const VkImageBlit scanout_blit_region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .srcOffsets = {{0, 0, 0}, {1280, 720, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstOffsets = {{0, 0, 0},
                       {(int32_t)capabilities.currentExtent.width,
                        (int32_t)capabilities.currentExtent.height, 1}},
    };
    vkCmdBlitImage(scanout_blit_command, scanout_blit_source,
                   VK_IMAGE_LAYOUT_GENERAL, zink_images[0],
                   VK_IMAGE_LAYOUT_GENERAL, 1u, &scanout_blit_region,
                   VK_FILTER_LINEAR);
    CHECK(vkEndCommandBuffer(scanout_blit_command));

    vkDestroyImage(device, scanout_blit_source, NULL);
    vkFreeMemory(device, scanout_blit_source_memory, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    for (uint32_t i = 0; i < 4; ++i)
        vkDestroySemaphore(device, acquire_semaphores[i], NULL);

    agcVideoOutDebugSetNextCloseResult(AGC_ERROR_INTERNAL);
    vkDestroySwapchainKHR(device, zink_swapchain, NULL);
    if (vk_ps5_device_wsi_ownership_count(device) != 1u)
        return 1;
    VkSwapchainKHR rejected_swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device, &swapchain_info, NULL,
            &rejected_swapchain) != VK_ERROR_DEVICE_LOST ||
        rejected_swapchain != VK_NULL_HANDLE)
        return 1;
    vkDestroyDevice(device, NULL);
    if (vk_ps5_device_wsi_ownership_count(device) != 1u)
        return 1;
    vkDestroySurfaceKHR(instance, surface, NULL);
    puts("vulkan_ps5 WSI tests passed (including fail-closed teardown)");
    return 0;
}
