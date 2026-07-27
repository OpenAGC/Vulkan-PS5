#include <vulkan/vulkan.h>
#include <agc_driver_debug.h>
#include <agc_pm4.h>

#include "../src/vulkan_ps5_internal.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t *read_spirv(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && (length & 3) == 0 && fseek(file, 0, SEEK_SET) == 0);
    uint32_t *code = malloc((size_t)length);
    assert(code && fread(code, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    return code;
}

static VkShaderModule shader_module(VkDevice device, const char *path)
{
    size_t size;
    uint32_t *code = read_spirv(path, &size);
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code,
    };
    VkShaderModule module;
    assert(vkCreateShaderModule(device, &info, NULL, &module) == VK_SUCCESS);
    free(code);
    return module;
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);
    uint32_t physical_count = 1;
    VkPhysicalDevice physical;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, &physical) == VK_SUCCESS);
    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    VkDevice device;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);

    VkShaderModule module = shader_module(device, argv[1]);
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkPipelineLayout layout;
    assert(vkCreatePipelineLayout(device, &layout_info, NULL, &layout) == VK_SUCCESS);
    const VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = module,
            .pName = "main",
        },
        .layout = layout,
    };
    VkPipeline pipeline;
    assert(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                    &pipeline_info, NULL, &pipeline) == VK_SUCCESS);

    VkShaderModule vertex = shader_module(device, argv[2]);
    VkShaderModule fragment = shader_module(device, argv[3]);
    const VkPipelineLayoutCreateInfo graphics_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkPipelineLayout graphics_layout;
    assert(vkCreatePipelineLayout(device, &graphics_layout_info, NULL,
                                  &graphics_layout) == VK_SUCCESS);
    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference color = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass render_pass;
    assert(vkCreateRenderPass(device, &render_pass_info, NULL,
                              &render_pass) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo graphics_stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", NULL},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkGraphicsPipelineCreateInfo graphics_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = graphics_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .layout = graphics_layout,
        .renderPass = render_pass,
    };
    VkPipeline graphics_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &graphics_info, NULL,
                                     &graphics_pipeline) == VK_SUCCESS);

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    };
    VkCommandPool pool;
    assert(vkCreateCommandPool(device, &pool_info, NULL, &pool) == VK_SUCCESS);
    const VkCommandBufferAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command;
    assert(vkAllocateCommandBuffers(device, &allocate_info, &command) == VK_SUCCESS);
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdDispatch(command, 3, 5, 7);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);

    const uint32_t *dwords;
    uint32_t count = vk_ps5_command_buffer_dwords(command, &dwords);
    assert(count > 20);
    bool found_dispatch = false, found_draw = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (((dwords[i] >> 8) & 0xffu) == AGC_PM4_OP_DISPATCH_DIRECT) {
            assert(i + 4 < count);
            assert(dwords[i + 1] == 3 && dwords[i + 2] == 5 && dwords[i + 3] == 7);
            found_dispatch = true;
        } else if (((dwords[i] >> 8) & 0xffu) == AGC_PM4_OP_DRAW_INDEX_AUTO) {
            assert(i + 2 < count && dwords[i + 1] == 3);
            found_draw = true;
        }
    }
    assert(found_dispatch && found_draw);

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    assert(vkCreateFence(device, &fence_info, NULL, &fence) == VK_SUCCESS);
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    assert(vkQueueSubmit(queue, 1, &submit_info, fence) == VK_SUCCESS);
    assert(vkGetFenceStatus(device, fence) == VK_SUCCESS);
    const AgcCommandBufferSubmit *submitted = agcDriverDebugLastDcbSubmit();
    assert(submitted && submitted->dword_count > count);
    const uint32_t *submitted_dwords =
        (const uint32_t *)(uintptr_t)submitted->command_address;
    bool found_release = false;
    for (uint32_t n = count; n < submitted->dword_count; ++n)
        found_release |= ((submitted_dwords[n] >> 8) & 0xffu) ==
            AGC_PM4_OP_RELEASE_MEM;
    assert(found_release);

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyShaderModule(device, vertex, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, layout, NULL);
    vkDestroyShaderModule(device, module, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
