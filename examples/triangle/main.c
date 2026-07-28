#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_ps5_triangle_frag_spv.h"
#include "vulkan_ps5_triangle_vert_spv.h"
#if defined(VULKAN_PS5_GEOMETRY_SAMPLE)
#include "vulkan_ps5_geometry_spv.h"
#define SAMPLE_LABEL "geometry"
#else
#define SAMPLE_LABEL "triangle"
#endif

#define TARGET_WIDTH 256u
#define TARGET_HEIGHT 256u
#define GREEN_RGBA8 0xff00ff00u

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf(SAMPLE_LABEL ": %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if ((compatible_types & (1u << i)) != 0 &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
            return i;
    }
    return UINT32_MAX;
}

int main(void)
{
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView image_view;
    VkRenderPass render_pass;
    VkFramebuffer framebuffer;
    VkShaderModule vertex_shader;
#if defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    VkShaderModule geometry_shader;
#endif
    VkShaderModule fragment_shader;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkFence fence;
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    VkQueryPool query_pool;
#endif
    void *mapped;

    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t physical_count = 1u;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u) {
        printf(SAMPLE_LABEL ": expected one physical device\n");
        return 1;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    const char *device_extensions[] = {
        VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
    };
    const VkPhysicalDeviceHostQueryResetFeatures host_reset = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .hostQueryReset = VK_TRUE,
    };
#endif
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
#if defined(VULKAN_PS5_QUERY_SAMPLE)
        .pNext = &host_reset,
#endif
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
#if defined(VULKAN_PS5_QUERY_SAMPLE)
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
#endif
    };
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    const VkQueryPoolCreateInfo query_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = 1,
    };
    VK_CHECK(vkCreateQueryPool(device, &query_info, NULL, &query_pool));
    vkResetQueryPoolEXT(device, query_pool, 0, 1);
#endif

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {TARGET_WIDTH, TARGET_HEIGHT, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VK_CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX) {
        printf(SAMPLE_LABEL ": no host-visible compatible memory type\n");
        return 1;
    }
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    VK_CHECK(vkAllocateMemory(device, &memory_info, NULL, &memory));
    VK_CHECK(vkBindImageMemory(device, image, memory, 0));
    VK_CHECK(vkMapMemory(device, memory, 0, requirements.size, 0, &mapped));
    memset(mapped, 0, (size_t)requirements.size);
    const VkMappedMemoryRange mapped_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory,
        .offset = 0,
        .size = requirements.size,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &mapped_range));

    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &image_view));
    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference color_attachment = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .pAttachments = &image_view,
        .width = TARGET_WIDTH,
        .height = TARGET_HEIGHT,
        .layers = 1,
    };
    VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));

    const VkShaderModuleCreateInfo vertex_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_triangle_vert_spv),
        .pCode = vulkan_ps5_triangle_vert_spv,
    };
    const VkShaderModuleCreateInfo fragment_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_triangle_frag_spv),
        .pCode = vulkan_ps5_triangle_frag_spv,
    };
#if defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    const VkShaderModuleCreateInfo geometry_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_geometry_spv),
        .pCode = vulkan_ps5_geometry_spv,
    };
#endif
    VK_CHECK(vkCreateShaderModule(
        device, &vertex_shader_info, NULL, &vertex_shader));
    VK_CHECK(vkCreateShaderModule(
        device, &fragment_shader_info, NULL, &fragment_shader));
#if defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    VK_CHECK(vkCreateShaderModule(
        device, &geometry_shader_info, NULL, &geometry_shader));
#endif
    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VK_CHECK(vkCreatePipelineLayout(
        device, &pipeline_layout_info, NULL, &pipeline_layout));
    const VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader,
            .pName = "main",
        },
#if defined(VULKAN_PS5_GEOMETRY_SAMPLE)
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
            .module = geometry_shader,
            .pName = "main",
        },
#endif
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader,
            .pName = "main",
        },
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkViewport viewport = {
        0, 0, TARGET_WIDTH, TARGET_HEIGHT, 0, 1,
    };
    const VkRect2D scissor = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}};
    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
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
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = sizeof(stages) / sizeof(stages[0]),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .layout = pipeline_layout,
        .renderPass = render_pass,
    };
    VK_CHECK(vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));

    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VK_CHECK(vkCreateCommandPool(
        device, &command_pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo command_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(
        device, &command_allocate_info, &command));
    const VkCommandBufferBeginInfo command_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(command, &command_begin));
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}},
    };
#if defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    vkCmdResetQueryPool(command, query_pool, 0, 1);
#endif
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    vkCmdBeginQuery(command, query_pool, 0, 0);
#endif
#if !defined(VULKAN_PS5_QUERY_IDLE_ONLY)
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
#endif
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    vkCmdEndQuery(command, query_pool, 0);
#endif
    vkCmdEndRenderPass(command);
    VK_CHECK(vkEndCommandBuffer(command));

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    printf("query: stage submit\n");
#endif
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, fence));
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    printf("query: stage submitted\n");
#endif
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    printf("query: stage fence\n");
#endif
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1, &mapped_range));

    const uint32_t *pixels = mapped;
    uint32_t green_count = 0;
    uint32_t unexpected_count = 0;
    for (uint32_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        if (pixels[i] == GREEN_RGBA8)
            ++green_count;
        else if (pixels[i] != 0u)
            ++unexpected_count;
    }
    int status = 0;
    uint32_t center = pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
        TARGET_WIDTH / 2u];
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    uint64_t query_data[2] = {0, 0};
    VkResult query_result = vkGetQueryPoolResults(device, query_pool, 0, 1,
        sizeof(query_data), query_data, sizeof(query_data),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    printf("query: stage result=%d samples=%llu available=%llu\n",
        query_result, (unsigned long long)query_data[0],
        (unsigned long long)query_data[1]);
#endif
    int image_ok;
#if defined(VULKAN_PS5_QUERY_IDLE_ONLY)
    image_ok = green_count == 0u && unexpected_count == 0u && center == 0u &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#else
#if defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    image_ok = green_count >= 3500u && green_count <= 6000u &&
        unexpected_count == 0u && center == GREEN_RGBA8 &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#else
    image_ok = green_count >= 16000u && green_count <= 21000u &&
        unexpected_count == 0u && center == GREEN_RGBA8 &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#endif
#endif
    if (!image_ok
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
        || query_result != VK_SUCCESS || query_data[1] != 1u ||
        query_data[0] != green_count
#endif
        ) {
#if defined(VULKAN_PS5_QUERY_IDLE_ONLY)
        printf("query_idle: mismatch result=%d samples=%llu available=%llu green=%u unexpected=%u\n",
            query_result, (unsigned long long)query_data[0],
            (unsigned long long)query_data[1], green_count, unexpected_count);
#elif defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
        printf("query: mismatch result=%d samples=%llu available=%llu green=%u unexpected=%u\n",
            query_result, (unsigned long long)query_data[0],
            (unsigned long long)query_data[1], green_count, unexpected_count);
#else
        printf(SAMPLE_LABEL ": mismatch green=%u unexpected=%u center=%08x\n",
            green_count, unexpected_count, center);
#endif
        status = 1;
    } else {
#if defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY)
        printf("query_lifecycle: PASS green=%u\n", green_count);
#elif defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
        printf("query_reset: PASS green=%u\n", green_count);
#elif defined(VULKAN_PS5_QUERY_IDLE_ONLY)
        printf("query_idle: PASS samples=%llu available=%llu\n",
            (unsigned long long)query_data[0],
            (unsigned long long)query_data[1]);
#elif defined(VULKAN_PS5_QUERY_SAMPLE)
        printf("query: PASS samples=%llu green=%u\n",
            (unsigned long long)query_data[0], green_count);
#else
        printf(SAMPLE_LABEL ": PASS %u green pixels\n", green_count);
#endif
    }

    vkDestroyFence(device, fence, NULL);
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    vkDestroyQueryPool(device, query_pool, NULL);
#endif
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyShaderModule(device, fragment_shader, NULL);
#if defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    vkDestroyShaderModule(device, geometry_shader, NULL);
#endif
    vkDestroyShaderModule(device, vertex_shader, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyImageView(device, image_view, NULL);
    vkUnmapMemory(device, memory);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return status;
}
