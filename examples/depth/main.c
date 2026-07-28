#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_ps5_depth_frag_spv.h"
#include "vulkan_ps5_depth_vert_spv.h"

#if defined(VULKAN_PS5_DEPTH_BIAS_CLAMP_PROBE)
#include "../system_service_exit.h"
#define SAMPLE_NAME "depth_bias_clamp"
#define NEAR_DEPTH_BITS 0x3ec00000u
#define FAR_DEPTH_BITS 0x3f600000u
#else
#define SAMPLE_NAME "depth"
#define NEAR_DEPTH_BITS 0x3e800000u
#define FAR_DEPTH_BITS 0x3f400000u
#endif

#define WIDTH 256u
#define HEIGHT 256u
#define GREEN 0xff00ff00u
#define RED 0xff0000ffu

#define VK_CHECK(call) do { \
    VkResult result_ = (call); \
    if (result_ != VK_SUCCESS) { \
        printf(SAMPLE_NAME ": %s failed (%d)\n", #call, result_); \
        return 1; \
    } \
} while (0)

static uint32_t host_type(VkPhysicalDevice physical, uint32_t compatible)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((compatible & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return i;
    return UINT32_MAX;
}

int main(void)
{
    VkInstance instance;
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    VkPhysicalDevice physical;
    uint32_t physical_count = 1u;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u) {
        printf(SAMPLE_NAME ": expected one physical device\n");
        return 1;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));

    const VkImageCreateInfo color_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {WIDTH, HEIGHT, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkImage color_image;
    VK_CHECK(vkCreateImage(device, &color_info, NULL, &color_image));
    VkMemoryRequirements color_requirements;
    vkGetImageMemoryRequirements(device, color_image, &color_requirements);
    uint32_t color_type = host_type(physical, color_requirements.memoryTypeBits);
    if (color_type == UINT32_MAX) return 1;
    const VkMemoryAllocateInfo color_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = color_requirements.size,
        .memoryTypeIndex = color_type,
    };
    VkDeviceMemory color_memory;
    VK_CHECK(vkAllocateMemory(device, &color_memory_info, NULL, &color_memory));
    VK_CHECK(vkBindImageMemory(device, color_image, color_memory, 0));
    void *color_data;
    VK_CHECK(vkMapMemory(device, color_memory, 0, VK_WHOLE_SIZE, 0,
                         &color_data));
    memset(color_data, 0, (size_t)color_requirements.size);

    const VkImageCreateInfo depth_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
        .extent = {WIDTH, HEIGHT, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage depth_image;
    VK_CHECK(vkCreateImage(device, &depth_info, NULL, &depth_image));
    VkMemoryRequirements depth_requirements;
    vkGetImageMemoryRequirements(device, depth_image, &depth_requirements);
    uint32_t depth_type = host_type(physical, depth_requirements.memoryTypeBits);
    if (depth_type == UINT32_MAX || depth_requirements.alignment != 65536u) {
        printf("depth: incompatible D32+S8 memory requirements\n");
        return 1;
    }
    const VkMemoryAllocateInfo depth_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = depth_requirements.size,
        .memoryTypeIndex = depth_type,
    };
    VkDeviceMemory depth_memory;
    VK_CHECK(vkAllocateMemory(device, &depth_memory_info, NULL, &depth_memory));
    VK_CHECK(vkBindImageMemory(device, depth_image, depth_memory, 0));
    void *depth_data;
    VK_CHECK(vkMapMemory(device, depth_memory, 0, VK_WHOLE_SIZE, 0,
                         &depth_data));
    for (size_t i = 0; i < depth_requirements.size / sizeof(uint32_t); ++i)
        ((uint32_t *)depth_data)[i] = 0x3f800000u;
    const VkMappedMemoryRange initial_ranges[] = {
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, color_memory, 0,
         color_requirements.size},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, depth_memory, 0,
         depth_requirements.size},
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 2, initial_ranges));

    const VkImageViewCreateInfo color_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = color_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT |
            VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1},
    };
    VkImageView color_view, depth_view;
    VK_CHECK(vkCreateImageView(device, &color_view_info, NULL, &color_view));
    VK_CHECK(vkCreateImageView(device, &depth_view_info, NULL, &depth_view));
    const VkAttachmentDescription attachments[] = {
        {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_GENERAL},
        {0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
         VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL},
    };
    const VkAttachmentReference color_ref = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference depth_ref = {
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass render_pass;
    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));
    const VkImageView framebuffer_views[] = {color_view, depth_view};
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 2,
        .pAttachments = framebuffer_views,
        .width = WIDTH,
        .height = HEIGHT,
        .layers = 1,
    };
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));

    const VkShaderModuleCreateInfo vs_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
        sizeof(vulkan_ps5_depth_vert_spv), vulkan_ps5_depth_vert_spv,
    };
    const VkShaderModuleCreateInfo fs_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
        sizeof(vulkan_ps5_depth_frag_spv), vulkan_ps5_depth_frag_spv,
    };
    VkShaderModule vs, fs;
    VK_CHECK(vkCreateShaderModule(device, &vs_info, NULL, &vs));
    VK_CHECK(vkCreateShaderModule(device, &fs_info, NULL, &fs));
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkViewport viewport = {0, 0, WIDTH, HEIGHT, 0, 1};
    const VkRect2D scissor = {{0, 0}, {WIDTH, HEIGHT}};
    const VkPipelineViewportStateCreateInfo viewport_state = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0,
        1, &viewport, 1, &scissor,
    };
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
#if defined(VULKAN_PS5_DEPTH_BIAS_CLAMP_PROBE)
        .depthBiasEnable = VK_TRUE,
#endif
        .lineWidth = 1.0f,
    };
#if defined(VULKAN_PS5_DEPTH_BIAS_CLAMP_PROBE)
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_DEPTH_BIAS};
    const VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1u,
        .pDynamicStates = dynamic_states,
    };
#endif
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depth_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .stencilTestEnable = VK_TRUE,
        .front = {
            .passOp = VK_STENCIL_OP_REPLACE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .compareMask = 0xffu,
            .writeMask = 0xffu,
            .reference = 0x5au,
        },
        .back = {
            .passOp = VK_STENCIL_OP_REPLACE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .compareMask = 0xffu,
            .writeMask = 0xffu,
            .reference = 0x5au,
        },
    };
    const VkPipelineColorBlendAttachmentState color_blend = {
        .colorWriteMask = 0xfu,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend,
    };
    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_state,
        .pColorBlendState = &blend,
#if defined(VULKAN_PS5_DEPTH_BIAS_CLAMP_PROBE)
        .pDynamicState = &dynamic_state,
#endif
        .layout = layout,
        .renderPass = render_pass,
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    };
    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo command_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1,
    };
    VkCommandBuffer command;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(command, &begin_info));
    const VkRenderPassBeginInfo begin_render = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {WIDTH, HEIGHT}},
    };
    vkCmdBeginRenderPass(command, &begin_render, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
#if defined(VULKAN_PS5_DEPTH_BIAS_CLAMP_PROBE)
    vkCmdSetDepthBias(command, 1000000000.0f, 0.125f, 0.0f);
#endif
    vkCmdDraw(command, 9, 1, 0, 0);
    vkCmdEndRenderPass(command);
    VK_CHECK(vkEndCommandBuffer(command));
    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 2, initial_ranges));

    uint32_t green = 0, red = 0, other = 0;
    const uint32_t *pixels = color_data;
    for (uint32_t i = 0; i < WIDTH * HEIGHT; ++i) {
        if (pixels[i] == GREEN) ++green;
        else if (pixels[i] == RED) ++red;
        else if (pixels[i] != 0u) ++other;
    }
    uint32_t clear_depth = 0, near_depth = 0, far_depth = 0;
    const uint32_t *depth_words = depth_data;
    for (size_t i = 0; i < depth_requirements.size / sizeof(uint32_t); ++i) {
        if (depth_words[i] == 0x3f800000u) ++clear_depth;
        else if (depth_words[i] == NEAR_DEPTH_BITS) ++near_depth;
        else if (depth_words[i] == FAR_DEPTH_BITS) ++far_depth;
    }
    uint32_t stencil_written = 0;
    const uint8_t *depth_bytes = depth_data;
    for (size_t i = 0; i < depth_requirements.size; ++i)
        if (depth_bytes[i] == 0x5au) ++stencil_written;
    uint32_t left = pixels[128u * WIDTH + 77u];
    uint32_t right = pixels[128u * WIDTH + 192u];
    int status = green < 5000u || red < 4000u || other != 0u ||
        left != GREEN || right != RED || clear_depth == 0u ||
        near_depth < 1000u || far_depth < 1000u ||
        stencil_written != green + red;
    if (status)
        printf(SAMPLE_NAME ": mismatch green=%u red=%u other=%u left=%08x right=%08x raw=%u/%u/%u stencil=%u\n",
            green, red, other, left, right, clear_depth, near_depth, far_depth,
            stencil_written);
    else
        printf(SAMPLE_NAME ": PASS green=%u red=%u raw=%u/%u/%u stencil=%u\n",
            green, red, clear_depth, near_depth, far_depth, stencil_written);

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, layout, NULL);
    vkDestroyShaderModule(device, fs, NULL);
    vkDestroyShaderModule(device, vs, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyImageView(device, depth_view, NULL);
    vkDestroyImageView(device, color_view, NULL);
    vkUnmapMemory(device, depth_memory);
    vkUnmapMemory(device, color_memory);
    vkDestroyImage(device, depth_image, NULL);
    vkDestroyImage(device, color_image, NULL);
    vkFreeMemory(device, depth_memory, NULL);
    vkFreeMemory(device, color_memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
#if defined(OPENAGC_PROSPERO) && \
    defined(VULKAN_PS5_DEPTH_BIAS_CLAMP_PROBE)
    vulkan_ps5_system_service_exit(SAMPLE_NAME);
#endif
    return status;
}
