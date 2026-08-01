#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdint.h>

int main(void) {
    uint32_t version = 0;
    assert(vkEnumerateInstanceVersion(&version) == VK_SUCCESS);
    assert(version >= VK_API_VERSION_1_1);

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "loader-smoke",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&create_info, NULL, &instance) == VK_SUCCESS);

    uint32_t count = 0;
    assert(vkEnumeratePhysicalDevices(instance, &count, NULL) == VK_SUCCESS);
    assert(count == 1);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &count, &physical) == VK_SUCCESS);
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    assert(properties.vendorID == 0x1002);

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
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);
    assert(vkGetDeviceProcAddr(device, "vkCmdBindVertexBuffers2") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCmdBindVertexBuffers2EXT") != NULL);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);
    assert(queue != VK_NULL_HANDLE);
    assert(vkQueueWaitIdle(queue) == VK_SUCCESS);
    assert(vkDeviceWaitIdle(device) == VK_SUCCESS);
    vkDestroyDevice(device, NULL);

    vkDestroyInstance(instance, NULL);
    return 0;
}
