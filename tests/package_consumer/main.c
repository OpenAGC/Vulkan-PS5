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
    const VkPhysicalDeviceTimelineSemaphoreFeatures timeline_feature = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .timelineSemaphore = VK_TRUE,
    };
    const char *const device_extensions[] = {
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &timeline_feature,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
    };
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical_device, &device_info, NULL, &device) !=
        VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 4;
    }

    VkDevice peer_device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical_device, &device_info, NULL, &peer_device) !=
        VK_SUCCESS) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return 5;
    }
    VkQueue queue = VK_NULL_HANDLE;
    VkQueue peer_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);
    vkGetDeviceQueue(peer_device, 0, 0, &peer_queue);
    if (queue == VK_NULL_HANDLE || peer_queue == VK_NULL_HANDLE ||
        queue == peer_queue) {
        vkDestroyDevice(peer_device, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return 6;
    }
    vkDestroyDevice(peer_device, NULL);
    if (vkDeviceWaitIdle(device) != VK_SUCCESS) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return 7;
    }
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &memory_info, NULL, &memory) != VK_SUCCESS) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return 8;
    }
    vkFreeMemory(device, memory, NULL);

    const VkSemaphoreTypeCreateInfo timeline_type = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 1,
    };
    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timeline_type,
    };
    VkSemaphore timeline = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &semaphore_info, NULL, &timeline) !=
        VK_SUCCESS) {
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return 9;
    }
    const uint64_t signal_value = 2;
    const VkTimelineSemaphoreSubmitInfo timeline_submit = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues = &signal_value,
    };
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &timeline_submit,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &timeline,
    };
    uint64_t observed = 0;
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkGetSemaphoreCounterValue(device, timeline, &observed) != VK_SUCCESS ||
        observed != signal_value) {
        vkDestroySemaphore(device, timeline, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
        return 10;
    }
    vkDestroySemaphore(device, timeline, NULL);

    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}

int main(void) {
    uint32_t version = 0;
    int result = vkEnumerateInstanceVersion(&version) == VK_SUCCESS &&
                         version >= VK_API_VERSION_1_1
                     ? exercise_vulkan()
                     : 11;
    printf("package-consumer: %s result=%d\n",
           result == 0 ? "PASS" : "FAIL", result);
    fflush(NULL);
#ifdef VULKAN_PS5_PACKAGE_CONSUMER_PROSPERO
    terminate_prospero_app();
#else
    return result;
#endif
}
