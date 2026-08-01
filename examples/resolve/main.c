#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>

#include "../system_service_exit.h"
#include "source_vert_spv.h"
#include "source_frag_spv.h"

#define WIDTH 32u
#define HEIGHT 32u
#define EXPECTED_COLOR UINT32_C(0xff00ff80)

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf("resolve: %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

typedef struct TestImage {
    VkImage image;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize row_pitch;
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
    VkSampleCountFlagBits samples, VkImageTiling tiling,
    VkImageUsageFlags usage, TestImage *image)
{
    const VkImageCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {WIDTH, HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = samples,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = tiling == VK_IMAGE_TILING_LINEAR ?
            VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED,
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
    if (result == VK_SUCCESS && tiling == VK_IMAGE_TILING_LINEAR)
        result = vkMapMemory(device, image->memory, 0u, requirements.size, 0u,
            (void **)&image->mapped);
    if (result != VK_SUCCESS)
        return result;
    if (tiling == VK_IMAGE_TILING_LINEAR) {
        const VkImageSubresource subresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        };
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(device, image->image, &subresource,
            &layout);
        image->mapped += layout.offset;
        image->row_pitch = layout.rowPitch;
    }
    return VK_SUCCESS;
}

static int run_resolve(void)
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
        .pEnabledFeatures = &(VkPhysicalDeviceFeatures){
            .sampleRateShading = VK_TRUE,
        },
    };
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));

    TestImage source = {0};
    TestImage destination = {0};
    VK_CHECK(create_image(physical, device, VK_SAMPLE_COUNT_4_BIT,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &source));
    VK_CHECK(create_image(physical, device, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        &destination));
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = source.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        },
    };
    VkImageView source_view;
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &source_view));
    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_4_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference color_reference = {
        .attachment = 0u,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
    VkRenderPass render_pass;
    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL,
        &render_pass));
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, NULL,
        &pipeline_layout));
    const VkShaderModuleCreateInfo vertex_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_resolve_source_vert_spv),
        .pCode = vulkan_ps5_resolve_source_vert_spv,
    };
    const VkShaderModuleCreateInfo fragment_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_resolve_source_frag_spv),
        .pCode = vulkan_ps5_resolve_source_frag_spv,
    };
    VkShaderModule vertex;
    VkShaderModule fragment;
    VK_CHECK(vkCreateShaderModule(device, &vertex_info, NULL, &vertex));
    VK_CHECK(vkCreateShaderModule(device, &fragment_info, NULL, &fragment));
    const VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment,
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
    const VkViewport viewport = {0.0f, 0.0f, WIDTH, HEIGHT, 0.0f, 1.0f};
    const VkRect2D scissor = {{0, 0}, {WIDTH, HEIGHT}};
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
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_4_BIT,
        .sampleShadingEnable = VK_TRUE,
        .minSampleShading = 1.0f,
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
    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2u,
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
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
        &pipeline_info, NULL, &pipeline));
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1u,
        .pAttachments = &source_view,
        .width = WIDTH,
        .height = HEIGHT,
        .layers = 1u,
    };
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL,
        &framebuffer));

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
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {WIDTH, HEIGHT}},
    };
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(command, 6u, 1u, 0u, 0u);
    vkCmdEndRenderPass(command);
    const VkImageResolve region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .srcOffset = {0, 0, 0},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .dstOffset = {0, 0, 0},
        .extent = {WIDTH, HEIGHT, 1u},
    };
    vkCmdResolveImage(command, source.image, VK_IMAGE_LAYOUT_GENERAL,
        destination.image, VK_IMAGE_LAYOUT_GENERAL, 1u, &region);
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
        UINT64_C(5000000000)));
    const VkMappedMemoryRange readback = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = destination.memory,
        .size = VK_WHOLE_SIZE,
    };
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1u, &readback));

    int status = 0;
#if defined(OPENAGC_PROSPERO)
    uint32_t matched = 0u;
    for (uint32_t y = 0u; y < HEIGHT && !status; ++y) {
        const uint32_t *row = (const uint32_t *)(destination.mapped +
            y * destination.row_pitch);
        for (uint32_t x = 0u; x < WIDTH; ++x) {
            if (row[x] != EXPECTED_COLOR) {
                printf("resolve: mismatch x=%u y=%u got=%08x expected=%08x\n",
                    x, y, row[x], EXPECTED_COLOR);
                status = 1;
                break;
            }
            matched++;
        }
    }
    if (!status)
        printf("resolve: PASS pixels=%u color=%08x samples=4\n",
            matched, EXPECTED_COLOR);
#else
    puts("resolve: PASS command recording");
#endif

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyShaderModule(device, vertex, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyImageView(device, source_view, NULL);
    vkUnmapMemory(device, destination.memory);
    vkDestroyImage(device, destination.image, NULL);
    vkDestroyImage(device, source.image, NULL);
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
    puts("resolve: stage=start");
    const int status = run_resolve();
    if (status)
        printf("resolve: FAIL status=%d\n", status);
    printf("resolve: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("resolve");
#endif
    return status;
}
