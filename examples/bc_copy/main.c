#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(OPENAGC_PROSPERO)
#include "../system_service_exit.h"
#endif

#define FORMAT_COUNT 14u
#define SOURCE_MIPS 2u
#define DESTINATION_MIPS 3u
#define COPY_REGION_COUNT 2u
#define BC_FEATURES (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | \
    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | \
    VK_FORMAT_FEATURE_TRANSFER_DST_BIT | \
    VK_FORMAT_FEATURE_BLIT_SRC_BIT)

typedef struct FormatCase {
    VkFormat format;
    const char *name;
    uint32_t block_size;
} FormatCase;

typedef struct TestImage {
    VkImage image;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize allocation_size;
    VkSubresourceLayout mips[DESTINATION_MIPS];
} TestImage;

static const FormatCase format_cases[FORMAT_COUNT] = {
    {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, "bc1_unorm", 8u},
    {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, "bc1_srgb", 8u},
    {VK_FORMAT_BC2_UNORM_BLOCK, "bc2_unorm", 16u},
    {VK_FORMAT_BC2_SRGB_BLOCK, "bc2_srgb", 16u},
    {VK_FORMAT_BC3_UNORM_BLOCK, "bc3_unorm", 16u},
    {VK_FORMAT_BC3_SRGB_BLOCK, "bc3_srgb", 16u},
    {VK_FORMAT_BC4_UNORM_BLOCK, "bc4_unorm", 8u},
    {VK_FORMAT_BC4_SNORM_BLOCK, "bc4_snorm", 8u},
    {VK_FORMAT_BC5_UNORM_BLOCK, "bc5_unorm", 16u},
    {VK_FORMAT_BC5_SNORM_BLOCK, "bc5_snorm", 16u},
    {VK_FORMAT_BC6H_UFLOAT_BLOCK, "bc6h_ufloat", 16u},
    {VK_FORMAT_BC6H_SFLOAT_BLOCK, "bc6h_sfloat", 16u},
    {VK_FORMAT_BC7_UNORM_BLOCK, "bc7_unorm", 16u},
    {VK_FORMAT_BC7_SRGB_BLOCK, "bc7_srgb", 16u},
};

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t index = 0u; index < properties.memoryTypeCount; ++index) {
        if ((compatible_types & (1u << index)) != 0u &&
            (properties.memoryTypes[index].propertyFlags &
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return index;
    }
    return UINT32_MAX;
}

static VkResult create_image(VkPhysicalDevice physical, VkDevice device,
    VkFormat format, uint32_t width, uint32_t height, uint32_t mip_count,
    TestImage *image)
{
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1u},
        .mipLevels = mip_count,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkResult result = vkCreateImage(device, &image_info, NULL, &image->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image->image, &requirements);
    const uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &memory_info, NULL, &image->memory);
    if (result == VK_SUCCESS)
        result = vkBindImageMemory(device, image->image, image->memory, 0u);
    if (result == VK_SUCCESS)
        result = vkMapMemory(device, image->memory, 0u, requirements.size, 0u,
            (void **)&image->mapped);
    if (result != VK_SUCCESS)
        return result;
    image->allocation_size = requirements.size;
    for (uint32_t mip = 0u; mip < mip_count; ++mip) {
        const VkImageSubresource subresource = {
            VK_IMAGE_ASPECT_COLOR_BIT, mip, 0u,
        };
        vkGetImageSubresourceLayout(device, image->image, &subresource,
            &image->mips[mip]);
    }
    return VK_SUCCESS;
}

static uint8_t pattern_byte(uint32_t format_index, uint32_t mip,
    uint32_t block, uint32_t byte)
{
    return (uint8_t)(0x19u + format_index * 23u + mip * 47u +
        block * 11u + byte * 3u);
}

static void write_source_mip(TestImage *source, uint32_t format_index,
    uint32_t mip, uint32_t blocks_wide, uint32_t blocks_high,
    uint32_t block_size)
{
    const VkSubresourceLayout *layout = &source->mips[mip];
    for (uint32_t y = 0u; y < blocks_high; ++y) {
        uint8_t *row = source->mapped + layout->offset + y * layout->rowPitch;
        for (uint32_t x = 0u; x < blocks_wide; ++x) {
            const uint32_t block = y * blocks_wide + x;
            for (uint32_t byte = 0u; byte < block_size; ++byte)
                row[x * block_size + byte] = pattern_byte(
                    format_index, mip, block, byte);
        }
    }
}

#if defined(OPENAGC_PROSPERO)
static int verify_destination_mip(const TestImage *destination,
    uint32_t format_index, uint32_t destination_mip, uint32_t source_mip,
    uint32_t blocks_wide, uint32_t blocks_high, uint32_t block_size)
{
    const VkSubresourceLayout *layout = &destination->mips[destination_mip];
    for (uint32_t y = 0u; y < blocks_high; ++y) {
        const uint8_t *row = destination->mapped + layout->offset +
            y * layout->rowPitch;
        for (uint32_t x = 0u; x < blocks_wide; ++x) {
            const uint32_t block = y * blocks_wide + x;
            for (uint32_t byte = 0u; byte < block_size; ++byte) {
                const uint8_t expected = pattern_byte(format_index,
                    source_mip, block, byte);
                if (row[x * block_size + byte] != expected) {
                    printf("bc_copy: mismatch format=%s dst_mip=%u "
                        "block=%u byte=%u got=%02x expected=%02x\n",
                        format_cases[format_index].name, destination_mip,
                        block, byte, row[x * block_size + byte], expected);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int verify_untouched_mip(const TestImage *destination,
    uint32_t format_index)
{
    const VkSubresourceLayout *layout = &destination->mips[0];
    const uint32_t block_size = format_cases[format_index].block_size;
    for (uint32_t y = 0u; y < 4u; ++y) {
        const uint8_t *row = destination->mapped + layout->offset +
            y * layout->rowPitch;
        for (uint32_t byte = 0u; byte < 4u * block_size; ++byte) {
            if (row[byte] != 0xa5u) {
                printf("bc_copy: untouched mip changed format=%s row=%u "
                    "byte=%u got=%02x\n", format_cases[format_index].name,
                    y, byte, row[byte]);
                return 0;
            }
        }
    }
    return 1;
}
#endif

static int run_probe(void)
{
    int status = 1;
    VkResult result = VK_SUCCESS;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    TestImage sources[FORMAT_COUNT] = {0};
    TestImage destinations[FORMAT_COUNT] = {0};

#define VK_TRY(expression) do { \
    result = (expression); \
    if (result != VK_SUCCESS) { \
        printf("bc_copy: %s failed (%d)\n", #expression, result); \
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
    if (physical_count != 1u) {
        puts("bc_copy: expected one physical device");
        goto cleanup;
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
    VK_TRY(vkCreateDevice(physical, &device_info, NULL, &device));

    VkMappedMemoryRange upload_ranges[FORMAT_COUNT * 2u];
    for (uint32_t index = 0u; index < FORMAT_COUNT; ++index) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical,
            format_cases[index].format, &properties);
        if (properties.linearTilingFeatures != BC_FEATURES ||
            properties.optimalTilingFeatures != BC_FEATURES ||
            properties.bufferFeatures != 0u) {
            printf("bc_copy: feature mismatch format=%s\n",
                format_cases[index].name);
            goto cleanup;
        }
        VK_TRY(create_image(physical, device, format_cases[index].format,
            8u, 8u, SOURCE_MIPS, &sources[index]));
        VK_TRY(create_image(physical, device, format_cases[index].format,
            16u, 16u, DESTINATION_MIPS, &destinations[index]));
        memset(sources[index].mapped, 0x31, sources[index].allocation_size);
        memset(destinations[index].mapped, 0xa5,
            destinations[index].allocation_size);
        write_source_mip(&sources[index], index, 0u, 2u, 2u,
            format_cases[index].block_size);
        write_source_mip(&sources[index], index, 1u, 1u, 1u,
            format_cases[index].block_size);
        upload_ranges[index * 2u] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = sources[index].memory,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        };
        upload_ranges[index * 2u + 1u] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = destinations[index].memory,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        };
    }
    VK_TRY(vkFlushMappedMemoryRanges(device, FORMAT_COUNT * 2u,
        upload_ranges));

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VK_TRY(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command = VK_NULL_HANDLE;
    VK_TRY(vkAllocateCommandBuffers(device, &allocation_info, &command));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_TRY(vkBeginCommandBuffer(command, &begin_info));

    VkImageMemoryBarrier to_copy[FORMAT_COUNT * 2u];
    VkImageMemoryBarrier to_host[FORMAT_COUNT];
    for (uint32_t index = 0u; index < FORMAT_COUNT; ++index) {
        const VkImageSubresourceRange source_range = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, SOURCE_MIPS, 0u, 1u,
        };
        const VkImageSubresourceRange destination_range = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, DESTINATION_MIPS, 0u, 1u,
        };
        to_copy[index * 2u] = (VkImageMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sources[index].image,
            .subresourceRange = source_range,
        };
        to_copy[index * 2u + 1u] = (VkImageMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = destinations[index].image,
            .subresourceRange = destination_range,
        };
        to_host[index] = (VkImageMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = destinations[index].image,
            .subresourceRange = destination_range,
        };
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL,
        FORMAT_COUNT * 2u, to_copy);
    const VkImageCopy copies[COPY_REGION_COUNT] = {
        {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 1u, 0u, 1u},
            .extent = {8u, 8u, 1u},
        },
        {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 1u, 0u, 1u},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 2u, 0u, 1u},
            .extent = {4u, 4u, 1u},
        },
    };
    for (uint32_t index = 0u; index < FORMAT_COUNT; ++index)
        vkCmdCopyImage(command, sources[index].image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destinations[index].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, COPY_REGION_COUNT, copies);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 0u, NULL,
        FORMAT_COUNT, to_host);
    VK_TRY(vkEndCommandBuffer(command));

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0u, 0u, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_TRY(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    VK_TRY(vkQueueSubmit(queue, 1u, &submit_info, fence));
    result = vkWaitForFences(device, 1u, &fence, VK_TRUE,
        UINT64_C(2000000000));
    if (result != VK_SUCCESS) {
        printf("bc_copy: two-second fence wait failed (%d)\n", result);
        goto cleanup;
    }
    VkMappedMemoryRange download_ranges[FORMAT_COUNT];
    for (uint32_t index = 0u; index < FORMAT_COUNT; ++index) {
        download_ranges[index] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = destinations[index].memory,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        };
    }
    VK_TRY(vkInvalidateMappedMemoryRanges(device, FORMAT_COUNT,
        download_ranges));

#if defined(OPENAGC_PROSPERO)
    for (uint32_t index = 0u; index < FORMAT_COUNT; ++index) {
        if (!verify_untouched_mip(&destinations[index], index) ||
            !verify_destination_mip(&destinations[index], index,
                1u, 0u, 2u, 2u, format_cases[index].block_size) ||
            !verify_destination_mip(&destinations[index], index,
                2u, 1u, 1u, 1u, format_cases[index].block_size))
            goto cleanup;
    }
    puts("bc_copy: PASS formats=14 regions=28 exact-bytes mips=2");
#else
    puts("bc_copy: PASS command recording formats=14 regions=28");
#endif
    status = 0;

cleanup:
    if (device != VK_NULL_HANDLE) {
        if (fence != VK_NULL_HANDLE)
            vkDestroyFence(device, fence, NULL);
        if (pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, pool, NULL);
        for (uint32_t index = 0u; index < FORMAT_COUNT; ++index) {
            if (sources[index].mapped)
                vkUnmapMemory(device, sources[index].memory);
            if (destinations[index].mapped)
                vkUnmapMemory(device, destinations[index].memory);
            if (sources[index].image != VK_NULL_HANDLE)
                vkDestroyImage(device, sources[index].image, NULL);
            if (destinations[index].image != VK_NULL_HANDLE)
                vkDestroyImage(device, destinations[index].image, NULL);
            if (sources[index].memory != VK_NULL_HANDLE)
                vkFreeMemory(device, sources[index].memory, NULL);
            if (destinations[index].memory != VK_NULL_HANDLE)
                vkFreeMemory(device, destinations[index].memory, NULL);
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
    puts("bc_copy: stage=start");
    const int status = run_probe();
    printf("bc_copy: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("bc_copy");
#endif
    return status;
}
