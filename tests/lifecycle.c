#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    uint32_t api_version = 0;
    assert(vkEnumerateInstanceVersion(&api_version) == VK_SUCCESS);
    assert(VK_API_VERSION_MAJOR(api_version) == 1);
    assert(VK_API_VERSION_MINOR(api_version) == 1);

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "lifecycle",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);

    uint32_t physical_count = 0;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, NULL) == VK_SUCCESS);
    assert(physical_count == 1);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, &physical) == VK_SUCCESS);

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    assert(properties.vendorID == 0x1002);
    assert(strstr(properties.deviceName, "gfx1013") != NULL);

    VkPhysicalDeviceSubgroupProperties subgroup = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    };
    VkPhysicalDeviceMaintenance3Properties maintenance = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES,
        .pNext = &subgroup,
    };
    VkPhysicalDeviceIDProperties id = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
        .pNext = &maintenance,
    };
    VkPhysicalDeviceProperties2 properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &id,
    };
    vkGetPhysicalDeviceProperties2(physical, &properties2);
    assert(id.deviceUUID[0] != 0);
    assert(maintenance.maxMemoryAllocationSize == 12ull * 1024 * 1024 * 1024);
    assert(subgroup.subgroupSize == 32);

    VkPhysicalDeviceVulkan11Features features11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .shaderDrawParameters = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features11,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features2);
    assert(features11.shaderDrawParameters == VK_FALSE);

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical, &features);
    const VkBool32 *feature_bits = (const VkBool32 *)&features;
    for (size_t i = 0; i < sizeof(features) / sizeof(*feature_bits); ++i)
        assert(feature_bits[i] == VK_FALSE);

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    assert(memory_properties.memoryTypeCount == 2);
    assert(memory_properties.memoryHeapCount == 2);
    assert(memory_properties.memoryHeaps[0].size == 4ull * 1024 * 1024 * 1024);
    assert(memory_properties.memoryHeaps[1].size == 12ull * 1024 * 1024 * 1024);

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM,
                                        &format_properties);
    assert(format_properties.optimalTilingFeatures &
           VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
                                        &format_properties);
    assert(format_properties.optimalTilingFeatures == 0);

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
    assert(family_count == 1);
    VkQueueFamilyProperties family;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, &family);
    assert((family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                                VK_QUEUE_TRANSFER_BIT)) != 0);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    VkDeviceGroupDeviceCreateInfo group_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO,
        .physicalDeviceCount = 1,
        .pPhysicalDevices = &physical,
    };
    device_info.pNext = &group_info;
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);
    assert(queue != VK_NULL_HANDLE);

    VkPipelineCacheCreateInfo cache_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    VkPipelineCache cache = VK_NULL_HANDLE;
    assert(vkCreatePipelineCache(device, &cache_info, NULL, &cache) == VK_SUCCESS);
    size_t cache_size = 0;
    assert(vkGetPipelineCacheData(device, cache, &cache_size, NULL) == VK_SUCCESS);
    assert(cache_size >= sizeof(VkPipelineCacheHeaderVersionOne));
    void *cache_data = malloc(cache_size);
    assert(cache_data != NULL);
    assert(vkGetPipelineCacheData(device, cache, &cache_size, cache_data) == VK_SUCCESS);
    VkPipelineCacheHeaderVersionOne *cache_header = cache_data;
    assert(cache_header->vendorID == 0x1002);
    VkPipelineCacheCreateInfo restored_info = cache_info;
    restored_info.initialDataSize = cache_size;
    restored_info.pInitialData = cache_data;
    VkPipelineCache restored_cache = VK_NULL_HANDLE;
    assert(vkCreatePipelineCache(device, &restored_info, NULL, &restored_cache) == VK_SUCCESS);
    assert(vkMergePipelineCaches(device, cache, 1, &restored_cache) == VK_SUCCESS);
    vkDestroyPipelineCache(device, restored_cache, NULL);
    vkDestroyPipelineCache(device, cache, NULL);
    free(cache_data);

    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &allocation, NULL, &memory) == VK_SUCCESS);
    void *mapped = NULL;
    assert(vkMapMemory(device, memory, 128, 256, 0, &mapped) == VK_SUCCESS);
    memset(mapped, 0x5a, 256);
    vkUnmapMemory(device, memory);
    vkFreeMemory(device, memory, NULL);

    assert(vkGetInstanceProcAddr(instance, "vkCreateDevice") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkAllocateMemory") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCreateImage") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCreateGraphicsPipelines") != NULL);
    assert(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice") == NULL);
    assert(vkGetDeviceProcAddr(VK_NULL_HANDLE, "vkAllocateMemory") == NULL);
    assert(vkDeviceWaitIdle(device) == VK_SUCCESS);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
