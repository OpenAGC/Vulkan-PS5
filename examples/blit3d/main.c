#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../system_service_exit.h"

#define VOLUME_WIDTH 4u
#define VOLUME_HEIGHT 4u
#define VOLUME_DEPTH 4u
#define SELF_WIDTH 8u
#define SELF_HEIGHT 8u
#define GUARD_COLOR UINT32_C(0xffc13aa7)

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf("blit3d: %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

typedef struct TestImage {
    VkImage image;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize allocation_size;
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
    VkImageType type, VkExtent3D extent, uint32_t mip_levels,
    VkImageUsageFlags usage, TestImage *image)
{
    const VkImageCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = type,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = extent,
        .mipLevels = mip_levels,
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
    if (result == VK_SUCCESS)
        image->allocation_size = requirements.size;
    return result;
}

static VkSubresourceLayout image_layout(VkDevice device, VkImage image,
    uint32_t mip_level)
{
    const VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = mip_level,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, image, &subresource, &layout);
    return layout;
}

static uint32_t volume_color(uint32_t x, uint32_t y, uint32_t z)
{
    return UINT32_C(0xff000000) | (x * 41u + z * 7u + 3u) |
        ((y * 37u + z * 13u + 5u) << 8u) |
        ((x * 11u + y * 17u + z * 29u + 9u) << 16u);
}

static uint32_t self_color(uint32_t x, uint32_t y)
{
    return UINT32_C(0xff000000) | (x * 23u + 7u) |
        ((y * 19u + 11u) << 8u) | ((x * 5u + y * 31u + 13u) << 16u);
}

static int run_probe(void)
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
    TestImage self = {0};
    const VkExtent3D volume_extent = {
        VOLUME_WIDTH, VOLUME_HEIGHT, VOLUME_DEPTH };
    const VkExtent3D self_extent = { SELF_WIDTH, SELF_HEIGHT, 1u };
    VK_CHECK(create_image(physical, device, VK_IMAGE_TYPE_3D, volume_extent,
        1u, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &source));
    VK_CHECK(create_image(physical, device, VK_IMAGE_TYPE_3D, volume_extent,
        1u, VK_IMAGE_USAGE_TRANSFER_DST_BIT, &destination));
    VK_CHECK(create_image(physical, device, VK_IMAGE_TYPE_2D, self_extent,
        2u, VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT, &self));

    const VkSubresourceLayout source_layout = image_layout(device,
        source.image, 0u);
    const VkSubresourceLayout destination_layout = image_layout(device,
        destination.image, 0u);
    const VkSubresourceLayout self_base_layout = image_layout(device,
        self.image, 0u);
    const VkSubresourceLayout self_mip_layout = image_layout(device,
        self.image, 1u);
    if (!source_layout.depthPitch || !destination_layout.depthPitch ||
        !source_layout.rowPitch || !destination_layout.rowPitch ||
        !self_base_layout.rowPitch || !self_mip_layout.rowPitch)
        return 1;
    printf("blit3d: layouts base=%llu/%llu/%llu mip=%llu/%llu/%llu\n",
        (unsigned long long)self_base_layout.offset,
        (unsigned long long)self_base_layout.rowPitch,
        (unsigned long long)self_base_layout.size,
        (unsigned long long)self_mip_layout.offset,
        (unsigned long long)self_mip_layout.rowPitch,
        (unsigned long long)self_mip_layout.size);
    for (uint32_t z = 0u; z < VOLUME_DEPTH; ++z) {
        for (uint32_t y = 0u; y < VOLUME_HEIGHT; ++y) {
            uint32_t *source_row = (uint32_t *)(source.mapped +
                source_layout.offset + z * source_layout.depthPitch +
                y * source_layout.rowPitch);
            uint32_t *destination_row = (uint32_t *)(destination.mapped +
                destination_layout.offset + z * destination_layout.depthPitch +
                y * destination_layout.rowPitch);
            for (uint32_t x = 0u; x < VOLUME_WIDTH; ++x) {
                source_row[x] = volume_color(x, y, z);
                destination_row[x] = GUARD_COLOR;
            }
        }
    }
    for (uint32_t y = 0u; y < SELF_HEIGHT; ++y) {
        uint32_t *row = (uint32_t *)(self.mapped + self_base_layout.offset +
            y * self_base_layout.rowPitch);
        for (uint32_t x = 0u; x < SELF_WIDTH; ++x)
            row[x] = self_color(x, y);
    }
    for (uint32_t y = 0u; y < SELF_HEIGHT / 2u; ++y) {
        uint32_t *row = (uint32_t *)(self.mapped + self_mip_layout.offset +
            y * self_mip_layout.rowPitch);
        for (uint32_t x = 0u; x < SELF_WIDTH / 2u; ++x)
            row[x] = GUARD_COLOR;
    }
    const VkMappedMemoryRange uploads[] = {
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, source.memory, 0u,
            VK_WHOLE_SIZE},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, destination.memory, 0u,
            VK_WHOLE_SIZE},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, self.memory, 0u,
            VK_WHOLE_SIZE},
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 3u, uploads));

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
    const VkImageBlit volume_region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .srcOffsets = {{0, 0, 0}, {4, 4, 4}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .dstOffsets = {{0, 0, 0}, {4, 4, 4}},
    };
    const VkImageBlit self_region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .srcOffsets = {{0, 0, 0}, {8, 8, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 1u, 0u, 1u},
        .dstOffsets = {{0, 0, 0}, {4, 4, 1}},
    };
    vkCmdBlitImage(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
        destination.image, VK_IMAGE_LAYOUT_GENERAL, 1u, &volume_region,
        VK_FILTER_NEAREST);
    vkCmdBlitImage(command, self.image, VK_IMAGE_LAYOUT_GENERAL,
        self.image, VK_IMAGE_LAYOUT_GENERAL, 1u, &self_region,
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
        UINT64_C(2000000000)));
    const VkMappedMemoryRange readbacks[] = {
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, destination.memory, 0u,
            VK_WHOLE_SIZE},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, self.memory, 0u,
            VK_WHOLE_SIZE},
    };
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 2u, readbacks));

    int status = 0;
#if defined(OPENAGC_PROSPERO)
    uint32_t volume_pixels = 0u;
    uint32_t self_pixels = 0u;
    for (uint32_t z = 0u; z < VOLUME_DEPTH && !status; ++z) {
        for (uint32_t y = 0u; y < VOLUME_HEIGHT && !status; ++y) {
            const uint32_t *row = (const uint32_t *)(destination.mapped +
                destination_layout.offset + z * destination_layout.depthPitch +
                y * destination_layout.rowPitch);
            for (uint32_t x = 0u; x < VOLUME_WIDTH; ++x) {
                const uint32_t expected = volume_color(x, y, z);
                if (row[x] != expected) {
                    printf("blit3d: volume mismatch x=%u y=%u z=%u "
                        "got=%08x expected=%08x\n", x, y, z, row[x],
                        expected);
                    status = 1;
                    break;
                }
                volume_pixels++;
            }
        }
    }
    for (uint32_t y = 0u; y < SELF_HEIGHT / 2u; ++y) {
        const uint32_t *row = (const uint32_t *)(self.mapped +
            self_mip_layout.offset + y * self_mip_layout.rowPitch);
        for (uint32_t x = 0u; x < SELF_WIDTH / 2u; ++x) {
            const uint32_t expected = self_color(x * 2u + 1u, y * 2u + 1u);
            if (row[x] != expected) {
                printf("blit3d: self mismatch x=%u y=%u got=%08x "
                    "expected=%08x\n", x, y, row[x], expected);
                status = 1;
                continue;
            }
            self_pixels++;
        }
    }
    if (!status)
        printf("blit3d: PASS volume=%u self=%u\n", volume_pixels,
            self_pixels);
#else
    puts("blit3d: PASS command recording");
#endif

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkUnmapMemory(device, self.memory);
    vkUnmapMemory(device, destination.memory);
    vkUnmapMemory(device, source.memory);
    vkDestroyImage(device, self.image, NULL);
    vkDestroyImage(device, destination.image, NULL);
    vkDestroyImage(device, source.image, NULL);
    vkFreeMemory(device, self.memory, NULL);
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
    puts("blit3d: stage=start");
    const int status = run_probe();
    printf("blit3d: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("blit3d");
#endif
    return status;
}
