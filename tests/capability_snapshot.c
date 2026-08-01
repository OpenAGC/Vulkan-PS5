#include <vulkan/vulkan.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void print_hex(const char *key, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    printf("%s\t", key);
    for (size_t i = 0; i < size; ++i)
        printf("%02x", bytes[i]);
    putchar('\n');
}

static int snapshot(void)
{
    static const char *const instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
    };
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vulkan-ps5-capability-snapshot",
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = instance_extensions,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS)
        return 2;

    uint32_t api_version = 0;
    if (vkEnumerateInstanceVersion(&api_version) != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 3;
    }
    printf("schema\t1\napi\t%u\n", api_version);

    uint32_t instance_extension_count = 0;
    if (vkEnumerateInstanceExtensionProperties(
            NULL, &instance_extension_count, NULL) != VK_SUCCESS ||
        instance_extension_count > 64) {
        vkDestroyInstance(instance, NULL);
        return 4;
    }
    VkExtensionProperties instance_properties[64];
    uint32_t written = instance_extension_count;
    if (vkEnumerateInstanceExtensionProperties(
            NULL, &written, instance_properties) != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 5;
    }
    for (uint32_t i = 0; i < written; ++i)
        printf("instance_extension\t%s\t%u\n",
               instance_properties[i].extensionName,
               instance_properties[i].specVersion);

    uint32_t physical_count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    if (vkEnumeratePhysicalDevices(instance, &physical_count, &physical) !=
            VK_SUCCESS ||
        physical_count != 1) {
        vkDestroyInstance(instance, NULL);
        return 6;
    }

    VkPhysicalDeviceProperties properties = {0};
    VkPhysicalDeviceFeatures features = {0};
    vkGetPhysicalDeviceProperties(physical, &properties);
    vkGetPhysicalDeviceFeatures(physical, &features);
    printf("device\t%u\t%u\t%u\t%u\t%s\n", properties.apiVersion,
           properties.driverVersion, properties.vendorID, properties.deviceID,
           properties.deviceName);
    print_hex("limits", &properties.limits, sizeof(properties.limits));
    print_hex("sparse_properties", &properties.sparseProperties,
              sizeof(properties.sparseProperties));
    print_hex("features", &features, sizeof(features));

    uint32_t device_extension_count = 0;
    if (vkEnumerateDeviceExtensionProperties(
            physical, NULL, &device_extension_count, NULL) != VK_SUCCESS ||
        device_extension_count > 128) {
        vkDestroyInstance(instance, NULL);
        return 7;
    }
    VkExtensionProperties device_properties[128];
    written = device_extension_count;
    if (vkEnumerateDeviceExtensionProperties(
            physical, NULL, &written, device_properties) != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 8;
    }
    for (uint32_t i = 0; i < written; ++i)
        printf("device_extension\t%s\t%u\n",
               device_properties[i].extensionName,
               device_properties[i].specVersion);

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, NULL);
    if (queue_count > 16) {
        vkDestroyInstance(instance, NULL);
        return 9;
    }
    VkQueueFamilyProperties queues[16];
    memset(queues, 0, sizeof(queues));
    written = queue_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &written, queues);
    for (uint32_t i = 0; i < written; ++i)
        printf("queue\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n", i,
               queues[i].queueFlags, queues[i].queueCount,
               queues[i].timestampValidBits,
               queues[i].minImageTransferGranularity.width,
               queues[i].minImageTransferGranularity.height,
               queues[i].minImageTransferGranularity.depth);

    VkPhysicalDeviceMemoryProperties memory = {0};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        printf("memory_heap\t%u\t%" PRIu64 "\t%u\n", i,
               (uint64_t)memory.memoryHeaps[i].size,
               memory.memoryHeaps[i].flags);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        printf("memory_type\t%u\t%u\t%u\n", i,
               memory.memoryTypes[i].propertyFlags,
               memory.memoryTypes[i].heapIndex);

    for (uint32_t format = 0; format <= 512; ++format) {
        VkFormatProperties format_properties = {0};
        vkGetPhysicalDeviceFormatProperties(
            physical, (VkFormat)format, &format_properties);
        if (format_properties.linearTilingFeatures ||
            format_properties.optimalTilingFeatures ||
            format_properties.bufferFeatures)
            printf("format\t%u\t%u\t%u\t%u\n", format,
                   format_properties.linearTilingFeatures,
                   format_properties.optimalTilingFeatures,
                   format_properties.bufferFeatures);
    }
    {
        const VkFormat format = VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT;
        VkFormatProperties format_properties = {0};
        vkGetPhysicalDeviceFormatProperties(
            physical, format, &format_properties);
        if (format_properties.linearTilingFeatures ||
            format_properties.optimalTilingFeatures ||
            format_properties.bufferFeatures)
            printf("format\t%u\t%u\t%u\t%u\n", (uint32_t)format,
                   format_properties.linearTilingFeatures,
                   format_properties.optimalTilingFeatures,
                   format_properties.bufferFeatures);
    }

    const VkHeadlessSurfaceCreateInfoEXT surface_info = {
        .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateHeadlessSurfaceEXT(
            instance, &surface_info, NULL, &surface) != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 10;
    }
    VkBool32 present_support = VK_FALSE;
    VkSurfaceCapabilitiesKHR capabilities = {0};
    if (vkGetPhysicalDeviceSurfaceSupportKHR(
            physical, 0, surface, &present_support) != VK_SUCCESS ||
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physical, surface, &capabilities) != VK_SUCCESS) {
        vkDestroySurfaceKHR(instance, surface, NULL);
        vkDestroyInstance(instance, NULL);
        return 11;
    }
    printf("wsi_support\t%u\n", present_support);
    printf("wsi_capabilities\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
           capabilities.minImageCount, capabilities.maxImageCount,
           capabilities.currentExtent.width, capabilities.currentExtent.height,
           capabilities.minImageExtent.width, capabilities.minImageExtent.height,
           capabilities.maxImageExtent.width, capabilities.maxImageExtent.height,
           capabilities.maxImageArrayLayers, capabilities.supportedTransforms,
           capabilities.supportedUsageFlags);

    uint32_t surface_format_count = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical, surface, &surface_format_count, NULL) != VK_SUCCESS ||
        surface_format_count > 16) {
        vkDestroySurfaceKHR(instance, surface, NULL);
        vkDestroyInstance(instance, NULL);
        return 12;
    }
    VkSurfaceFormatKHR surface_formats[16];
    written = surface_format_count;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(
            physical, surface, &written, surface_formats) != VK_SUCCESS) {
        vkDestroySurfaceKHR(instance, surface, NULL);
        vkDestroyInstance(instance, NULL);
        return 13;
    }
    for (uint32_t i = 0; i < written; ++i)
        printf("wsi_format\t%u\t%u\n", surface_formats[i].format,
               surface_formats[i].colorSpace);

    uint32_t present_mode_count = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical, surface, &present_mode_count, NULL) != VK_SUCCESS ||
        present_mode_count > 16) {
        vkDestroySurfaceKHR(instance, surface, NULL);
        vkDestroyInstance(instance, NULL);
        return 14;
    }
    VkPresentModeKHR present_modes[16];
    written = present_mode_count;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical, surface, &written, present_modes) != VK_SUCCESS) {
        vkDestroySurfaceKHR(instance, surface, NULL);
        vkDestroyInstance(instance, NULL);
        return 15;
    }
    for (uint32_t i = 0; i < written; ++i)
        printf("wsi_present_mode\t%u\n", present_modes[i]);

    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}

int main(void)
{
    return snapshot();
}
