#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_ps5_format_attachment_float_spv.h"
#include "vulkan_ps5_format_attachment_sint_spv.h"
#include "vulkan_ps5_format_attachment_uint_spv.h"
#include "vulkan_ps5_format_attachment_vert_spv.h"

#include "../system_service_exit.h"

#define FORMAT_COUNT 36u
#define FRAGMENT_CLASS_COUNT 3u
#define CLEAR_ATTACHMENT_WIDTH 1280u
#define CLEAR_ATTACHMENT_HEIGHT 720u

typedef enum NumericClass {
    NUMERIC_FLOAT = 0,
    NUMERIC_UINT = 1,
    NUMERIC_SINT = 2,
} NumericClass;

typedef struct FormatCase {
    VkFormat format;
    const char *name;
    NumericClass numeric_class;
    VkClearColorValue value;
    uint32_t expected[4];
    uint32_t byte_count;
} FormatCase;

typedef struct TestImage {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    uint8_t *mapped;
    uint8_t *pixel;
    VkDeviceSize allocation_size;
} TestImage;

static const FormatCase format_cases[FORMAT_COUNT] = {
    {VK_FORMAT_R8_UNORM, "r8_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x40), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8_SNORM, "r8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x40), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8_UINT, "r8_uint", NUMERIC_UINT,
        {.uint32 = {0xabu, 0u, 0u, 1u}},
        {UINT32_C(0xab), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8_SINT, "r8_sint", NUMERIC_SINT,
        {.int32 = {-2, 0, 0, 1}},
        {UINT32_C(0xfe), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8G8_UNORM, "rg8_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.75f, 0.0f, 1.0f}},
        {UINT32_C(0xbf40), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R8G8_SNORM, "rg8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 0.0f, 1.0f}},
        {UINT32_C(0xc040), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R8G8_UINT, "rg8_uint", NUMERIC_UINT,
        {.uint32 = {0x34u, 0xcdu, 0u, 1u}},
        {UINT32_C(0xcd34), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R8G8_SINT, "rg8_sint", NUMERIC_SINT,
        {.int32 = {-2, 123, 0, 1}},
        {UINT32_C(0x7bfe), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_A8B8G8R8_SNORM_PACK32, "rgba8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 1.0f, -1.0f}},
        {UINT32_C(0x817fc040), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_A8B8G8R8_UINT_PACK32, "rgba8_uint", NUMERIC_UINT,
        {.uint32 = {0x12u, 0x34u, 0x56u, 0x78u}},
        {UINT32_C(0x78563412), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_A8B8G8R8_SINT_PACK32, "rgba8_sint", NUMERIC_SINT,
        {.int32 = {-1, -2, 0x34, -128}},
        {UINT32_C(0x8034feff), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_A2B10G10R10_UINT_PACK32, "rgb10a2_uint", NUMERIC_UINT,
        {.uint32 = {0x123u, 0x234u, 0x345u, 2u}},
        {UINT32_C(0xb458d123), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_A2R10G10B10_UNORM_PACK32, "bgr10a2_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.5f, 0.75f, 1.0f}},
        {UINT32_C(0xd00802ff), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R5G6B5_UNORM_PACK16, "r5g6b5_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x87e0), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_B5G6R5_UNORM_PACK16, "b5g6r5_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x07f0), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R5G5B5A1_UNORM_PACK16, "r5g5b5a1_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0x87c1), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_A1R5G5B5_UNORM_PACK16, "a1r5g5b5_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0xc3e0), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT, "a4b4g4r4_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {UINT32_C(0xf0f8), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16_UNORM, "r16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x4000), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16_SNORM, "r16_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x4000), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16_UINT, "r16_uint", NUMERIC_UINT,
        {.uint32 = {0x1234u, 0u, 0u, 1u}},
        {UINT32_C(0x1234), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16_SINT, "r16_sint", NUMERIC_SINT,
        {.int32 = {-2, 0, 0, 1}},
        {UINT32_C(0xfffe), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16G16_UNORM, "rg16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.75f, 0.0f, 1.0f}},
        {UINT32_C(0xbfff4000), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16_SNORM, "rg16_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 0.0f, 1.0f}},
        {UINT32_C(0xc0004000), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16_UINT, "rg16_uint", NUMERIC_UINT,
        {.uint32 = {0x1234u, 0xabcdu, 0u, 1u}},
        {UINT32_C(0xabcd1234), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16_SINT, "rg16_sint", NUMERIC_SINT,
        {.int32 = {-2, 12345, 0, 1}},
        {UINT32_C(0x3039fffe), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16B16A16_UNORM, "rgba16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.0f, 0.25f, 0.5f, 1.0f}},
        {UINT32_C(0x40000000), UINT32_C(0xffff8000), 0u, 0u}, 8u},
    {VK_FORMAT_R16G16B16A16_SNORM, "rgba16_snorm", NUMERIC_FLOAT,
        {.float32 = {-1.0f, -0.5f, 0.5f, 1.0f}},
        {UINT32_C(0xc0008001), UINT32_C(0x7fff4000), 0u, 0u}, 8u},
    {VK_FORMAT_R16G16B16A16_UINT, "rgba16_uint", NUMERIC_UINT,
        {.uint32 = {0x0123u, 0x4567u, 0x89abu, 0xcdefu}},
        {UINT32_C(0x45670123), UINT32_C(0xcdef89ab), 0u, 0u}, 8u},
    {VK_FORMAT_R16G16B16A16_SINT, "rgba16_sint", NUMERIC_SINT,
        {.int32 = {-1, -32768, 12345, -23456}},
        {UINT32_C(0x8000ffff), UINT32_C(0xa4603039), 0u, 0u}, 8u},
    {VK_FORMAT_R32_UINT, "r32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x89abcdef), 0u, 0u, 1u}},
        {UINT32_C(0x89abcdef), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R32_SINT, "r32_sint", NUMERIC_SINT,
        {.int32 = {-987654321, 0, 0, 1}},
        {UINT32_C(0xc521974f), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R32G32_UINT, "rg32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 1u}},
        {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 0u}, 8u},
    {VK_FORMAT_R32G32_SINT, "rg32_sint", NUMERIC_SINT,
        {.int32 = {-1, INT32_MIN, 0, 1}},
        {UINT32_C(0xffffffff), UINT32_C(0x80000000), 0u, 0u}, 8u},
    {VK_FORMAT_R32G32B32A32_UINT, "rgba32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531)}},
        {UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531)}, 16u},
    {VK_FORMAT_R32G32B32A32_SINT, "rgba32_sint", NUMERIC_SINT,
        {.int32 = {-1, INT32_MIN, 123456789, -987654321}},
        {UINT32_C(0xffffffff), UINT32_C(0x80000000),
            UINT32_C(0x075bcd15), UINT32_C(0xc521974f)}, 16u},
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

#if defined(OPENAGC_PROSPERO)
static uint32_t find_device_local_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0u; i < properties.memoryTypeCount; ++i) {
        if ((compatible_types & (1u << i)) != 0u &&
            (properties.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0u)
            return i;
    }
    return UINT32_MAX;
}

static int run_clear_attachments_regression(VkPhysicalDevice physical,
                                            VkDevice device, VkQueue queue)
{
    int status = 1;
    VkResult result;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    void *mapped = NULL;
    const VkDeviceSize readback_size =
        (VkDeviceSize)CLEAR_ATTACHMENT_WIDTH * CLEAR_ATTACHMENT_HEIGHT * 4u;

#define CLEAR_TRY(expression) do { \
    result = (expression); \
    if (result != VK_SUCCESS) { \
        printf("format_attachments: clear_attachment %s failed (%d)\\n", \
            #expression, result); \
        goto cleanup; \
    } \
} while (0)

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {CLEAR_ATTACHMENT_WIDTH, CLEAR_ATTACHMENT_HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    CLEAR_TRY(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements image_requirements;
    vkGetImageMemoryRequirements(device, image, &image_requirements);
    const uint32_t image_memory_type = find_device_local_memory_type(
        physical, image_requirements.memoryTypeBits);
    if (image_memory_type == UINT32_MAX) {
        printf("format_attachments: clear_attachment has no device-local image memory\\n");
        goto cleanup;
    }
    const VkMemoryAllocateInfo image_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = image_memory_type,
    };
    CLEAR_TRY(vkAllocateMemory(device, &image_allocation, NULL, &image_memory));
    CLEAR_TRY(vkBindImageMemory(device, image, image_memory, 0u));

    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        },
    };
    CLEAR_TRY(vkCreateImageView(device, &view_info, NULL, &view));

    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference color_reference = {
        .attachment = 0u,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &color_reference,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1u,
        .pAttachments = &attachment,
        .subpassCount = 1u,
        .pSubpasses = &subpass,
    };
    CLEAR_TRY(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1u,
        .pAttachments = &view,
        .width = CLEAR_ATTACHMENT_WIDTH,
        .height = CLEAR_ATTACHMENT_HEIGHT,
        .layers = 1u,
    };
    CLEAR_TRY(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));

    const VkBufferCreateInfo readback_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = readback_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    CLEAR_TRY(vkCreateBuffer(device, &readback_info, NULL, &readback));
    VkMemoryRequirements readback_requirements;
    vkGetBufferMemoryRequirements(device, readback, &readback_requirements);
    const uint32_t readback_memory_type = find_host_visible_memory_type(
        physical, readback_requirements.memoryTypeBits);
    if (readback_memory_type == UINT32_MAX) {
        printf("format_attachments: clear_attachment has no host-visible readback memory\\n");
        goto cleanup;
    }
    const VkMemoryAllocateInfo readback_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = readback_requirements.size,
        .memoryTypeIndex = readback_memory_type,
    };
    CLEAR_TRY(vkAllocateMemory(device, &readback_allocation, NULL, &readback_memory));
    CLEAR_TRY(vkBindBufferMemory(device, readback, readback_memory, 0u));
    CLEAR_TRY(vkMapMemory(device, readback_memory, 0u, readback_size, 0u, &mapped));

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    CLEAR_TRY(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    CLEAR_TRY(vkAllocateCommandBuffers(device, &command_info, &command));
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    CLEAR_TRY(vkCreateFence(device, &fence_info, NULL, &fence));

    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    CLEAR_TRY(vkBeginCommandBuffer(command, &begin_info));
    const VkImageSubresourceRange image_range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    const VkRenderPassBeginInfo begin_render_pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {CLEAR_ATTACHMENT_WIDTH, CLEAR_ATTACHMENT_HEIGHT}},
    };
    const VkClearAttachment clear_attachment = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0u,
        .clearValue.color.float32 = {1.0f, 0.0f, 1.0f, 1.0f},
    };
    const VkClearRect clear_rect = {
        .rect = {{0, 0}, {CLEAR_ATTACHMENT_WIDTH, CLEAR_ATTACHMENT_HEIGHT}},
        .baseArrayLayer = 0u,
        .layerCount = 1u,
    };
    vkCmdBeginRenderPass(command, &begin_render_pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdClearAttachments(command, 1u, &clear_attachment, 1u, &clear_rect);
    vkCmdEndRenderPass(command);
    const VkImageMemoryBarrier to_transfer_source = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = image_range,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL, 1u,
        &to_transfer_source);
    const VkBufferImageCopy copy = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .imageExtent = {CLEAR_ATTACHMENT_WIDTH, CLEAR_ATTACHMENT_HEIGHT, 1u},
    };
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readback, 1u, &copy);
    const VkBufferMemoryBarrier to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = readback,
        .offset = 0u,
        .size = readback_size,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 1u, &to_host, 0u, NULL);
    CLEAR_TRY(vkEndCommandBuffer(command));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    CLEAR_TRY(vkQueueSubmit(queue, 1u, &submit_info, fence));
    CLEAR_TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    const VkMappedMemoryRange invalidate = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = readback_memory,
        .size = readback_size,
    };
    CLEAR_TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));

    const uint8_t expected[] = {0xffu, 0x00u, 0xffu, 0xffu};
    const uint32_t sample_offsets[] = {
        0u,
        ((CLEAR_ATTACHMENT_HEIGHT / 2u) * CLEAR_ATTACHMENT_WIDTH +
         CLEAR_ATTACHMENT_WIDTH / 2u) * 4u,
        (CLEAR_ATTACHMENT_WIDTH * CLEAR_ATTACHMENT_HEIGHT - 1u) * 4u,
    };
    for (uint32_t i = 0u; i < sizeof(sample_offsets) / sizeof(sample_offsets[0]); ++i) {
        const uint8_t *pixel = (const uint8_t *)mapped + sample_offsets[i];
        if (memcmp(pixel, expected, sizeof(expected)) != 0) {
            printf("format_attachments: clear_attachment mismatch sample=%u "
                   "got=%02x%02x%02x%02x expected=ff00ffff\\n", i,
                   pixel[0], pixel[1], pixel[2], pixel[3]);
            goto cleanup;
        }
    }
    puts("format_attachments: CLEAR_ATTACHMENTS PASS format=b8g8r8a8_unorm "
         "extent=1280x720 checks=3 magenta=ff00ffff");
    status = 0;

cleanup:
    if (mapped != NULL)
        vkUnmapMemory(device, readback_memory);
    if (fence != VK_NULL_HANDLE)
        vkDestroyFence(device, fence, NULL);
    if (command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(device, command_pool, NULL);
    if (readback != VK_NULL_HANDLE)
        vkDestroyBuffer(device, readback, NULL);
    if (readback_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, readback_memory, NULL);
    if (framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device, framebuffer, NULL);
    if (render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, render_pass, NULL);
    if (view != VK_NULL_HANDLE)
        vkDestroyImageView(device, view, NULL);
    if (image != VK_NULL_HANDLE)
        vkDestroyImage(device, image, NULL);
    if (image_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, image_memory, NULL);
    return status;

#undef CLEAR_TRY
}
#endif

static VkResult create_test_image(VkPhysicalDevice physical, VkDevice device,
    const FormatCase *format_case, TestImage *test_image)
{
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical, format_case->format,
        &properties);
    if ((properties.linearTilingFeatures &
         VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0u)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format_case->format,
        .extent = {1u, 1u, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
    memset(test_image->mapped, 0xa5, requirements.size);
    test_image->allocation_size = requirements.size;
    const VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, test_image->image, &subresource,
        &layout);
    test_image->pixel = test_image->mapped + layout.offset;

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

static VkResult create_shader_module(VkDevice device, const uint32_t *words,
    size_t size, VkShaderModule *module)
{
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = words,
    };
    return vkCreateShaderModule(device, &info, NULL, module);
}

static VkResult create_pipeline(VkDevice device, VkPipelineLayout layout,
    VkShaderModule vertex, VkShaderModule fragment, VkFormat format,
    VkPipeline *pipeline)
{
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0u,
            VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0u,
            VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", NULL},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkViewport viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const VkRect2D scissor = {{0, 0}, {1u, 1u}};
    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1u,
        .pViewports = &viewport,
        .scissorCount = 1u,
        .pScissors = &scissor,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1u,
        .pAttachments = &blend_attachment,
    };
    const VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1u,
        .pColorAttachmentFormats = &format,
    };
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2u,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .layout = layout,
    };
    return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &info,
        NULL, pipeline);
}

static int run_probe(void)
{
    int status = 1;
    VkResult result;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule vertex_module = VK_NULL_HANDLE;
    VkShaderModule fragment_modules[FRAGMENT_CLASS_COUNT] = {VK_NULL_HANDLE};
    VkPipeline pipelines[FORMAT_COUNT] = {VK_NULL_HANDLE};
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    TestImage images[FORMAT_COUNT] = {0};

#define VK_TRY(expression) do { \
    result = (expression); \
    if (result != VK_SUCCESS) { \
        printf("format_attachments: %s failed (%d)\n", #expression, result); \
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
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0u, 0u, &queue);
#if defined(OPENAGC_PROSPERO)
    if (run_clear_attachments_regression(physical, device, queue) != 0)
        goto cleanup;
#endif

    VkMappedMemoryRange mapped_ranges[FORMAT_COUNT];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        result = create_test_image(physical, device, &format_cases[i],
            &images[i]);
        if (result != VK_SUCCESS) {
            printf("format_attachments: create %s failed (%d)\n",
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

    const VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0u,
        .size = sizeof(VkClearColorValue),
    };
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1u,
        .pPushConstantRanges = &push_range,
    };
    VK_TRY(vkCreatePipelineLayout(device, &layout_info, NULL,
        &pipeline_layout));
    VK_TRY(create_shader_module(device,
        vulkan_ps5_format_attachment_vert_spv,
        sizeof(vulkan_ps5_format_attachment_vert_spv), &vertex_module));
    VK_TRY(create_shader_module(device,
        vulkan_ps5_format_attachment_float_spv,
        sizeof(vulkan_ps5_format_attachment_float_spv),
        &fragment_modules[NUMERIC_FLOAT]));
    VK_TRY(create_shader_module(device,
        vulkan_ps5_format_attachment_uint_spv,
        sizeof(vulkan_ps5_format_attachment_uint_spv),
        &fragment_modules[NUMERIC_UINT]));
    VK_TRY(create_shader_module(device,
        vulkan_ps5_format_attachment_sint_spv,
        sizeof(vulkan_ps5_format_attachment_sint_spv),
        &fragment_modules[NUMERIC_SINT]));
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        result = create_pipeline(device, pipeline_layout, vertex_module,
            fragment_modules[format_cases[i].numeric_class],
            format_cases[i].format, &pipelines[i]);
        if (result != VK_SUCCESS) {
            printf("format_attachments: pipeline %s failed (%d)\n",
                format_cases[i].name, result);
            goto cleanup;
        }
    }

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VK_TRY(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));
    VkCommandBuffer commands[FORMAT_COUNT] = {VK_NULL_HANDLE};
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FORMAT_COUNT,
    };
    VK_TRY(vkAllocateCommandBuffers(device, &command_info, commands));
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_TRY(vkCreateFence(device, &fence_info, NULL, &fence));

    const VkImageSubresourceRange image_range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        VkCommandBuffer command = commands[i];
        VK_TRY(vkBeginCommandBuffer(command, &begin_info));
        const VkImageMemoryBarrier to_attachment = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].image,
            .subresourceRange = image_range,
        };
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
            0u, NULL, 0u, NULL, 1u, &to_attachment);
        const VkRenderingAttachmentInfo color_attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = images[i].view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        const VkRenderingInfo rendering = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {{0, 0}, {1u, 1u}},
            .layerCount = 1u,
            .colorAttachmentCount = 1u,
            .pColorAttachments = &color_attachment,
        };
        vkCmdBeginRendering(command, &rendering);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelines[i]);
        vkCmdPushConstants(command, pipeline_layout,
            VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(format_cases[i].value),
            &format_cases[i].value);
        vkCmdDraw(command, 3u, 1u, 0u, 0u);
        vkCmdEndRendering(command);
        const VkImageMemoryBarrier to_host = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].image,
            .subresourceRange = image_range,
        };
        vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 0u, NULL,
            1u, &to_host);
        VK_TRY(vkEndCommandBuffer(command));
        const VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1u,
            .pCommandBuffers = &command,
        };
        VK_TRY(vkQueueSubmit(queue, 1u, &submit_info, fence));
        result = vkWaitForFences(device, 1u, &fence, VK_TRUE,
            UINT64_C(2000000000));
        if (result != VK_SUCCESS) {
            printf("format_attachments: two-second fence wait failed "
                "format=%s (%d)\n", format_cases[i].name, result);
            goto cleanup;
        }
        if (i + 1u != FORMAT_COUNT)
            VK_TRY(vkResetFences(device, 1u, &fence));
    }
    VK_TRY(vkInvalidateMappedMemoryRanges(device, FORMAT_COUNT,
        mapped_ranges));

#if defined(OPENAGC_PROSPERO)
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        if (memcmp(images[i].pixel, format_cases[i].expected,
                format_cases[i].byte_count) != 0) {
            uint32_t actual[4] = {0u, 0u, 0u, 0u};
            memcpy(actual, images[i].pixel, format_cases[i].byte_count);
            printf("format_attachments: mismatch format=%s "
                "got=%08x,%08x,%08x,%08x\n", format_cases[i].name,
                actual[0], actual[1], actual[2], actual[3]);
            goto cleanup;
        }
    }
    printf("format_attachments: PASS formats=%u pixels=%u exact-bits\n",
        FORMAT_COUNT, FORMAT_COUNT);
#else
    puts("format_attachments: PASS command recording");
#endif
    status = 0;

cleanup:
    if (device != VK_NULL_HANDLE) {
        if (fence != VK_NULL_HANDLE)
            vkDestroyFence(device, fence, NULL);
        if (command_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, command_pool, NULL);
        for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
            if (pipelines[i] != VK_NULL_HANDLE)
                vkDestroyPipeline(device, pipelines[i], NULL);
        }
        if (vertex_module != VK_NULL_HANDLE)
            vkDestroyShaderModule(device, vertex_module, NULL);
        for (uint32_t i = 0u; i < FRAGMENT_CLASS_COUNT; ++i) {
            if (fragment_modules[i] != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, fragment_modules[i], NULL);
        }
        if (pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, NULL);
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
    puts("format_attachments: stage=start");
    const int status = run_probe();
    printf("format_attachments: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("format_attachments");
#endif
    return status;
}
