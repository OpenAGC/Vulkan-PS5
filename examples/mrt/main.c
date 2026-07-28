#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_ps5_mrt_frag_spv.h"
#include "vulkan_ps5_mrt_vert_spv.h"

#if defined(VULKAN_PS5_INDEPENDENT_BLEND_PROBE)
#include "../system_service_exit.h"
#define SAMPLE_NAME "independent_blend"
#define TARGET1_COLOR 0x80800080u
#else
#define SAMPLE_NAME "mrt"
#define TARGET1_COLOR MAGENTA
#endif

#define WIDTH 256u
#define HEIGHT 256u
#define GREEN 0xff00ff00u
#define MAGENTA 0xffff00ffu

#define VK_CHECK(call) do { \
    VkResult result_ = (call); \
    if (result_ != VK_SUCCESS) { \
        printf(SAMPLE_NAME ": %s failed (%d)\n", #call, result_); \
        return 1; \
    } \
} while (0)

typedef struct Target {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkMemoryRequirements requirements;
    void *mapped;
} Target;

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

static int create_target(VkPhysicalDevice physical, VkDevice device,
                         Target *target)
{
    const VkImageCreateInfo image_info = {
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
    VkResult result = vkCreateImage(device, &image_info, NULL, &target->image);
    if (result != VK_SUCCESS) return result;
    vkGetImageMemoryRequirements(device, target->image, &target->requirements);
    uint32_t type = host_type(physical, target->requirements.memoryTypeBits);
    if (type == UINT32_MAX) return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = target->requirements.size,
        .memoryTypeIndex = type,
    };
    result = vkAllocateMemory(device, &memory_info, NULL, &target->memory);
    if (result != VK_SUCCESS) return result;
    result = vkBindImageMemory(device, target->image, target->memory, 0);
    if (result != VK_SUCCESS) return result;
    result = vkMapMemory(device, target->memory, 0, VK_WHOLE_SIZE, 0,
                         &target->mapped);
    if (result != VK_SUCCESS) return result;
    memset(target->mapped, 0, (size_t)target->requirements.size);
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = target->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    return vkCreateImageView(device, &view_info, NULL, &target->view);
}

int main(void)
{
    VkInstance instance;
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    VkPhysicalDevice physical;
    uint32_t physical_count = 1;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1) {
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

    Target targets[2] = {0};
    VK_CHECK(create_target(physical, device, &targets[0]));
    VK_CHECK(create_target(physical, device, &targets[1]));
    const VkMappedMemoryRange ranges[] = {
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, targets[0].memory, 0,
         targets[0].requirements.size},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, targets[1].memory, 0,
         targets[1].requirements.size},
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 2, ranges));

    const VkAttachmentDescription attachments[] = {
        {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_GENERAL},
        {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
         VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
         VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_GENERAL},
    };
    const VkAttachmentReference color_refs[] = {
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 2,
        .pColorAttachments = color_refs,
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
    const VkImageView views[] = {targets[0].view, targets[1].view};
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 2,
        .pAttachments = views,
        .width = WIDTH,
        .height = HEIGHT,
        .layers = 1,
    };
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));

    const VkShaderModuleCreateInfo vertex_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
        sizeof(vulkan_ps5_mrt_vert_spv), vulkan_ps5_mrt_vert_spv,
    };
    const VkShaderModuleCreateInfo fragment_info = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0,
        sizeof(vulkan_ps5_mrt_frag_spv), vulkan_ps5_mrt_frag_spv,
    };
    VkShaderModule vertex_shader, fragment_shader;
    VK_CHECK(vkCreateShaderModule(device, &vertex_info, NULL, &vertex_shader));
    VK_CHECK(vkCreateShaderModule(device, &fragment_info, NULL,
                                  &fragment_shader));
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader, "main", NULL},
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
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blend_attachments[] = {
        {.colorWriteMask = 0xfu},
#if defined(VULKAN_PS5_INDEPENDENT_BLEND_PROBE)
        {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_COLOR,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = 0xfu,
        },
#else
        {.colorWriteMask = 0xfu},
#endif
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = blend_attachments,
#if defined(VULKAN_PS5_INDEPENDENT_BLEND_PROBE)
        .blendConstants = {0.5f, 0.5f, 0.5f, 0.5f},
#endif
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
        .pColorBlendState = &blend,
        .layout = layout,
        .renderPass = render_pass,
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                       &pipeline_info, NULL, &pipeline));

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
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
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {WIDTH, HEIGHT}},
    };
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
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
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 2, ranges));

    const uint32_t expected[] = {GREEN, TARGET1_COLOR};
    uint32_t counts[2] = {0, 0};
    uint32_t unexpected[2] = {0, 0};
    int status = 0;
    for (uint32_t target = 0; target < 2; ++target) {
        const uint32_t *pixels = targets[target].mapped;
        for (uint32_t i = 0; i < WIDTH * HEIGHT; ++i) {
            if (pixels[i] == expected[target]) ++counts[target];
            else if (pixels[i] != 0) ++unexpected[target];
        }
        uint32_t center = pixels[(HEIGHT / 2) * WIDTH + WIDTH / 2];
        if (counts[target] < 16000 || counts[target] > 21000 ||
            unexpected[target] != 0 || center != expected[target] ||
            pixels[0] != 0 || pixels[WIDTH - 1] != 0)
            status = 1;
    }
    if (status)
        printf(SAMPLE_NAME ": mismatch target0=%u/%u target1=%u/%u\n",
               counts[0], unexpected[0], counts[1], unexpected[1]);
    else {
#if defined(VULKAN_PS5_INDEPENDENT_BLEND_PROBE)
        printf(SAMPLE_NAME ": PASS target0=%u target1=%u color1=%08x\n",
               counts[0], counts[1], expected[1]);
#else
        printf("mrt: PASS target0=%u target1=%u\n", counts[0], counts[1]);
#endif
    }

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, layout, NULL);
    vkDestroyShaderModule(device, fragment_shader, NULL);
    vkDestroyShaderModule(device, vertex_shader, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    for (uint32_t i = 0; i < 2; ++i) {
        vkDestroyImageView(device, targets[i].view, NULL);
        vkUnmapMemory(device, targets[i].memory);
        vkDestroyImage(device, targets[i].image, NULL);
        vkFreeMemory(device, targets[i].memory, NULL);
    }
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
#if defined(OPENAGC_PROSPERO) && \
    defined(VULKAN_PS5_INDEPENDENT_BLEND_PROBE)
    vulkan_ps5_system_service_exit(SAMPLE_NAME);
#endif
    return status;
}
