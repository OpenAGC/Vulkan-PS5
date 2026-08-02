#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_ps5_format_attachment_fixed_magenta_spv.h"
#include "vulkan_ps5_format_attachment_vert_spv.h"

#include "../system_service_exit.h"

/* This is deliberately a one-frame-per-case diagnostic.  It separates the
 * three producer paths that can feed Eden's scanout image, then reads every
 * produced pixel back before presenting that frame. */
enum {
    SOURCE_WIDTH = 1280u,
    SOURCE_HEIGHT = 720u,
    SCANOUT_WIDTH = 1920u,
    SCANOUT_HEIGHT = 1080u,
    IMAGE_COUNT = 3u,
    QUALIFICATION_SAMPLE_COUNT = 16u,
    QUALIFICATION_READBACK_BYTES =
        2u * QUALIFICATION_SAMPLE_COUNT * sizeof(uint32_t),
};

static const uint8_t kMagenta[] = {0xffu, 0x00u, 0xffu, 0xffu};

#define TRY(call) do { \
    result = (call); \
    if (result != VK_SUCCESS) { \
        printf("scanout_matrix: %s failed (%d)\n", #call, result); \
        goto cleanup; \
    } \
} while (0)

typedef struct Image {
    VkImage image;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize row_pitch;
} Image;

static uint32_t find_host_visible_memory_type(VkPhysicalDevice physical,
                                               uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0u; i < properties.memoryTypeCount; ++i) {
        if ((compatible_types & (UINT32_C(1) << i)) != 0u &&
            (properties.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return i;
    }
    return UINT32_MAX;
}

static VkResult create_image(VkPhysicalDevice physical, VkDevice device,
                             VkFormat format, uint32_t width, uint32_t height,
                             VkImageUsageFlags usage, bool garlic_source,
                             bool map, Image *out)
{
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = garlic_source ?
            VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                VK_IMAGE_CREATE_EXTENDED_USAGE_BIT : 0u,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {width, height, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = garlic_source ? VK_IMAGE_TILING_OPTIMAL :
            VK_IMAGE_TILING_LINEAR,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vkCreateImage(device, &info, NULL, &out->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, out->image, &requirements);
    uint32_t memory_type;
    if (garlic_source) {
        /* The PS5 WSI allocates its scanout images from garlic type 1. */
        if ((requirements.memoryTypeBits & UINT32_C(2)) == 0u)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        memory_type = 1u;
    } else {
        memory_type = find_host_visible_memory_type(physical,
                                                    requirements.memoryTypeBits);
        if (memory_type == UINT32_MAX)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &allocation, NULL, &out->memory);
    if (result == VK_SUCCESS)
        result = vkBindImageMemory(device, out->image, out->memory, 0u);
    if (result != VK_SUCCESS)
        return result;
    if (!map)
        return VK_SUCCESS;
    result = vkMapMemory(device, out->memory, 0u, VK_WHOLE_SIZE, 0u,
                         (void **)&out->mapped);
    if (result != VK_SUCCESS)
        return result;
    const VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, out->image, &subresource, &layout);
    out->mapped += layout.offset;
    out->row_pitch = layout.rowPitch;
    return VK_SUCCESS;
}

/* Eden's intermediate image is explicitly Onion-backed (type 0), unlike the
 * garlic source exercised by the earlier matrix cases.  Keep this allocator
 * narrow so case F verifies that exact placement rather than accidentally
 * falling back to a host-visible or garlic-compatible type. */
static VkResult create_eden_onion_source(VkDevice device, Image *out)
{
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {SOURCE_WIDTH, SOURCE_HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vkCreateImage(device, &info, NULL, &out->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, out->image, &requirements);
    if ((requirements.memoryTypeBits & UINT32_C(1)) == 0u)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = 0u,
    };
    result = vkAllocateMemory(device, &allocation, NULL, &out->memory);
    if (result == VK_SUCCESS)
        result = vkBindImageMemory(device, out->image, out->memory, 0u);
    return result;
}

/* VMA places Eden's three presentation images in one Onion allocation at
 * successive image-requirement offsets.  Case G uses the same placement,
 * while retaining explicit ownership of the one shared VkDeviceMemory. */
static VkResult create_shared_eden_onion_sources(VkDevice device,
                                                 Image images[IMAGE_COUNT],
                                                 VkDeviceMemory *memory_out)
{
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {SOURCE_WIDTH, SOURCE_HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkMemoryRequirements requirements = {0};
    for (uint32_t i = 0u; i < IMAGE_COUNT; ++i) {
        VkResult result = vkCreateImage(device, &info, NULL, &images[i].image);
        if (result != VK_SUCCESS)
            return result;
        VkMemoryRequirements current;
        vkGetImageMemoryRequirements(device, images[i].image, &current);
        if ((current.memoryTypeBits & UINT32_C(1)) == 0u)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (i == 0u) {
            requirements = current;
        } else if (current.size != requirements.size ||
                   current.alignment != requirements.alignment) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size * IMAGE_COUNT,
        .memoryTypeIndex = 0u,
    };
    VkResult result = vkAllocateMemory(device, &allocation, NULL, memory_out);
    if (result != VK_SUCCESS)
        return result;
    for (uint32_t i = 0u; i < IMAGE_COUNT; ++i) {
        result = vkBindImageMemory(device, images[i].image, *memory_out,
                                   requirements.size * i);
        if (result != VK_SUCCESS)
            return result;
    }
    return VK_SUCCESS;
}

/* Recreate one member of the shared allocation with an untouched native
 * resource state.  This lets case H reverse command recording order without
 * carrying state from case G into the decisive probe. */
static VkResult recreate_shared_eden_onion_source(VkDevice device,
                                                  VkDeviceMemory memory,
                                                  VkDeviceSize offset,
                                                  Image *out)
{
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .extent = {SOURCE_WIDTH, SOURCE_HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkResult result = vkCreateImage(device, &info, NULL, &out->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, out->image, &requirements);
    if ((requirements.memoryTypeBits & UINT32_C(1)) == 0u)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    return vkBindImageMemory(device, out->image, memory, offset);
}

static VkResult create_readback(VkPhysicalDevice physical, VkDevice device,
                                VkDeviceSize size,
                                VkBuffer *buffer, VkDeviceMemory *memory,
                                uint8_t **mapped)
{
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = vkCreateBuffer(device, &info, NULL, buffer);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);
    const uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &allocation, NULL, memory);
    if (result == VK_SUCCESS)
        result = vkBindBufferMemory(device, *buffer, *memory, 0u);
    if (result == VK_SUCCESS)
        result = vkMapMemory(device, *memory, 0u, VK_WHOLE_SIZE, 0u,
                             (void **)mapped);
    return result;
}

/* Match Eden's qualification path: sixteen evenly distributed 1x1 reads
 * written into one half of a 128-byte readback buffer. */
static void copy_qualification_samples(VkCommandBuffer command, VkImage image,
                                       uint32_t width, uint32_t height,
                                       VkBuffer buffer, VkDeviceSize base_offset)
{
    VkBufferImageCopy samples[QUALIFICATION_SAMPLE_COUNT] = {{0}};
    for (uint32_t i = 0u; i < QUALIFICATION_SAMPLE_COUNT; ++i) {
        samples[i].bufferOffset = base_offset + i * sizeof(uint32_t);
        samples[i].imageSubresource =
            (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        samples[i].imageOffset =
            (VkOffset3D){(int32_t)((i % 4u) * (width - 1u) / 3u),
                         (int32_t)((i / 4u) * (height - 1u) / 3u), 0};
        samples[i].imageExtent = (VkExtent3D){1u, 1u, 1u};
    }
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer, QUALIFICATION_SAMPLE_COUNT, samples);
}

static void qualification_readback_to_host(VkCommandBuffer command,
                                           VkBuffer buffer)
{
    const VkBufferMemoryBarrier to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = 0u,
        .size = QUALIFICATION_READBACK_BYTES,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 1u,
                         &to_host, 0u, NULL);
}

static bool check_qualification_samples(const char *label,
                                        const uint8_t *samples)
{
    for (uint32_t i = 0u; i < QUALIFICATION_SAMPLE_COUNT; ++i) {
        const uint8_t *pixel = samples + i * sizeof(kMagenta);
        if (memcmp(pixel, kMagenta, sizeof(kMagenta)) != 0) {
            printf("scanout_matrix: %s FAIL sample=%u got=%02x%02x%02x%02x "
                   "expected=ff00ffff\n", label, i, pixel[0], pixel[1],
                   pixel[2], pixel[3]);
            return false;
        }
    }
    printf("scanout_matrix: %s PASS samples=%u exact=ff00ffff\n", label,
           QUALIFICATION_SAMPLE_COUNT);
    return true;
}

static bool check_pixels(const char *label, const uint8_t *pixels,
                         VkDeviceSize row_pitch, uint32_t width,
                         uint32_t height)
{
    for (uint32_t y = 0u; y < height; ++y) {
        const uint8_t *row = pixels + (VkDeviceSize)y * row_pitch;
        for (uint32_t x = 0u; x < width; ++x) {
            const uint8_t *pixel = row + x * sizeof(kMagenta);
            if (memcmp(pixel, kMagenta, sizeof(kMagenta)) != 0) {
                printf("scanout_matrix: %s FAIL x=%u y=%u got=%02x%02x%02x%02x "
                       "expected=ff00ffff\n", label, x, y, pixel[0],
                       pixel[1], pixel[2], pixel[3]);
                return false;
            }
        }
    }
    printf("scanout_matrix: %s PASS pixels=%u exact=ff00ffff\n", label,
           width * height);
    return true;
}

static void image_barrier(VkCommandBuffer command, VkImage image,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkImageLayout old_layout, VkImageLayout new_layout,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage)
{
    const VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
    };
    vkCmdPipelineBarrier(command, src_stage, dst_stage, 0u, 0u, NULL, 0u,
                         NULL, 1u, &barrier);
}

static void copy_to_readback(VkCommandBuffer command, VkImage image,
                             VkBuffer buffer)
{
    const VkBufferImageCopy copy = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .imageExtent = {SCANOUT_WIDTH, SCANOUT_HEIGHT, 1u},
    };
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer, 1u, &copy);
    const VkBufferMemoryBarrier to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 1u,
                         &to_host, 0u, NULL);
}

static VkResult create_graphics_pipeline(VkDevice device, VkRenderPass pass,
                                         uint32_t width, uint32_t height,
                                         VkPipelineLayout *layout_out,
                                         VkShaderModule *vertex_out,
                                         VkShaderModule *fragment_out,
                                         VkPipeline *pipeline_out)
{
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkResult result = vkCreatePipelineLayout(device, &layout_info, NULL,
                                             layout_out);
    if (result != VK_SUCCESS)
        return result;
    const VkShaderModuleCreateInfo vertex_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_format_attachment_vert_spv),
        .pCode = vulkan_ps5_format_attachment_vert_spv,
    };
    result = vkCreateShaderModule(device, &vertex_info, NULL, vertex_out);
    if (result != VK_SUCCESS)
        return result;
    const VkShaderModuleCreateInfo fragment_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_format_attachment_fixed_magenta_spv),
        .pCode = vulkan_ps5_format_attachment_fixed_magenta_spv,
    };
    result = vkCreateShaderModule(device, &fragment_info, NULL, fragment_out);
    if (result != VK_SUCCESS)
        return result;
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0u,
         VK_SHADER_STAGE_VERTEX_BIT, *vertex_out, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0u,
         VK_SHADER_STAGE_FRAGMENT_BIT, *fragment_out, "main", NULL},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height,
                                 0.0f, 1.0f};
    const VkRect2D scissor = {{0, 0}, {width, height}};
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
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1u,
        .pAttachments = &blend_attachment,
    };
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2u,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .layout = *layout_out,
        .renderPass = pass,
    };
    return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &info, NULL,
                                     pipeline_out);
}

static int run_matrix(void)
{
#if !defined(OPENAGC_PROSPERO)
    puts("scanout_matrix: PASS command recording only");
    return 0;
#else
    int status = 1;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkCommandBuffer producer_command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore complete = VK_NULL_HANDLE;
    VkSemaphore render_ready = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    uint8_t *readback_pixels = NULL;
    VkBuffer qualification_readback = VK_NULL_HANDLE;
    VkDeviceMemory qualification_readback_memory = VK_NULL_HANDLE;
    uint8_t *qualification_readback_pixels = NULL;
    Image garlic_bgra = {0};
    Image eden_onion_bgra = {0};
    Image eden_shared_onion[IMAGE_COUNT] = {{0}};
    VkDeviceMemory eden_shared_onion_memory = VK_NULL_HANDLE;
    Image ordinary_bgra = {0};
    Image garlic_rgba = {0};
    VkImage swapchain_images[IMAGE_COUNT] = {VK_NULL_HANDLE};
    VkImageView scanout_view = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkImageView intermediate_view = VK_NULL_HANDLE;
    VkRenderPass intermediate_render_pass = VK_NULL_HANDLE;
    VkFramebuffer intermediate_framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout intermediate_pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule intermediate_vertex = VK_NULL_HANDLE;
    VkShaderModule intermediate_fragment = VK_NULL_HANDLE;
    VkPipeline intermediate_pipeline = VK_NULL_HANDLE;
    VkImageView eden_onion_view = VK_NULL_HANDLE;
    VkFramebuffer eden_onion_framebuffer = VK_NULL_HANDLE;
    VkImageView eden_shared_onion_views[IMAGE_COUNT] = {VK_NULL_HANDLE};
    VkFramebuffer eden_shared_onion_framebuffers[IMAGE_COUNT] = {VK_NULL_HANDLE};
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t image_index = 0u;
    const VkImageSubresourceRange range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    const VkClearColorValue magenta = {
        .float32 = {1.0f, 0.0f, 1.0f, 1.0f},
    };
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkMappedMemoryRange invalidate = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    VkMappedMemoryRange qualification_invalidate = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    const VkPipelineStageFlags transfer_wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &acquired,
        .pWaitDstStageMask = &transfer_wait,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &complete,
    };
    const VkImageBlit scale = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .srcOffsets = {{0, 0, 0}, {SOURCE_WIDTH, SOURCE_HEIGHT, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .dstOffsets = {{0, 0, 0}, {SCANOUT_WIDTH, SCANOUT_HEIGHT, 1}},
    };

    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    TRY(vkCreateInstance(&instance_info, NULL, &instance));
    const VkHeadlessSurfaceCreateInfoEXT surface_info = {
        .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
    };
    TRY(vkCreateHeadlessSurfaceEXT(instance, &surface_info, NULL, &surface));
    uint32_t physical_count = 1u;
    TRY(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u) {
        puts("scanout_matrix: expected exactly one physical device");
        goto cleanup;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0u,
        .queueCount = 1u,
        .pQueuePriorities = &priority,
    };
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = 1u,
        .ppEnabledExtensionNames = device_extensions,
    };
    TRY(vkCreateDevice(physical, &device_info, NULL, &device));
    vkGetDeviceQueue(device, 0u, 0u, &queue);

    const VkFormat mutable_formats[] = {
        VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB,
    };
    const VkImageFormatListCreateInfo format_list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2u,
        .pViewFormats = mutable_formats,
    };
    const VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = &format_list,
        .flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR,
        .surface = surface,
        .minImageCount = IMAGE_COUNT,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {SCANOUT_WIDTH, SCANOUT_HEIGHT},
        .imageArrayLayers = 1u,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    TRY(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain));
    uint32_t swapchain_count = IMAGE_COUNT;
    TRY(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_count,
                                swapchain_images));
    if (swapchain_count != IMAGE_COUNT) {
        puts("scanout_matrix: unexpected swapchain image count");
        goto cleanup;
    }
    TRY(create_image(physical, device, VK_FORMAT_B8G8R8A8_UNORM,
                     SOURCE_WIDTH, SOURCE_HEIGHT,
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, true, false,
                     &garlic_bgra));
    TRY(create_eden_onion_source(device, &eden_onion_bgra));
    TRY(create_shared_eden_onion_sources(device, eden_shared_onion,
                                         &eden_shared_onion_memory));
    TRY(create_image(physical, device, VK_FORMAT_B8G8R8A8_UNORM,
                     SCANOUT_WIDTH, SCANOUT_HEIGHT,
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT, false, false,
                     &ordinary_bgra));
    TRY(create_image(physical, device, VK_FORMAT_R8G8B8A8_UNORM,
                     SOURCE_WIDTH, SOURCE_HEIGHT,
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, true, false,
                     &garlic_rgba));
    TRY(create_readback(physical, device,
                        (VkDeviceSize)SCANOUT_WIDTH * SCANOUT_HEIGHT *
                            sizeof(uint32_t),
                        &readback, &readback_memory,
                        &readback_pixels));
    invalidate.memory = readback_memory;
    TRY(create_readback(physical, device, QUALIFICATION_READBACK_BYTES,
                        &qualification_readback,
                        &qualification_readback_memory,
                        &qualification_readback_pixels));
    qualification_invalidate.memory = qualification_readback_memory;
    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0u,
    };
    TRY(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 2u,
    };
    VkCommandBuffer command_buffers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    TRY(vkAllocateCommandBuffers(device, &command_info, command_buffers));
    command = command_buffers[0];
    producer_command = command_buffers[1];
    TRY(vkCreateFence(device, &fence_info, NULL, &fence));
    TRY(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired));
    TRY(vkCreateSemaphore(device, &semaphore_info, NULL, &complete));
    TRY(vkCreateSemaphore(device, &semaphore_info, NULL, &render_ready));

    /* E runs before A so the existing BGRA8 garlic intermediate has Eden's
     * real first-use state: initialLayout UNDEFINED, then a GENERAL color
     * attachment.  Its scanout blit occurs in a later queue submission. */
    const VkAttachmentDescription intermediate_attachment = {
        .flags = VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference intermediate_color = {
        .attachment = 0u,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkSubpassDescription intermediate_subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &intermediate_color,
    };
    const VkSubpassDependency intermediate_dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0u,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    const VkRenderPassCreateInfo intermediate_render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1u,
        .pAttachments = &intermediate_attachment,
        .subpassCount = 1u,
        .pSubpasses = &intermediate_subpass,
        .dependencyCount = 1u,
        .pDependencies = &intermediate_dependency,
    };
    TRY(vkCreateRenderPass(device, &intermediate_render_pass_info, NULL,
                           &intermediate_render_pass));
    TRY(create_graphics_pipeline(device, intermediate_render_pass,
                                 SOURCE_WIDTH, SOURCE_HEIGHT,
                                 &intermediate_pipeline_layout,
                                 &intermediate_vertex, &intermediate_fragment,
                                 &intermediate_pipeline));
    const VkImageViewCreateInfo intermediate_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = garlic_bgra.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
    };
    TRY(vkCreateImageView(device, &intermediate_view_info, NULL,
                          &intermediate_view));
    const VkFramebufferCreateInfo intermediate_framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = intermediate_render_pass,
        .attachmentCount = 1u,
        .pAttachments = &intermediate_view,
        .width = SOURCE_WIDTH,
        .height = SOURCE_HEIGHT,
        .layers = 1u,
    };
    TRY(vkCreateFramebuffer(device, &intermediate_framebuffer_info, NULL,
                            &intermediate_framebuffer));
    const VkImageViewCreateInfo eden_onion_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = eden_onion_bgra.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
    };
    TRY(vkCreateImageView(device, &eden_onion_view_info, NULL,
                          &eden_onion_view));
    const VkFramebufferCreateInfo eden_onion_framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = intermediate_render_pass,
        .attachmentCount = 1u,
        .pAttachments = &eden_onion_view,
        .width = SOURCE_WIDTH,
        .height = SOURCE_HEIGHT,
        .layers = 1u,
    };
    TRY(vkCreateFramebuffer(device, &eden_onion_framebuffer_info, NULL,
                            &eden_onion_framebuffer));
    for (uint32_t i = 0u; i < IMAGE_COUNT; ++i) {
        VkImageViewCreateInfo shared_view_info = eden_onion_view_info;
        shared_view_info.image = eden_shared_onion[i].image;
        TRY(vkCreateImageView(device, &shared_view_info, NULL,
                              &eden_shared_onion_views[i]));
        VkFramebufferCreateInfo shared_framebuffer_info =
            eden_onion_framebuffer_info;
        shared_framebuffer_info.pAttachments = &eden_shared_onion_views[i];
        TRY(vkCreateFramebuffer(device, &shared_framebuffer_info, NULL,
                                &eden_shared_onion_framebuffers[i]));
    }

    /* H runs before the broader A--G matrix.  It is the decisive reproduction:
     * record the consumer while the shared Onion source has pristine native
     * state, then record and submit its producer.  The consumer waits for the
     * producer at TRANSFER, exactly as Eden's presentation path does.  Running
     * it first avoids unrelated scanout presents masking this ordering probe. */
    const VkPipelineStageFlags h_wait_stages[2] = {
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
    const VkSemaphore h_wait_semaphores[2] = {acquired, render_ready};
    const VkSubmitInfo h_consumer_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 2u,
        .pWaitSemaphores = h_wait_semaphores,
        .pWaitDstStageMask = h_wait_stages,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &complete,
    };
    TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                              VK_NULL_HANDLE, &image_index));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, eden_shared_onion[0].image,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_barrier(command, swapchain_images[image_index], 0u,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdBlitImage(command, eden_shared_onion[0].image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[image_index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &scale, VK_FILTER_LINEAR);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_to_readback(command, swapchain_images[image_index], readback);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    TRY(vkEndCommandBuffer(command));

    TRY(vkResetCommandBuffer(producer_command, 0u));
    TRY(vkBeginCommandBuffer(producer_command, &begin));
    const VkRenderPassBeginInfo h_producer_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = intermediate_render_pass,
        .framebuffer = eden_shared_onion_framebuffers[0],
        .renderArea = {{0, 0}, {SOURCE_WIDTH, SOURCE_HEIGHT}},
    };
    vkCmdBeginRenderPass(producer_command, &h_producer_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(producer_command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      intermediate_pipeline);
    vkCmdDraw(producer_command, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(producer_command);
    TRY(vkEndCommandBuffer(producer_command));
    const VkSubmitInfo h_producer_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &producer_command,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &render_ready,
    };
    TRY(vkQueueSubmit(queue, 1u, &h_producer_submit, VK_NULL_HANDLE));
    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkQueueSubmit(queue, 1u, &h_consumer_submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
    if (!check_pixels("H consumer-recorded-before-producer", readback_pixels,
                      SCANOUT_WIDTH * sizeof(uint32_t), SCANOUT_WIDTH,
                      SCANOUT_HEIGHT))
        goto cleanup;
    const VkPresentInfoKHR h_present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &complete,
        .swapchainCount = 1u,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };
    TRY(vkQueuePresentKHR(queue, &h_present));
    puts("scanout_matrix: H eden-consumer-recorded-before-producer PASS pixels=2073600 exact=ff00ffff");

    /* H consumed shared source zero.  Recreate that same allocation member so
     * G still independently verifies all three pristine VMA-style sources. */
    vkDestroyFramebuffer(device, eden_shared_onion_framebuffers[0], NULL);
    eden_shared_onion_framebuffers[0] = VK_NULL_HANDLE;
    vkDestroyImageView(device, eden_shared_onion_views[0], NULL);
    eden_shared_onion_views[0] = VK_NULL_HANDLE;
    vkDestroyImage(device, eden_shared_onion[0].image, NULL);
    eden_shared_onion[0].image = VK_NULL_HANDLE;
    TRY(recreate_shared_eden_onion_source(device, eden_shared_onion_memory,
                                           0u, &eden_shared_onion[0]));
    VkImageViewCreateInfo h_reset_view_info = eden_onion_view_info;
    h_reset_view_info.image = eden_shared_onion[0].image;
    TRY(vkCreateImageView(device, &h_reset_view_info, NULL,
                          &eden_shared_onion_views[0]));
    VkFramebufferCreateInfo h_reset_framebuffer_info =
        eden_onion_framebuffer_info;
    h_reset_framebuffer_info.pAttachments = &eden_shared_onion_views[0];
    TRY(vkCreateFramebuffer(device, &h_reset_framebuffer_info, NULL,
                            &eden_shared_onion_framebuffers[0]));

    /* I: H proves that recording the consumer before its producer is sound.
     * Keep that ordering, but add Eden's exact qualification traffic: sparse
     * reads of the source before the blit and the destination afterwards,
     * sharing the two halves of one 128-byte host-visible buffer. */
    TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                              VK_NULL_HANDLE, &image_index));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, eden_shared_onion[0].image,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_barrier(command, swapchain_images[image_index], 0u,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_qualification_samples(command, eden_shared_onion[0].image,
                               SOURCE_WIDTH, SOURCE_HEIGHT,
                               qualification_readback, 0u);
    vkCmdBlitImage(command, eden_shared_onion[0].image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[image_index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &scale, VK_FILTER_LINEAR);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_qualification_samples(command, swapchain_images[image_index],
                               SCANOUT_WIDTH, SCANOUT_HEIGHT,
                               qualification_readback,
                               QUALIFICATION_READBACK_BYTES / 2u);
    qualification_readback_to_host(command, qualification_readback);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    TRY(vkEndCommandBuffer(command));

    TRY(vkResetCommandBuffer(producer_command, 0u));
    TRY(vkBeginCommandBuffer(producer_command, &begin));
    const VkRenderPassBeginInfo i_producer_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = intermediate_render_pass,
        .framebuffer = eden_shared_onion_framebuffers[0],
        .renderArea = {{0, 0}, {SOURCE_WIDTH, SOURCE_HEIGHT}},
    };
    vkCmdBeginRenderPass(producer_command, &i_producer_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(producer_command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      intermediate_pipeline);
    vkCmdDraw(producer_command, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(producer_command);
    TRY(vkEndCommandBuffer(producer_command));
    const VkSubmitInfo i_producer_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &producer_command,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &render_ready,
    };
    TRY(vkQueueSubmit(queue, 1u, &i_producer_submit, VK_NULL_HANDLE));
    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkQueueSubmit(queue, 1u, &h_consumer_submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &qualification_invalidate));
    if (!check_qualification_samples("I eden-source-sparse-read-before-blit",
                                     qualification_readback_pixels) ||
        !check_qualification_samples("I eden-swapchain-sparse-read-after-blit",
                                     qualification_readback_pixels +
                                         QUALIFICATION_READBACK_BYTES / 2u))
        goto cleanup;
    TRY(vkQueuePresentKHR(queue, &h_present));
    puts("scanout_matrix: I eden-sparse-readback-before-blit PASS source_samples=16 destination_samples=16 exact=ff00ffff");

    /* I consumes shared source zero too; restore it for G's independently
     * pristine three-image VMA-style allocation test. */
    vkDestroyFramebuffer(device, eden_shared_onion_framebuffers[0], NULL);
    eden_shared_onion_framebuffers[0] = VK_NULL_HANDLE;
    vkDestroyImageView(device, eden_shared_onion_views[0], NULL);
    eden_shared_onion_views[0] = VK_NULL_HANDLE;
    vkDestroyImage(device, eden_shared_onion[0].image, NULL);
    eden_shared_onion[0].image = VK_NULL_HANDLE;
    TRY(recreate_shared_eden_onion_source(device, eden_shared_onion_memory,
                                           0u, &eden_shared_onion[0]));
    h_reset_view_info.image = eden_shared_onion[0].image;
    TRY(vkCreateImageView(device, &h_reset_view_info, NULL,
                          &eden_shared_onion_views[0]));
    h_reset_framebuffer_info.pAttachments = &eden_shared_onion_views[0];
    TRY(vkCreateFramebuffer(device, &h_reset_framebuffer_info, NULL,
                            &eden_shared_onion_framebuffers[0]));

    TRY(vkBeginCommandBuffer(command, &begin));
    const VkRenderPassBeginInfo intermediate_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = intermediate_render_pass,
        .framebuffer = intermediate_framebuffer,
        .renderArea = {{0, 0}, {SOURCE_WIDTH, SOURCE_HEIGHT}},
    };
    vkCmdBeginRenderPass(command, &intermediate_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      intermediate_pipeline);
    vkCmdDraw(command, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(command);
    TRY(vkEndCommandBuffer(command));
    const VkSubmitInfo intermediate_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    TRY(vkQueueSubmit(queue, 1u, &intermediate_submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));

    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                              VK_NULL_HANDLE, &image_index));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, garlic_bgra.image, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_barrier(command, swapchain_images[image_index], 0u,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdBlitImage(command, garlic_bgra.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &scale, VK_FILTER_LINEAR);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_to_readback(command, swapchain_images[image_index], readback);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    TRY(vkEndCommandBuffer(command));
    TRY(vkQueueSubmit(queue, 1u, &submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
    if (!check_pixels("E eden-like-rendered-bgra-to-scanout-linear-blit",
                      readback_pixels, SCANOUT_WIDTH * sizeof(uint32_t),
                      SCANOUT_WIDTH, SCANOUT_HEIGHT))
        goto cleanup;
    const VkPresentInfoKHR intermediate_present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &complete,
        .swapchainCount = 1u,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };
    TRY(vkQueuePresentKHR(queue, &intermediate_present));

    /* A: garlic BGRA8 -> ordinary host-visible 1920x1080 through a scaling
     * blit.  This is the transfer path without scanout involvement. */
    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, garlic_bgra.image, 0u, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdClearColorImage(command, garlic_bgra.image, VK_IMAGE_LAYOUT_GENERAL,
                         &magenta, 1u, &range);
    image_barrier(command, garlic_bgra.image, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_barrier(command, ordinary_bgra.image, 0u, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdBlitImage(command, garlic_bgra.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   ordinary_bgra.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &scale, VK_FILTER_LINEAR);
    image_barrier(command, ordinary_bgra.image, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_to_readback(command, ordinary_bgra.image, readback);
    TRY(vkEndCommandBuffer(command));
    const VkSubmitInfo ordinary_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    TRY(vkQueueSubmit(queue, 1u, &ordinary_submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
    if (!check_pixels("A garlic-bgra-to-ordinary-linear-blit",
                      readback_pixels, SCANOUT_WIDTH * sizeof(uint32_t),
                      SCANOUT_WIDTH, SCANOUT_HEIGHT))
        goto cleanup;

    /* B: known RGBA8 garlic source -> real mutable UNORM/SRGB scanout. */
    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                              VK_NULL_HANDLE, &image_index));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, garlic_rgba.image, 0u, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdClearColorImage(command, garlic_rgba.image, VK_IMAGE_LAYOUT_GENERAL,
                         &magenta, 1u, &range);
    image_barrier(command, garlic_rgba.image, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_barrier(command, swapchain_images[image_index], 0u,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdBlitImage(command, garlic_rgba.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &scale, VK_FILTER_LINEAR);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_to_readback(command, swapchain_images[image_index], readback);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    TRY(vkEndCommandBuffer(command));
    TRY(vkQueueSubmit(queue, 1u, &submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
    if (!check_pixels("B rgba8-to-scanout-linear-blit", readback_pixels,
                      SCANOUT_WIDTH * sizeof(uint32_t), SCANOUT_WIDTH,
                      SCANOUT_HEIGHT))
        goto cleanup;
    const VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &complete,
        .swapchainCount = 1u,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
    };
    TRY(vkQueuePresentKHR(queue, &present));

    /* C: an actual fixed fragment shader writes directly to scanout, then
     * transfer readback proves the attachment/draw route independently. */
    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference color = {
        .attachment = 0u,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &color,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1u,
        .pAttachments = &attachment,
        .subpassCount = 1u,
        .pSubpasses = &subpass,
    };
    TRY(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));
    TRY(create_graphics_pipeline(device, render_pass, SCANOUT_WIDTH, SCANOUT_HEIGHT,
                                 &pipeline_layout, &vertex, &fragment, &pipeline));
    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                              VK_NULL_HANDLE, &image_index));
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = swapchain_images[image_index],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u},
    };
    TRY(vkCreateImageView(device, &view_info, NULL, &scanout_view));
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1u,
        .pAttachments = &scanout_view,
        .width = SCANOUT_WIDTH,
        .height = SCANOUT_HEIGHT,
        .layers = 1u,
    };
    TRY(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, swapchain_images[image_index], 0u,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {SCANOUT_WIDTH, SCANOUT_HEIGHT}},
    };
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(command, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(command);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_to_readback(command, swapchain_images[image_index], readback);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    TRY(vkEndCommandBuffer(command));
    const VkPipelineStageFlags graphics_wait =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo graphics_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores = &acquired,
        .pWaitDstStageMask = &graphics_wait,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &complete,
    };
    TRY(vkQueueSubmit(queue, 1u, &graphics_submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
    if (!check_pixels("C fixed-fragment-draw-to-scanout", readback_pixels,
                      SCANOUT_WIDTH * sizeof(uint32_t), SCANOUT_WIDTH,
                      SCANOUT_HEIGHT))
        goto cleanup;
    TRY(vkQueuePresentKHR(queue, &present));

    /* D: reuse A's BGRA8 garlic source after its first submit has completed,
     * then blit it into a newly acquired scanout image in a later submit. */
    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                              VK_NULL_HANDLE, &image_index));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, swapchain_images[image_index], 0u,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdBlitImage(command, garlic_bgra.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[image_index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &scale, VK_FILTER_LINEAR);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_to_readback(command, swapchain_images[image_index], readback);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    TRY(vkEndCommandBuffer(command));
    TRY(vkQueueSubmit(queue, 1u, &submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
    if (!check_pixels("D prior-submit-bgra-to-scanout-linear-blit",
                      readback_pixels, SCANOUT_WIDTH * sizeof(uint32_t),
                      SCANOUT_WIDTH, SCANOUT_HEIGHT))
        goto cleanup;
    TRY(vkQueuePresentKHR(queue, &present));

    /* F: Eden's exact producer contract.  The BGRA8 UNORM source is an
     * optimal, MUTABLE|EXTENDED_USAGE, TRANSFER_SRC|COLOR_ATTACHMENT image
     * backed specifically by Onion type 0.  Its first use is the MAY_ALIAS
     * GENERAL render pass, followed by the production-style later-submit
     * linear blit into the mutable UNORM/native-SRGB scanout image. */
    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkBeginCommandBuffer(command, &begin));
    const VkRenderPassBeginInfo eden_onion_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = intermediate_render_pass,
        .framebuffer = eden_onion_framebuffer,
        .renderArea = {{0, 0}, {SOURCE_WIDTH, SOURCE_HEIGHT}},
    };
    vkCmdBeginRenderPass(command, &eden_onion_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      intermediate_pipeline);
    vkCmdDraw(command, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(command);
    TRY(vkEndCommandBuffer(command));
    const VkSubmitInfo eden_onion_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    TRY(vkQueueSubmit(queue, 1u, &eden_onion_submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));

    TRY(vkResetFences(device, 1u, &fence));
    TRY(vkResetCommandBuffer(command, 0u));
    TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                              VK_NULL_HANDLE, &image_index));
    TRY(vkBeginCommandBuffer(command, &begin));
    image_barrier(command, eden_onion_bgra.image,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_barrier(command, swapchain_images[image_index], 0u,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdBlitImage(command, eden_onion_bgra.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_images[image_index],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &scale, VK_FILTER_LINEAR);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    copy_to_readback(command, swapchain_images[image_index], readback);
    image_barrier(command, swapchain_images[image_index],
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    TRY(vkEndCommandBuffer(command));
    TRY(vkQueueSubmit(queue, 1u, &submit, fence));
    TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_C(2000000000)));
    TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
    if (!check_pixels("F eden-onion-rendered-bgra-to-scanout-linear-blit",
                      readback_pixels, SCANOUT_WIDTH * sizeof(uint32_t),
                      SCANOUT_WIDTH, SCANOUT_HEIGHT))
        goto cleanup;
    TRY(vkQueuePresentKHR(queue, &present));

    /* G: retain the exact F image/render-pass contract but place all three
     * source images into one Onion allocation at the offsets VMA uses for
     * Eden.  The producer signals a binary semaphore and the consumer waits
     * on it together with swapchain acquisition at TRANSFER, exactly as the
     * production presentation submission does. */
    const VkPipelineStageFlags g_wait_stages[2] = {
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
    const VkSemaphore g_wait_semaphores[2] = {acquired, render_ready};
    const VkSubmitInfo g_consumer_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 2u,
        .pWaitSemaphores = g_wait_semaphores,
        .pWaitDstStageMask = g_wait_stages,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &complete,
    };
    for (uint32_t source_index = 0u; source_index < IMAGE_COUNT; ++source_index) {
        TRY(vkResetCommandBuffer(producer_command, 0u));
        TRY(vkBeginCommandBuffer(producer_command, &begin));
        const VkRenderPassBeginInfo shared_onion_begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = intermediate_render_pass,
            .framebuffer = eden_shared_onion_framebuffers[source_index],
            .renderArea = {{0, 0}, {SOURCE_WIDTH, SOURCE_HEIGHT}},
        };
        vkCmdBeginRenderPass(producer_command, &shared_onion_begin,
                             VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(producer_command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          intermediate_pipeline);
        vkCmdDraw(producer_command, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(producer_command);
        TRY(vkEndCommandBuffer(producer_command));
        const VkSubmitInfo g_producer_submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1u,
            .pCommandBuffers = &producer_command,
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &render_ready,
        };
        TRY(vkQueueSubmit(queue, 1u, &g_producer_submit, VK_NULL_HANDLE));

        TRY(vkAcquireNextImageKHR(device, swapchain, UINT64_C(2000000000), acquired,
                                  VK_NULL_HANDLE, &image_index));
        TRY(vkResetFences(device, 1u, &fence));
        TRY(vkResetCommandBuffer(command, 0u));
        TRY(vkBeginCommandBuffer(command, &begin));
        image_barrier(command, eden_shared_onion[source_index].image,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);
        image_barrier(command, swapchain_images[image_index], 0u,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdBlitImage(command, eden_shared_onion[source_index].image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapchain_images[image_index],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1u, &scale, VK_FILTER_LINEAR);
        image_barrier(command, swapchain_images[image_index],
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT);
        copy_to_readback(command, swapchain_images[image_index], readback);
        image_barrier(command, swapchain_images[image_index],
                      VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
        TRY(vkEndCommandBuffer(command));
        TRY(vkQueueSubmit(queue, 1u, &g_consumer_submit, fence));
        TRY(vkWaitForFences(device, 1u, &fence, VK_TRUE,
                            UINT64_C(2000000000)));
        TRY(vkInvalidateMappedMemoryRanges(device, 1u, &invalidate));
        if (!check_pixels("G shared-onion source", readback_pixels,
                          SCANOUT_WIDTH * sizeof(uint32_t), SCANOUT_WIDTH,
                          SCANOUT_HEIGHT))
            goto cleanup;
        TRY(vkQueuePresentKHR(queue, &present));
    }
    puts("scanout_matrix: G eden-shared-onion-semaphore-to-scanout-linear-blit PASS images=3 pixels=6220800 exact=ff00ffff");

    TRY(vkDeviceWaitIdle(device));
    puts("scanout_matrix: PASS cases=A,B,C,D,E,F,G,H,I exact-readback");
    status = 0;

cleanup:
    if (device != VK_NULL_HANDLE)
        (void)vkDeviceWaitIdle(device);
    if (intermediate_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, intermediate_pipeline, NULL);
    if (intermediate_fragment != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, intermediate_fragment, NULL);
    if (intermediate_vertex != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, intermediate_vertex, NULL);
    if (intermediate_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, intermediate_pipeline_layout, NULL);
    if (eden_onion_framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device, eden_onion_framebuffer, NULL);
    if (eden_onion_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, eden_onion_view, NULL);
    for (uint32_t i = 0u; i < IMAGE_COUNT; ++i) {
        if (eden_shared_onion_framebuffers[i] != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, eden_shared_onion_framebuffers[i], NULL);
        if (eden_shared_onion_views[i] != VK_NULL_HANDLE)
            vkDestroyImageView(device, eden_shared_onion_views[i], NULL);
    }
    if (intermediate_framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device, intermediate_framebuffer, NULL);
    if (intermediate_render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, intermediate_render_pass, NULL);
    if (intermediate_view != VK_NULL_HANDLE)
        vkDestroyImageView(device, intermediate_view, NULL);
    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, NULL);
    if (fragment != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragment, NULL);
    if (vertex != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertex, NULL);
    if (pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, NULL);
    if (render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, render_pass, NULL);
    if (scanout_view != VK_NULL_HANDLE) vkDestroyImageView(device, scanout_view, NULL);
    if (complete != VK_NULL_HANDLE) vkDestroySemaphore(device, complete, NULL);
    if (acquired != VK_NULL_HANDLE) vkDestroySemaphore(device, acquired, NULL);
    if (render_ready != VK_NULL_HANDLE)
        vkDestroySemaphore(device, render_ready, NULL);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
    if (command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, command_pool, NULL);
    if (qualification_readback_pixels)
        vkUnmapMemory(device, qualification_readback_memory);
    if (qualification_readback != VK_NULL_HANDLE)
        vkDestroyBuffer(device, qualification_readback, NULL);
    if (qualification_readback_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, qualification_readback_memory, NULL);
    if (readback_pixels) vkUnmapMemory(device, readback_memory);
    if (readback != VK_NULL_HANDLE) vkDestroyBuffer(device, readback, NULL);
    if (readback_memory != VK_NULL_HANDLE) vkFreeMemory(device, readback_memory, NULL);
    if (garlic_rgba.image) vkDestroyImage(device, garlic_rgba.image, NULL);
    if (garlic_rgba.memory) vkFreeMemory(device, garlic_rgba.memory, NULL);
    if (ordinary_bgra.image) vkDestroyImage(device, ordinary_bgra.image, NULL);
    if (ordinary_bgra.memory) vkFreeMemory(device, ordinary_bgra.memory, NULL);
    if (garlic_bgra.image) vkDestroyImage(device, garlic_bgra.image, NULL);
    if (garlic_bgra.memory) vkFreeMemory(device, garlic_bgra.memory, NULL);
    if (eden_onion_bgra.image) vkDestroyImage(device, eden_onion_bgra.image, NULL);
    if (eden_onion_bgra.memory) vkFreeMemory(device, eden_onion_bgra.memory, NULL);
    for (uint32_t i = 0u; i < IMAGE_COUNT; ++i) {
        if (eden_shared_onion[i].image)
            vkDestroyImage(device, eden_shared_onion[i].image, NULL);
    }
    if (eden_shared_onion_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, eden_shared_onion_memory, NULL);
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, NULL);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, NULL);
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, NULL);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, NULL);
    return status;
#endif
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    puts("scanout_matrix: stage=start");
    const int status = run_matrix();
    printf("scanout_matrix: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("scanout_matrix");
#endif
    return status;
}
