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

static VkResult create_readback(VkPhysicalDevice physical, VkDevice device,
                                VkBuffer *buffer, VkDeviceMemory *memory,
                                uint8_t **mapped)
{
    const VkDeviceSize size =
        (VkDeviceSize)SCANOUT_WIDTH * SCANOUT_HEIGHT * sizeof(uint32_t);
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
        result = vkMapMemory(device, *memory, 0u, size, 0u,
                             (void **)mapped);
    return result;
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
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore complete = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    uint8_t *readback_pixels = NULL;
    Image garlic_bgra = {0};
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
    TRY(create_readback(physical, device, &readback, &readback_memory,
                        &readback_pixels));
    invalidate.memory = readback_memory;
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
        .commandBufferCount = 1u,
    };
    TRY(vkAllocateCommandBuffers(device, &command_info, &command));
    TRY(vkCreateFence(device, &fence_info, NULL, &fence));
    TRY(vkCreateSemaphore(device, &semaphore_info, NULL, &acquired));
    TRY(vkCreateSemaphore(device, &semaphore_info, NULL, &complete));

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
    TRY(vkDeviceWaitIdle(device));
    puts("scanout_matrix: PASS cases=A,B,C,D,E exact-readback");
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
    if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
    if (command_pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, command_pool, NULL);
    if (readback_pixels) vkUnmapMemory(device, readback_memory);
    if (readback != VK_NULL_HANDLE) vkDestroyBuffer(device, readback, NULL);
    if (readback_memory != VK_NULL_HANDLE) vkFreeMemory(device, readback_memory, NULL);
    if (garlic_rgba.image) vkDestroyImage(device, garlic_rgba.image, NULL);
    if (garlic_rgba.memory) vkFreeMemory(device, garlic_rgba.memory, NULL);
    if (ordinary_bgra.image) vkDestroyImage(device, ordinary_bgra.image, NULL);
    if (ordinary_bgra.memory) vkFreeMemory(device, ordinary_bgra.memory, NULL);
    if (garlic_bgra.image) vkDestroyImage(device, garlic_bgra.image, NULL);
    if (garlic_bgra.memory) vkFreeMemory(device, garlic_bgra.memory, NULL);
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
