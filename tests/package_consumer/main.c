#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>

#ifdef VULKAN_PS5_PACKAGE_CONSUMER_PROSPERO
int sceKernelUsleep(unsigned int microseconds);
int sceSystemServiceGetAppStatus(void *status);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);

static _Noreturn void terminate_prospero_app(void) {
    uint32_t status[0x100 / sizeof(uint32_t)] = {0};
    const int status_result = sceSystemServiceGetAppStatus(status);
    uint32_t app_id = status[2];
    if (app_id < 0x10u || app_id == UINT32_MAX)
        app_id = status[0];
    if (status_result == 0 && app_id >= 0x10u && app_id != UINT32_MAX)
        (void)sceSystemServiceKillApp((int)app_id, 0, 0, 0);

    for (;;)
        sceKernelUsleep(250000);
}
#endif

static int exercise_vulkan(void) {
    const VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "VulkanPS5 package consumer",
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application_info,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS)
        return 1;

    uint32_t physical_device_count = 1;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    if (vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                   &physical_device) != VK_SUCCESS ||
        physical_device_count == 0) {
        vkDestroyInstance(instance, NULL);
        return 2;
    }

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                             &queue_family_count, NULL);
    if (queue_family_count == 0) {
        vkDestroyInstance(instance, NULL);
        return 3;
    }

    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical_device, &device_info, NULL, &device) !=
        VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 4;
    }

    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}

int main(void) {
    uint32_t version = 0;
    int result = vkEnumerateInstanceVersion(&version) == VK_SUCCESS &&
                         version >= VK_API_VERSION_1_1
                     ? exercise_vulkan()
                     : 5;
    printf("package-consumer: %s result=%d\n",
           result == 0 ? "PASS" : "FAIL", result);
    fflush(NULL);
#ifdef VULKAN_PS5_PACKAGE_CONSUMER_PROSPERO
    terminate_prospero_app();
#else
    return result;
#endif
}
