#include <vulkan/vulkan.h>

#include <stdio.h>
#include <string.h>

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
    VkSurfaceFormatKHR format;
    uint32_t zero = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &zero,
                                              &format) != VK_INCOMPLETE)
        return 1;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                                &format_count, &format));
    if (format.format != VK_FORMAT_B8G8R8A8_SRGB) return 1;

    uint32_t mode_count = 1;
    VkPresentModeKHR mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                                     &mode_count, &mode));
    if (mode != VK_PRESENT_MODE_FIFO_KHR) return 1;

    extension_count = 0;
    CHECK(vkEnumerateDeviceExtensionProperties(physical_device, NULL,
                                                &extension_count, NULL));
    VkExtensionProperties device_extensions[16];
    if (extension_count > 16) return 1;
    CHECK(vkEnumerateDeviceExtensionProperties(physical_device, NULL,
                                                &extension_count,
                                                device_extensions));
    if (!has_extension(device_extensions, extension_count,
                       VK_KHR_SWAPCHAIN_EXTENSION_NAME))
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
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = enabled_device_extensions,
    };
    VkDevice device = VK_NULL_HANDLE;
    CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = 3,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = capabilities.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));

    uint32_t image_count = 0;
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL));
    if (image_count != 3) return 1;
    VkImage images[3];
    uint32_t short_count = 2;
    if (vkGetSwapchainImagesKHR(device, swapchain, &short_count, images) !=
        VK_INCOMPLETE || short_count != 2)
        return 1;
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images));

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

    for (uint32_t i = 0; i < 3; ++i) {
        VkResult per_swapchain = VK_SUCCESS;
        const VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquire_semaphores[i],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &indices[i],
            .pResults = &per_swapchain,
        };
        CHECK(vkQueuePresentKHR(queue, &present_info));
        if (per_swapchain != VK_SUCCESS) return 1;
    }

    VkDeviceGroupPresentCapabilitiesKHR group_capabilities = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_CAPABILITIES_KHR,
    };
    CHECK(vkGetDeviceGroupPresentCapabilitiesKHR(device, &group_capabilities));
    if (group_capabilities.presentMask[0] != 1 ||
        group_capabilities.modes != VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR)
        return 1;

    swapchain_info.oldSwapchain = swapchain;
    VkSwapchainKHR replacement = VK_NULL_HANDLE;
    CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &replacement));
    if (vkAcquireNextImageKHR(device, swapchain, 0, acquire_semaphores[0],
                              VK_NULL_HANDLE, &unavailable_index) !=
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
    for (uint32_t i = 0; i < 4; ++i)
        vkDestroySemaphore(device, acquire_semaphores[i], NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    puts("vulkan_ps5 WSI tests passed");
    return 0;
}
