#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../system_service_exit.h"

#define SOURCE_WIDTH 8u
#define SOURCE_HEIGHT 8u
#define DESTINATION_WIDTH 20u
#define DESTINATION_HEIGHT 20u
#define GUARD_COLOR UINT32_C(0xffcc33aa)

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf("blit: %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

typedef struct TestImage {
    VkImage image;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize allocation_size;
    VkDeviceSize row_pitch;
} TestImage;

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0u; i < properties.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags =
            properties.memoryTypes[i].propertyFlags;
        if ((compatible_types & (1u << i)) != 0u &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return i;
    }
    return UINT32_MAX;
}

static VkResult create_image(VkPhysicalDevice physical, VkDevice device,
    uint32_t width, uint32_t height, VkImageUsageFlags usage, TestImage *image)
{
    const VkImageCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkResult result = vkCreateImage(device, &create, NULL, &image->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image->image, &requirements);
    const uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &allocate, NULL, &image->memory);
    if (result == VK_SUCCESS)
        result = vkBindImageMemory(device, image->image, image->memory, 0u);
    if (result == VK_SUCCESS)
        result = vkMapMemory(device, image->memory, 0u, requirements.size, 0u,
            (void **)&image->mapped);
    if (result != VK_SUCCESS)
        return result;
    const VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, image->image, &subresource, &layout);
    image->mapped += layout.offset;
    image->allocation_size = requirements.size;
    image->row_pitch = layout.rowPitch;
    return VK_SUCCESS;
}

static uint32_t source_color(uint32_t x, uint32_t y)
{
    return UINT32_C(0xff000000) | (x * 29u + 3u) |
        ((y * 31u + 5u) << 8u) | ((x * 7u + y * 11u + 9u) << 16u);
}

static int run_blit(void)
{
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t physical_count = 1u;
    VkPhysicalDevice physical;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u)
        return 1;
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

    TestImage source = {0};
    TestImage destination = {0};
    VK_CHECK(create_image(physical, device, SOURCE_WIDTH, SOURCE_HEIGHT,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &source));
    VK_CHECK(create_image(physical, device, DESTINATION_WIDTH,
        DESTINATION_HEIGHT, VK_IMAGE_USAGE_TRANSFER_DST_BIT, &destination));
    for (uint32_t y = 0u; y < SOURCE_HEIGHT; ++y) {
        uint32_t *row = (uint32_t *)(source.mapped + y * source.row_pitch);
        for (uint32_t x = 0u; x < SOURCE_WIDTH; ++x)
            row[x] = source_color(x, y);
    }
    for (uint32_t y = 0u; y < DESTINATION_HEIGHT; ++y) {
        uint32_t *row = (uint32_t *)(destination.mapped +
            y * destination.row_pitch);
        for (uint32_t x = 0u; x < DESTINATION_WIDTH; ++x)
            row[x] = GUARD_COLOR;
    }
    const VkMappedMemoryRange uploads[] = {
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, source.memory, 0u,
            VK_WHOLE_SIZE},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, destination.memory, 0u,
            VK_WHOLE_SIZE},
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 2u, uploads));

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command;
    VK_CHECK(vkAllocateCommandBuffers(device, &allocate_info, &command));
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(command, &begin));
    const VkImageBlit region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .srcOffsets = {{0, 0, 0}, {8, 8, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .dstOffsets = {{2, 2, 0}, {18, 18, 1}},
    };
    vkCmdBlitImage(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
        destination.image, VK_IMAGE_LAYOUT_GENERAL, 1u, &region,
        VK_FILTER_NEAREST);
    VK_CHECK(vkEndCommandBuffer(command));
    VkQueue queue;
    vkGetDeviceQueue(device, 0u, 0u, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    VK_CHECK(vkQueueSubmit(queue, 1u, &submit, fence));
    VK_CHECK(vkWaitForFences(device, 1u, &fence, VK_TRUE,
        UINT64_C(5000000000)));
    const VkMappedMemoryRange readback = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = destination.memory,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1u, &readback));

    int status = 0;
#if defined(OPENAGC_PROSPERO)
    uint32_t copied = 0u;
    uint32_t guards = 0u;
    for (uint32_t y = 0u; y < DESTINATION_HEIGHT && !status; ++y) {
        const uint32_t *row = (const uint32_t *)(destination.mapped +
            y * destination.row_pitch);
        for (uint32_t x = 0u; x < DESTINATION_WIDTH; ++x) {
            const bool inside = x >= 2u && x < 18u && y >= 2u && y < 18u;
            const uint32_t expected = inside ?
                source_color((x - 2u) / 2u, (y - 2u) / 2u) : GUARD_COLOR;
            if (row[x] != expected) {
                printf("blit: mismatch x=%u y=%u got=%08x expected=%08x\n",
                    x, y, row[x], expected);
                status = 1;
                break;
            }
            if (inside)
                copied++;
            else
                guards++;
        }
    }
    if (!status)
        printf("blit: PASS pixels=%u guards=%u nearest=2x\n",
            copied, guards);
#else
    puts("blit: PASS command recording");
#endif

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkUnmapMemory(device, destination.memory);
    vkUnmapMemory(device, source.memory);
    vkDestroyImage(device, destination.image, NULL);
    vkDestroyImage(device, source.image, NULL);
    vkFreeMemory(device, destination.memory, NULL);
    vkFreeMemory(device, source.memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return status;
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    puts("blit: stage=start");
    const int status = run_blit();
    printf("blit: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("blit");
#endif
    return status;
}
