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
    assert(argc == 7);
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
    VkShaderModule geometry = shader_module(device, argv[4]);
    VkShaderModule tess_control = shader_module(device, argv[5]);
    VkShaderModule tess_evaluation = shader_module(device, argv[6]);

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
    float scale = 0.75f;
    const VkSpecializationMapEntry vertex_map = {1, 0, sizeof(scale)};
    const VkSpecializationInfo vertex_specialization = {
        1, &vertex_map, sizeof(scale), &scale,
    };
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", &vertex_specialization},
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
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkViewport viewport = {0, 0, 256, 256, 0, 1};
    const VkRect2D scissor = {{0, 0}, {256, 256}};
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
        .depthBiasEnable = VK_TRUE,
        .depthBiasConstantFactor = 2.0f,
        .depthBiasClamp = 0.25f,
        .depthBiasSlopeFactor = -1.5f,
        .lineWidth = 1,
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
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    const VkGraphicsPipelineCreateInfo graphics_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .layout = graphics_layout,
        .renderPass = render_pass,
    };
    VkPipeline graphics_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_info,
                                     NULL, &graphics_pipeline) == VK_SUCCESS);

    const VkVertexInputBindingDescription instance_bindings[] = {
        {0, 8, VK_VERTEX_INPUT_RATE_VERTEX},
        {1, 12, VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    const VkVertexInputAttributeDescription instance_attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0},
    };
    const VkVertexInputBindingDivisorDescriptionEXT divisor = {
        .binding = 1,
        .divisor = 2,
    };
    const VkPipelineVertexInputDivisorStateCreateInfoEXT divisor_state = {
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,
        .vertexBindingDivisorCount = 1,
        .pVertexBindingDivisors = &divisor,
    };
    const VkPipelineVertexInputStateCreateInfo instance_vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = &divisor_state,
        .vertexBindingDescriptionCount = 2,
        .pVertexBindingDescriptions = instance_bindings,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = instance_attributes,
    };
    VkGraphicsPipelineCreateInfo instanced_graphics_info = graphics_info;
    instanced_graphics_info.pVertexInputState = &instance_vertex_input;
    VkPipeline instance_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &instanced_graphics_info,
                                     NULL, &instance_pipeline) == VK_SUCCESS);

    VkVertexInputBindingDivisorDescriptionEXT zero_divisor = divisor;
    zero_divisor.divisor = 0;
    VkPipelineVertexInputDivisorStateCreateInfoEXT zero_divisor_state =
        divisor_state;
    zero_divisor_state.pVertexBindingDivisors = &zero_divisor;
    VkPipelineVertexInputStateCreateInfo zero_vertex_input =
        instance_vertex_input;
    zero_vertex_input.pNext = &zero_divisor_state;
    VkGraphicsPipelineCreateInfo zero_info = graphics_info;
    zero_info.pVertexInputState = &zero_vertex_input;
    VkPipeline rejected_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &zero_info,
                                     NULL, &rejected_pipeline) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(rejected_pipeline == VK_NULL_HANDLE);

    const VkPipelineShaderStageCreateInfo geometry_stages[] = {
        stages[0],
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_GEOMETRY_BIT, geometry, "main", NULL},
        stages[1],
    };
    VkGraphicsPipelineCreateInfo geometry_info = graphics_info;
    geometry_info.stageCount = 3;
    geometry_info.pStages = geometry_stages;
    VkPipeline geometry_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &geometry_info,
                                     NULL, &geometry_pipeline) == VK_SUCCESS);

    const VkPipelineShaderStageCreateInfo tessellation_stages[] = {
        stages[0],
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tess_control, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, tess_evaluation, "main", NULL},
        stages[1],
    };
    const VkPipelineTessellationStateCreateInfo tessellation_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .patchControlPoints = 3,
    };
    const VkPipelineInputAssemblyStateCreateInfo patch_input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
    };
    VkGraphicsPipelineCreateInfo tessellation_info = graphics_info;
    tessellation_info.stageCount = 4;
    tessellation_info.pStages = tessellation_stages;
    tessellation_info.pTessellationState = &tessellation_state;
    tessellation_info.pInputAssemblyState = &patch_input_assembly;
    VkPipeline tessellation_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &tessellation_info,
                                     NULL, &tessellation_pipeline) == VK_SUCCESS);

    vkDestroyPipeline(device, tessellation_pipeline, NULL);
    vkDestroyPipeline(device, geometry_pipeline, NULL);
    vkDestroyPipeline(device, instance_pipeline, NULL);
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyPipeline(device, compute_pipeline, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    vkDestroyPipelineLayout(device, compute_layout, NULL);
    vkDestroyDescriptorSetLayout(device, resource_set, NULL);
    vkDestroyDescriptorSetLayout(device, empty_set, NULL);
    vkDestroyShaderModule(device, compute, NULL);
    vkDestroyShaderModule(device, tess_evaluation, NULL);
    vkDestroyShaderModule(device, tess_control, NULL);
    vkDestroyShaderModule(device, geometry, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyShaderModule(device, vertex, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
