#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../system_service_exit.h"

#define IMAGE_WIDTH 8u
#define IMAGE_HEIGHT 8u
#define FORMAT_COUNT 5u

typedef struct DepthFormatCase {
    VkFormat format;
    const char *name;
    VkImageAspectFlags aspects;
    uint32_t depth_bytes;
    uint32_t depth_value;
} DepthFormatCase;

typedef struct TestImage {
    VkImage image;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize allocation_size;
    VkSubresourceLayout depth_layout;
    VkSubresourceLayout stencil_layout;
} TestImage;

static const DepthFormatCase format_cases[FORMAT_COUNT] = {
    {VK_FORMAT_D16_UNORM, "d16", VK_IMAGE_ASPECT_DEPTH_BIT,
        2u, UINT32_C(0x4000)},
    {VK_FORMAT_D32_SFLOAT, "d32", VK_IMAGE_ASPECT_DEPTH_BIT,
        4u, UINT32_C(0x3e800000)},
    {VK_FORMAT_S8_UINT, "s8", VK_IMAGE_ASPECT_STENCIL_BIT, 0u, 0u},
    {VK_FORMAT_D16_UNORM_S8_UINT, "d16s8",
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        2u, UINT32_C(0x4000)},
    {VK_FORMAT_D32_SFLOAT_S8_UINT, "d32s8",
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        4u, UINT32_C(0x3e800000)},
};

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0u; i < properties.memoryTypeCount; ++i) {
        if ((compatible_types & (1u << i)) != 0u &&
            (properties.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return i;
    }
    return UINT32_MAX;
}

static VkResult create_image(VkPhysicalDevice physical, VkDevice device,
    const DepthFormatCase *format_case, TestImage *test_image)
{
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format_case->format,
        .extent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkResult result = vkCreateImage(device, &image_info, NULL,
        &test_image->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, test_image->image, &requirements);
    const uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &allocation, NULL, &test_image->memory);
    if (result == VK_SUCCESS)
        result = vkBindImageMemory(device, test_image->image,
            test_image->memory, 0u);
    if (result == VK_SUCCESS)
        result = vkMapMemory(device, test_image->memory, 0u,
            requirements.size, 0u, (void **)&test_image->mapped);
    if (result != VK_SUCCESS)
        return result;
    test_image->allocation_size = requirements.size;
    memset(test_image->mapped, 0xa5, requirements.size);
    if ((format_case->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u) {
        const VkImageSubresource subresource = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        };
        vkGetImageSubresourceLayout(device, test_image->image, &subresource,
            &test_image->depth_layout);
    }
    if ((format_case->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u) {
        const VkImageSubresource subresource = {
            .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
        };
        vkGetImageSubresourceLayout(device, test_image->image, &subresource,
            &test_image->stencil_layout);
    }
    return VK_SUCCESS;
}

#if defined(OPENAGC_PROSPERO)
static bool check_depth(const TestImage *image,
    const DepthFormatCase *format_case, uint32_t *checked)
{
    if ((format_case->aspects & VK_IMAGE_ASPECT_DEPTH_BIT) == 0u)
        return true;
    for (uint32_t y = 0u; y < IMAGE_HEIGHT; ++y) {
        const uint8_t *row = image->mapped + image->depth_layout.offset +
            y * image->depth_layout.rowPitch;
        for (uint32_t x = 0u; x < IMAGE_WIDTH; ++x) {
            uint32_t actual = 0u;
            memcpy(&actual, row + x * format_case->depth_bytes,
                format_case->depth_bytes);
            if (actual != format_case->depth_value) {
                printf("depth_clear: mismatch format=%s aspect=depth "
                    "x=%u y=%u got=%08x expected=%08x\n",
                    format_case->name, x, y, actual,
                    format_case->depth_value);
                return false;
            }
            ++*checked;
        }
    }
    return true;
}

static bool check_stencil(const TestImage *image,
    const DepthFormatCase *format_case, uint32_t *checked)
{
    if ((format_case->aspects & VK_IMAGE_ASPECT_STENCIL_BIT) == 0u)
        return true;
    for (uint32_t y = 0u; y < IMAGE_HEIGHT; ++y) {
        const uint8_t *row = image->mapped + image->stencil_layout.offset +
            y * image->stencil_layout.rowPitch;
        for (uint32_t x = 0u; x < IMAGE_WIDTH; ++x) {
            const uint8_t actual = row[x];
            if (actual != UINT8_C(0x5a)) {
                printf("depth_clear: mismatch format=%s aspect=stencil "
                    "x=%u y=%u got=%02x expected=5a\n",
                    format_case->name, x, y, actual);
                return false;
            }
            ++*checked;
        }
    }
    return true;
}
#endif

static int run_probe(void)
{
    int status = 1;
    VkResult result;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    TestImage images[FORMAT_COUNT] = {0};

#define VK_TRY(expression) do { \
    result = (expression); \
    if (result != VK_SUCCESS) { \
        printf("depth_clear: %s failed (%d)\n", #expression, result); \
        goto cleanup; \
    } \
} while (0)

    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_TRY(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t physical_count = 1u;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VK_TRY(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u)
        goto cleanup;
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
    VK_TRY(vkCreateDevice(physical, &device_info, NULL, &device));

    VkMappedMemoryRange mapped_ranges[FORMAT_COUNT];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        result = create_image(physical, device, &format_cases[i], &images[i]);
        if (result != VK_SUCCESS) {
            printf("depth_clear: create %s failed (%d)\n",
                format_cases[i].name, result);
            goto cleanup;
        }
        mapped_ranges[i] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = images[i].memory,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        };
    }
    VK_TRY(vkFlushMappedMemoryRanges(device, FORMAT_COUNT, mapped_ranges));
    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VK_TRY(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FORMAT_COUNT,
    };
    VkCommandBuffer commands[FORMAT_COUNT] = {0};
    VK_TRY(vkAllocateCommandBuffers(device, &allocation, commands));
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        VK_TRY(vkBeginCommandBuffer(commands[i], &begin));
        const VkImageSubresourceRange range = {
            format_cases[i].aspects, 0u, 1u, 0u, 1u,
        };
        const VkImageMemoryBarrier to_clear = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].image,
            .subresourceRange = range,
        };
        const VkImageMemoryBarrier to_host = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].image,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL,
            1u, &to_clear);
        const VkClearDepthStencilValue clear = {0.25f, UINT32_C(0x5a)};
        vkCmdClearDepthStencilImage(commands[i], images[i].image,
            VK_IMAGE_LAYOUT_GENERAL, &clear, 1u, &range);
        vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 0u, NULL,
            1u, &to_host);
        result = vkEndCommandBuffer(commands[i]);
        if (result != VK_SUCCESS) {
            printf("depth_clear: record %s failed (%d)\n",
                format_cases[i].name, result);
            goto cleanup;
        }
    }
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0u, 0u, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_TRY(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = FORMAT_COUNT,
        .pCommandBuffers = commands,
    };
    VK_TRY(vkQueueSubmit(queue, 1u, &submit, fence));
    result = vkWaitForFences(device, 1u, &fence, VK_TRUE,
        UINT64_C(2000000000));
    if (result != VK_SUCCESS) {
        printf("depth_clear: two-second fence wait failed (%d)\n", result);
        goto cleanup;
    }
    VK_TRY(vkInvalidateMappedMemoryRanges(device, FORMAT_COUNT,
        mapped_ranges));
#if defined(OPENAGC_PROSPERO)
    uint32_t depth_checked = 0u;
    uint32_t stencil_checked = 0u;
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        if (!check_depth(&images[i], &format_cases[i], &depth_checked) ||
            !check_stencil(&images[i], &format_cases[i], &stencil_checked))
            goto cleanup;
    }
    printf("depth_clear: PASS formats=%u depth=%u stencil=%u exact-bits\n",
        FORMAT_COUNT, depth_checked, stencil_checked);
#else
    puts("depth_clear: PASS command recording");
#endif
    status = 0;

cleanup:
    if (device != VK_NULL_HANDLE) {
        if (fence != VK_NULL_HANDLE)
            vkDestroyFence(device, fence, NULL);
        if (pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, pool, NULL);
        for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
            if (images[i].mapped)
                vkUnmapMemory(device, images[i].memory);
            if (images[i].image != VK_NULL_HANDLE)
                vkDestroyImage(device, images[i].image, NULL);
            if (images[i].memory != VK_NULL_HANDLE)
                vkFreeMemory(device, images[i].memory, NULL);
        }
        vkDestroyDevice(device, NULL);
    }
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, NULL);
    return status;

#undef VK_TRY
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    puts("depth_clear: stage=start");
    const int status = run_probe();
    printf("depth_clear: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("depth_clear");
#endif
    return status;
}
