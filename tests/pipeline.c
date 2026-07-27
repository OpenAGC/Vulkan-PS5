#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t *read_spirv(const char *path, size_t *size) {
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

static VkShaderModule shader_module(VkDevice device, const char *path) {
    size_t size;
    uint32_t *code = read_spirv(path, &size);
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code,
    };
    VkShaderModule module = VK_NULL_HANDLE;
    assert(vkCreateShaderModule(device, &info, NULL, &module) == VK_SUCCESS);
    free(code);
    return module;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);
    uint32_t physical_count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, &physical) == VK_SUCCESS);
    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
    };
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);

    VkShaderModule vertex = shader_module(device, argv[1]);
    VkShaderModule fragment = shader_module(device, argv[2]);
    VkShaderModule compute = shader_module(device, argv[3]);

    VkDescriptorSetLayout empty_set = VK_NULL_HANDLE, resource_set = VK_NULL_HANDLE;
    const VkDescriptorSetLayoutCreateInfo empty_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    };
    assert(vkCreateDescriptorSetLayout(device, &empty_info, NULL, &empty_set) == VK_SUCCESS);
    const VkDescriptorSetLayoutBinding storage_binding = {
        .binding = 3,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo resource_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &storage_binding,
    };
    assert(vkCreateDescriptorSetLayout(device, &resource_info, NULL, &resource_set) == VK_SUCCESS);
    const VkDescriptorSetLayout sets[] = {empty_set, resource_set};
    const VkPushConstantRange push = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = 4,
    };
    const VkPipelineLayoutCreateInfo compute_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = sets,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    };
    VkPipelineLayout compute_layout = VK_NULL_HANDLE;
    assert(vkCreatePipelineLayout(device, &compute_layout_info, NULL,
                                  &compute_layout) == VK_SUCCESS);
    const VkComputePipelineCreateInfo compute_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = compute,
            .pName = "main",
        },
        .layout = compute_layout,
    };
    VkPipeline compute_pipeline = VK_NULL_HANDLE;
    assert(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute_info,
                                    NULL, &compute_pipeline) == VK_SUCCESS);

    const VkPipelineLayoutCreateInfo graphics_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkPipelineLayout graphics_layout = VK_NULL_HANDLE;
    assert(vkCreatePipelineLayout(device, &graphics_layout_info, NULL,
                                  &graphics_layout) == VK_SUCCESS);
    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
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
    VkRenderPass render_pass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass) == VK_SUCCESS);
    float gain = 2.0f;
    const VkSpecializationMapEntry map = {0, 0, sizeof(gain)};
    const VkSpecializationInfo specialization = {1, &map, sizeof(gain), &gain};
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", &specialization},
    };
    const VkVertexInputBindingDescription vertex_binding = {0, 20, VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription vertex_attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 8},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = vertex_attributes,
    };
    const VkGraphicsPipelineCreateInfo graphics_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .layout = graphics_layout,
        .renderPass = render_pass,
    };
    VkPipeline graphics_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_info,
                                     NULL, &graphics_pipeline) == VK_SUCCESS);

    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyPipeline(device, compute_pipeline, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    vkDestroyPipelineLayout(device, compute_layout, NULL);
    vkDestroyDescriptorSetLayout(device, resource_set, NULL);
    vkDestroyDescriptorSetLayout(device, empty_set, NULL);
    vkDestroyShaderModule(device, compute, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyShaderModule(device, vertex, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
