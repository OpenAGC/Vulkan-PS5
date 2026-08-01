#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../system_service_exit.h"

#define IMAGE_WIDTH 8u
#define IMAGE_HEIGHT 8u
#define FORMAT_COUNT 37u
#define INTEGER_FEATURES (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
    VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | \
    VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | \
    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | \
    VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
#define NORMALIZED_FEATURES (INTEGER_FEATURES | \
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | \
    VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)
#define NORMALIZED_NO_STORAGE_FEATURES (NORMALIZED_FEATURES & \
    ~VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
#define SAMPLED_ONLY_FEATURES (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | \
    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | \
    VK_FORMAT_FEATURE_BLIT_SRC_BIT)

typedef struct FormatCase {
    VkFormat format;
    const char *name;
    VkClearColorValue clear;
    uint32_t expected[4];
    uint32_t byte_count;
    VkFormatFeatureFlags features;
} FormatCase;

typedef struct TestImage {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    uint8_t *mapped;
    uint8_t *pixels;
    VkDeviceSize allocation_size;
    VkDeviceSize row_pitch;
} TestImage;

static const FormatCase format_cases[FORMAT_COUNT] = {
    {
        VK_FORMAT_R8_UNORM, "r8_unorm",
        {.float32 = {0.25f, 0.0f, 0.0f, 0.0f}},
        {UINT32_C(0x00000040), 0u, 0u, 0u}, 1u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R8_SNORM, "r8_snorm",
        {.float32 = {0.5f, 0.0f, 0.0f, 0.0f}},
        {UINT32_C(0x00000040), 0u, 0u, 0u}, 1u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R8_UINT, "r8_uint",
        {.uint32 = {0xabu, 0u, 0u, 0u}},
        {UINT32_C(0x000000ab), 0u, 0u, 0u}, 1u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R8_SINT, "r8_sint",
        {.int32 = {-2, 0, 0, 0}},
        {UINT32_C(0x000000fe), 0u, 0u, 0u}, 1u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R8G8_UNORM, "rg8_unorm",
        {.float32 = {0.25f, 0.75f, 0.0f, 0.0f}},
        {UINT32_C(0x0000bf40), 0u, 0u, 0u}, 2u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R8G8_SNORM, "rg8_snorm",
        {.float32 = {0.5f, -0.5f, 0.0f, 0.0f}},
        {UINT32_C(0x0000c040), 0u, 0u, 0u}, 2u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R8G8_UINT, "rg8_uint",
        {.uint32 = {0x34u, 0xcdu, 0u, 0u}},
        {UINT32_C(0x0000cd34), 0u, 0u, 0u}, 2u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R8G8_SINT, "rg8_sint",
        {.int32 = {-2, 123, 0, 0}},
        {UINT32_C(0x00007bfe), 0u, 0u, 0u}, 2u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_A8B8G8R8_SNORM_PACK32, "rgba8_snorm",
        {.float32 = {0.5f, -0.5f, 1.0f, -1.0f}},
        {UINT32_C(0x807fc040), 0u, 0u, 0u}, 4u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_A8B8G8R8_UINT_PACK32, "rgba8_uint",
        {.uint32 = {0x12u, 0x34u, 0x56u, 0x78u}},
        {UINT32_C(0x78563412), 0u, 0u, 0u}, 4u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_A8B8G8R8_SINT_PACK32, "rgba8_sint",
        {.int32 = {-1, -2, 0x34, -128}},
        {UINT32_C(0x8034feff), 0u, 0u, 0u}, 4u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_A2B10G10R10_UINT_PACK32, "rgb10a2_uint",
        {.uint32 = {0x123u, 0x234u, 0x345u, 2u}},
        {UINT32_C(0xb458d123), 0u, 0u, 0u}, 4u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_A2R10G10B10_UNORM_PACK32, "bgr10a2_unorm",
        {.float32 = {0.25f, 0.5f, 0.75f, 1.0f}},
        {UINT32_C(0xd00802ff), 0u, 0u, 0u}, 4u,
        NORMALIZED_NO_STORAGE_FEATURES,
    },
    {
        VK_FORMAT_R5G6B5_UNORM_PACK16, "r5g6b5_unorm",
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x000087e0), 0u, 0u, 0u}, 2u,
        NORMALIZED_NO_STORAGE_FEATURES,
    },
    {
        VK_FORMAT_B5G6R5_UNORM_PACK16, "b5g6r5_unorm",
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x000007f0), 0u, 0u, 0u}, 2u,
        NORMALIZED_NO_STORAGE_FEATURES,
    },
    {
        VK_FORMAT_R5G5B5A1_UNORM_PACK16, "r5g5b5a1_unorm",
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x000087c1), 0u, 0u, 0u}, 2u,
        NORMALIZED_NO_STORAGE_FEATURES,
    },
    {
        VK_FORMAT_A1R5G5B5_UNORM_PACK16, "a1r5g5b5_unorm",
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x0000c3e0), 0u, 0u, 0u}, 2u,
        NORMALIZED_NO_STORAGE_FEATURES,
    },
    {
        VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT, "a4b4g4r4_unorm",
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x0000f0f8), 0u, 0u, 0u}, 2u,
        NORMALIZED_NO_STORAGE_FEATURES,
    },
    {
        VK_FORMAT_R4G4_UNORM_PACK8, "r4g4_unorm",
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x000000f8), 0u, 0u, 0u}, 1u,
        SAMPLED_ONLY_FEATURES,
    },
    {
        VK_FORMAT_R16_UNORM, "r16_unorm",
        {.float32 = {0.25f, 0.0f, 0.0f, 0.0f}},
        {UINT32_C(0x00004000), 0u, 0u, 0u}, 2u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R16_SNORM, "r16_snorm",
        {.float32 = {0.5f, 0.0f, 0.0f, 0.0f}},
        {UINT32_C(0x00004000), 0u, 0u, 0u}, 2u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R16_UINT, "r16_uint",
        {.uint32 = {0x1234u, 0u, 0u, 0u}},
        {UINT32_C(0x00001234), 0u, 0u, 0u}, 2u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R16_SINT, "r16_sint",
        {.int32 = {-2, 0, 0, 0}},
        {UINT32_C(0x0000fffe), 0u, 0u, 0u}, 2u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R16G16_UNORM, "rg16_unorm",
        {.float32 = {0.25f, 0.75f, 0.0f, 0.0f}},
        {UINT32_C(0xbfff4000), 0u, 0u, 0u}, 4u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R16G16_SNORM, "rg16_snorm",
        {.float32 = {0.5f, -0.5f, 0.0f, 0.0f}},
        {UINT32_C(0xc0004000), 0u, 0u, 0u}, 4u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R16G16_UINT, "rg16_uint",
        {.uint32 = {0x1234u, 0xabcdu, 0u, 0u}},
        {UINT32_C(0xabcd1234), 0u, 0u, 0u}, 4u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R16G16_SINT, "rg16_sint",
        {.int32 = {-2, 12345, 0, 0}},
        {UINT32_C(0x3039fffe), 0u, 0u, 0u}, 4u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R16G16B16A16_UNORM, "rgba16_unorm",
        {.float32 = {0.0f, 0.25f, 0.5f, 1.0f}},
        {UINT32_C(0x40000000), UINT32_C(0xffff8000), 0u, 0u},
        8u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R16G16B16A16_SNORM, "rgba16_snorm",
        {.float32 = {-1.0f, -0.5f, 0.5f, 1.0f}},
        {UINT32_C(0xc0008000), UINT32_C(0x7fff4000), 0u, 0u},
        8u, NORMALIZED_FEATURES,
    },
    {
        VK_FORMAT_R16G16B16A16_UINT, "rgba16_uint",
        {.uint32 = {0x0123u, 0x4567u, 0x89abu, 0xcdefu}},
        {UINT32_C(0x45670123), UINT32_C(0xcdef89ab), 0u, 0u},
        8u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R16G16B16A16_SINT, "rgba16_sint",
        {.int32 = {-1, -32768, 12345, -23456}},
        {UINT32_C(0x8000ffff), UINT32_C(0xa4603039), 0u, 0u},
        8u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R32_UINT, "r32_uint",
        {.uint32 = {UINT32_C(0x89abcdef), 0u, 0u, 0u}},
        {UINT32_C(0x89abcdef), 0u, 0u, 0u}, 4u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R32_SINT, "r32_sint",
        {.int32 = {-987654321, 0, 0, 0}},
        {UINT32_C(0xc521974f), 0u, 0u, 0u}, 4u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R32G32_UINT, "rg32_uint",
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 0u}},
        {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 0u},
        8u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R32G32_SINT, "rg32_sint",
        {.int32 = {-1, INT32_MIN, 0, 0}},
        {UINT32_C(0xffffffff), UINT32_C(0x80000000), 0u, 0u},
        8u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R32G32B32A32_UINT, "rgba32_uint",
        {.uint32 = {
            UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531),
        }},
        {
            UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531),
        }, 16u, INTEGER_FEATURES,
    },
    {
        VK_FORMAT_R32G32B32A32_SINT, "rgba32_sint",
        {.int32 = {-1, INT32_MIN, 123456789, -987654321}},
        {
            UINT32_C(0xffffffff), UINT32_C(0x80000000),
            UINT32_C(0x075bcd15), UINT32_C(0xc521974f),
        }, 16u, INTEGER_FEATURES,
    },
};

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
    const FormatCase *format_case, TestImage *test_image)
{
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical, format_case->format,
        &properties);
    if (properties.linearTilingFeatures != format_case->features)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format_case->format,
        .extent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT |
            ((format_case->features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ?
                VK_IMAGE_USAGE_STORAGE_BIT : 0u) |
            ((format_case->features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ?
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0u) |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &memory_info, NULL, &test_image->memory);
    if (result == VK_SUCCESS)
        result = vkBindImageMemory(device, test_image->image,
            test_image->memory, 0u);
    if (result == VK_SUCCESS)
        result = vkMapMemory(device, test_image->memory, 0u,
            requirements.size, 0u, (void **)&test_image->mapped);
    if (result != VK_SUCCESS)
        return result;

    const VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, test_image->image, &subresource,
        &layout);
    test_image->pixels = test_image->mapped + layout.offset;
    test_image->allocation_size = requirements.size;
    test_image->row_pitch = layout.rowPitch;
    memset(test_image->mapped, 0xa5, requirements.size);

    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = test_image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format_case->format,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        },
    };
    return vkCreateImageView(device, &view_info, NULL, &test_image->view);
}

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
        printf("integer_formats: %s failed (%d)\n", #expression, result); \
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
        printf("integer_formats: expected one physical device\n");
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

    VkMappedMemoryRange upload_ranges[FORMAT_COUNT];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        result = create_image(physical, device, &format_cases[i], &images[i]);
        if (result != VK_SUCCESS) {
            printf("integer_formats: create %s failed (%d)\n",
                format_cases[i].name, result);
            goto cleanup;
        }
        upload_ranges[i] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = images[i].memory,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        };
    }
    VK_TRY(vkFlushMappedMemoryRanges(device, FORMAT_COUNT, upload_ranges));

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VK_TRY(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command = VK_NULL_HANDLE;
    VK_TRY(vkAllocateCommandBuffers(device, &command_info, &command));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_TRY(vkBeginCommandBuffer(command, &begin_info));

    VkImageMemoryBarrier to_clear[FORMAT_COUNT];
    VkImageMemoryBarrier to_host[FORMAT_COUNT];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        const VkImageSubresourceRange range = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        };
        to_clear[i] = (VkImageMemoryBarrier) {
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
        to_host[i] = (VkImageMemoryBarrier) {
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
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL,
        FORMAT_COUNT, to_clear);
    const VkImageSubresourceRange clear_range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i)
        vkCmdClearColorImage(command, images[i].image,
            VK_IMAGE_LAYOUT_GENERAL, &format_cases[i].clear, 1u,
            &clear_range);
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
        printf("integer_formats: two-second fence wait failed (%d)\n", result);
        return 1;
    }
    VK_TRY(vkInvalidateMappedMemoryRanges(device, FORMAT_COUNT,
        upload_ranges));

#if defined(OPENAGC_PROSPERO)
    uint32_t checked = 0u;
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        const size_t pixel_size = format_cases[i].byte_count;
        for (uint32_t y = 0u; y < IMAGE_HEIGHT; ++y) {
            const uint8_t *row = images[i].pixels + y * images[i].row_pitch;
            for (uint32_t x = 0u; x < IMAGE_WIDTH; ++x) {
                const uint8_t *pixel = row + x * pixel_size;
                if (memcmp(pixel, format_cases[i].expected, pixel_size) != 0) {
                    uint32_t actual[4] = {0};
                    memcpy(actual, pixel, pixel_size);
                    printf("integer_formats: mismatch format=%s x=%u y=%u "
                        "got=%08x,%08x,%08x,%08x\n",
                        format_cases[i].name, x, y, actual[0], actual[1],
                        actual[2], actual[3]);
                    goto cleanup;
                }
                ++checked;
            }
        }
    }
    printf("integer_formats: PASS formats=%u pixels=%u exact-bits\n",
        FORMAT_COUNT, checked);
#else
    puts("integer_formats: PASS command recording");
#endif
    status = 0;

cleanup:
    if (device != VK_NULL_HANDLE) {
        if (fence != VK_NULL_HANDLE)
            vkDestroyFence(device, fence, NULL);
        if (pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, pool, NULL);
        for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
            if (images[i].view != VK_NULL_HANDLE)
                vkDestroyImageView(device, images[i].view, NULL);
            if (images[i].mapped != NULL)
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
    puts("integer_formats: stage=start");
    const int status = run_probe();
    printf("integer_formats: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("integer_formats");
#endif
    return status;
}
