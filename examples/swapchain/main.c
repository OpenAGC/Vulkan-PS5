#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>

#ifdef OPENAGC_PROSPERO
#include <sys/thr.h>
#endif

enum { FRAME_COUNT = 1800, IMAGE_COUNT = 3 };

#define REQUIRE(call) do { \
    result = (call); \
    if (result != VK_SUCCESS) { \
        printf("swapchain: %s failed (%d)\n", #call, result); \
        goto cleanup; \
    } \
} while (0)

int main(void) {
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore complete = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer commands[IMAGE_COUNT] = {VK_NULL_HANDLE};

    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vulkan-ps5-swapchain",
        .apiVersion = VK_API_VERSION_1_1,
    };
    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = instance_extensions,
    };
    REQUIRE(vkCreateInstance(&instance_info, NULL, &instance));

    const VkHeadlessSurfaceCreateInfoEXT surface_info = {
        .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
    };
    REQUIRE(vkCreateHeadlessSurfaceEXT(instance, &surface_info, NULL, &surface));

    uint32_t physical_count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    REQUIRE(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    VkBool32 present_supported = VK_FALSE;
    REQUIRE(vkGetPhysicalDeviceSurfaceSupportKHR(
        physical, 0, surface, &present_supported));
    if (!present_supported) {
        printf("swapchain: universal queue cannot present\n");
        goto cleanup;
    }

    VkSurfaceCapabilitiesKHR capabilities;
    REQUIRE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical, surface, &capabilities));
    uint32_t format_count = 1;
    VkSurfaceFormatKHR format;
    REQUIRE(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical, surface, &format_count, &format));
    uint32_t present_mode_count = 1;
    VkPresentModeKHR present_mode;
    REQUIRE(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical, surface, &present_mode_count, &present_mode));
    if (capabilities.minImageCount != IMAGE_COUNT ||
        capabilities.maxImageCount != IMAGE_COUNT ||
        present_mode != VK_PRESENT_MODE_FIFO_KHR) {
        printf("swapchain: unexpected surface contract\n");
        goto cleanup;
    }

    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
    };
    REQUIRE(vkCreateDevice(physical, &device_info, NULL, &device));
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    const VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = IMAGE_COUNT,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = capabilities.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    REQUIRE(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
    uint32_t image_count = IMAGE_COUNT;
    VkImage images[IMAGE_COUNT];
    REQUIRE(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));
    if (image_count != IMAGE_COUNT) goto cleanup;

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0,
    };
    REQUIRE(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = IMAGE_COUNT,
    };
    REQUIRE(vkAllocateCommandBuffers(device, &command_info, commands));
    for (uint32_t i = 0; i < IMAGE_COUNT; ++i) {
        const VkCommandBufferBeginInfo begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        };
        REQUIRE(vkBeginCommandBuffer(commands[i], &begin));
        const VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
            1, &barrier);
        REQUIRE(vkEndCommandBuffer(commands[i]));
    }

    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    REQUIRE(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired));
    REQUIRE(vkCreateSemaphore(device, &semaphore_info, NULL, &complete));
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    REQUIRE(vkCreateFence(device, &fence_info, NULL, &fence));

    for (uint32_t frame = 0; frame < FRAME_COUNT; ++frame) {
        REQUIRE(vkWaitForFences(device, 1, &fence, VK_TRUE, 2000000000ull));
        REQUIRE(vkResetFences(device, 1, &fence));
        uint32_t image_index = 0;
        REQUIRE(vkAcquireNextImageKHR(device, swapchain, 2000000000ull,
                                      acquired, VK_NULL_HANDLE, &image_index));
        const VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSubmitInfo submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquired,
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &commands[image_index],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &complete,
        };
        REQUIRE(vkQueueSubmit(queue, 1, &submit, fence));
        const VkPresentInfoKHR present = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &complete,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index,
        };
        REQUIRE(vkQueuePresentKHR(queue, &present));
        if ((frame + 1u) % 300u == 0u)
            printf("swapchain: %u/%u frames\n", frame + 1u, FRAME_COUNT);
    }
    REQUIRE(vkDeviceWaitIdle(device));
    result = VK_SUCCESS;

cleanup:
    printf("swapchain: cleanup begin\n");
    if (device) vkDeviceWaitIdle(device);
    if (fence) vkDestroyFence(device, fence, NULL);
    if (complete) vkDestroySemaphore(device, complete, NULL);
    if (acquired) vkDestroySemaphore(device, acquired, NULL);
    if (command_pool) vkDestroyCommandPool(device, command_pool, NULL);
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, NULL);
    printf("swapchain: swapchain destroyed\n");
    if (device) vkDestroyDevice(device, NULL);
    if (surface) vkDestroySurfaceKHR(instance, surface, NULL);
    if (instance) vkDestroyInstance(instance, NULL);
    if (result == VK_SUCCESS)
        printf("swapchain: PASS %u frames\n", FRAME_COUNT);
    const int exit_code = result == VK_SUCCESS ? 0 : 1;
#ifdef OPENAGC_PROSPERO
    fflush(NULL);
    long thread_state = exit_code;
    thr_exit(&thread_state);
    __builtin_unreachable();
#else
    return exit_code;
#endif
}
