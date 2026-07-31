#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../system_service_exit.h"

#define BUFFER_SIZE 256u

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf("buffer_copy: %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0u; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if ((compatible_types & (1u << i)) != 0u &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return i;
    }
    return UINT32_MAX;
}

static int create_buffer(VkPhysicalDevice physical, VkDevice device,
    VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *memory,
    uint8_t **mapped)
{
    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = BUFFER_SIZE,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = vkCreateBuffer(device, &buffer_info, NULL, buffer);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);
    uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &memory_info, NULL, memory);
    if (result == VK_SUCCESS)
        result = vkBindBufferMemory(device, *buffer, *memory, 0u);
    if (result == VK_SUCCESS)
        result = vkMapMemory(device, *memory, 0u, BUFFER_SIZE, 0u,
            (void **)mapped);
    return result;
}

static int run_buffer_copy(void)
{
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t physical_count = 1u;
    VkPhysicalDevice physical;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u) {
        printf("buffer_copy: expected one physical device\n");
        return 1;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0u,
        .queueCount = 1u,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &queue_info,
    };
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));

    VkBuffer source, destination;
    VkDeviceMemory source_memory, destination_memory;
    uint8_t *source_data, *destination_data;
    VK_CHECK(create_buffer(physical, device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        &source, &source_memory, &source_data));
    VK_CHECK(create_buffer(physical, device, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        &destination, &destination_memory, &destination_data));
    for (uint32_t i = 0u; i < BUFFER_SIZE; ++i)
        source_data[i] = (uint8_t)(i ^ 0x5au);
    memset(destination_data, 0xcdu, BUFFER_SIZE);
    const VkMappedMemoryRange upload_ranges[2] = {
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, source_memory, 0u,
            BUFFER_SIZE},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, destination_memory, 0u,
            BUFFER_SIZE},
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 2u, upload_ranges));

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocate_info, &command));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(command, &begin_info));
    const VkBufferCopy regions[2] = {
        {16u, 32u, 64u},
        {128u, 144u, 80u},
    };
    vkCmdCopyBuffer(command, source, destination, 2u, regions);
    VK_CHECK(vkEndCommandBuffer(command));

    VkQueue queue;
    vkGetDeviceQueue(device, 0u, 0u, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    VK_CHECK(vkQueueSubmit(queue, 1u, &submit_info, fence));
    VK_CHECK(vkWaitForFences(device, 1u, &fence, VK_TRUE, 5000000000ull));
    const VkMappedMemoryRange readback_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = destination_memory,
        .offset = 0u,
        .size = BUFFER_SIZE,
    };
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1u, &readback_range));

    uint32_t copied = 0u, guards = 0u;
    int status = 0;
    for (uint32_t i = 0u; i < BUFFER_SIZE; ++i) {
        uint8_t expected = 0xcdu;
        bool is_copied = i >= 32u && i < 96u;
        if (is_copied)
            expected = source_data[16u + i - 32u];
        else if (i >= 144u && i < 224u) {
            is_copied = true;
            expected = source_data[128u + i - 144u];
        }
        if (destination_data[i] != expected) {
            printf("buffer_copy: mismatch at %u: %02x != %02x\n",
                i, destination_data[i], expected);
            status = 1;
            break;
        }
        if (is_copied)
            copied++;
        else
            guards++;
    }
    if (!status)
        printf("buffer_copy: PASS bytes=%u regions=2 guards=%u\n",
            copied, guards);

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkUnmapMemory(device, destination_memory);
    vkUnmapMemory(device, source_memory);
    vkDestroyBuffer(device, destination, NULL);
    vkDestroyBuffer(device, source, NULL);
    vkFreeMemory(device, destination_memory, NULL);
    vkFreeMemory(device, source_memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return status;
}

int main(void)
{
    int status;

    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    puts("buffer_copy: stage=start");
    status = run_buffer_copy();
    printf("buffer_copy: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("buffer_copy");
#endif
    return status;
}
