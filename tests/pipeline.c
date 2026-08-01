#include <vulkan/vulkan.h>

#include "../src/vulkan_ps5_internal.h"

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
    assert(argc == 10);
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
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
        .depthClipEnable = VK_TRUE,
    };
    const VkPhysicalDeviceScalarBlockLayoutFeatures scalar_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
        .pNext = &depth_clip_features,
        .scalarBlockLayout = VK_TRUE,
    };
    const char *device_extensions[] = {
        VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
        VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &scalar_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = &(const VkPhysicalDeviceFeatures){
            .alphaToOne = VK_TRUE,
            .imageCubeArray = VK_TRUE,
            .multiViewport = VK_TRUE,
        },
    };
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);

    VkShaderModule vertex = shader_module(device, argv[1]);
    VkShaderModule fragment = shader_module(device, argv[2]);
    VkShaderModule compute = shader_module(device, argv[3]);
    VkShaderModule geometry = shader_module(device, argv[4]);
    VkShaderModule tess_control = shader_module(device, argv[5]);
    VkShaderModule tess_evaluation = shader_module(device, argv[6]);
    VkShaderModule sample_fragment = shader_module(device, argv[7]);
    VkShaderModule cube_array_fragment = shader_module(device, argv[8]);
    VkShaderModule scalar_compute = shader_module(device, argv[9]);

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
    assert(vk_ps5_pipeline_has_native_shaders(compute_pipeline));
    assert(vk_ps5_pipeline_has_native_compute_pipeline(compute_pipeline));
    VkComputePipelineCreateInfo scalar_compute_info = compute_info;
    scalar_compute_info.stage.module = scalar_compute;
    VkPipeline scalar_compute_pipeline = VK_NULL_HANDLE;
    assert(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                    &scalar_compute_info, NULL,
                                    &scalar_compute_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_shaders(scalar_compute_pipeline));
    assert(vk_ps5_pipeline_has_native_compute_pipeline(
        scalar_compute_pipeline));

    const VkDescriptorSetLayoutBinding sampled_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo sampled_set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampled_binding,
    };
    VkDescriptorSetLayout sampled_set = VK_NULL_HANDLE;
    assert(vkCreateDescriptorSetLayout(device, &sampled_set_info, NULL,
                                       &sampled_set) == VK_SUCCESS);
    const VkPipelineLayoutCreateInfo graphics_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &sampled_set,
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
        .depthClampEnable = VK_TRUE,
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
        .logicOpEnable = VK_TRUE,
        .logicOp = VK_LOGIC_OP_XOR,
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
    assert(vk_ps5_pipeline_has_native_shaders(graphics_pipeline));
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(graphics_pipeline));
    VkPipelineRasterizationStateCreateInfo native_rasterization = rasterization;
    native_rasterization.depthClampEnable = VK_FALSE;
    native_rasterization.depthBiasEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo native_blend = blend;
    native_blend.logicOpEnable = VK_FALSE;
    VkGraphicsPipelineCreateInfo native_graphics_info = graphics_info;
    native_graphics_info.pRasterizationState = &native_rasterization;
    native_graphics_info.pColorBlendState = &native_blend;
    VkPipeline native_graphics_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &native_graphics_info, NULL, &native_graphics_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_shaders(native_graphics_pipeline));
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        native_graphics_pipeline));
    VkPipelineRasterizationStateCreateInfo clamp_rasterization =
        native_rasterization;
    clamp_rasterization.depthClampEnable = VK_TRUE;
    VkGraphicsPipelineCreateInfo clamp_graphics_info = native_graphics_info;
    clamp_graphics_info.pRasterizationState = &clamp_rasterization;
    VkPipeline clamp_graphics_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &clamp_graphics_info, NULL, &clamp_graphics_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        clamp_graphics_pipeline));
    const VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip_off = {
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT,
        .depthClipEnable = VK_FALSE,
    };
    VkPipelineRasterizationStateCreateInfo unclipped_rasterization =
        native_rasterization;
    unclipped_rasterization.pNext = &depth_clip_off;
    VkGraphicsPipelineCreateInfo unclipped_graphics_info =
        native_graphics_info;
    unclipped_graphics_info.pRasterizationState = &unclipped_rasterization;
    VkPipeline unclipped_graphics_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &unclipped_graphics_info, NULL, &unclipped_graphics_pipeline) ==
        VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        unclipped_graphics_pipeline));
    assert(vk_ps5_pipeline_native_rasterization_flags(
        unclipped_graphics_pipeline) ==
        AGC_RASTERIZATION_DEPTH_CLIP_DISABLE_BIT);
    VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip_on =
        depth_clip_off;
    depth_clip_on.depthClipEnable = VK_TRUE;
    VkPipelineRasterizationStateCreateInfo clipped_clamp_rasterization =
        clamp_rasterization;
    clipped_clamp_rasterization.pNext = &depth_clip_on;
    VkGraphicsPipelineCreateInfo clipped_clamp_graphics_info =
        native_graphics_info;
    clipped_clamp_graphics_info.pRasterizationState =
        &clipped_clamp_rasterization;
    VkPipeline clipped_clamp_graphics_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &clipped_clamp_graphics_info, NULL,
        &clipped_clamp_graphics_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_native_rasterization_flags(
        clipped_clamp_graphics_pipeline) ==
        AGC_RASTERIZATION_DEPTH_CLIP_ENABLE_BIT);
    VkPipelineRasterizationDepthClipStateCreateInfoEXT bad_depth_clip =
        depth_clip_off;
    bad_depth_clip.flags = 1u;
    unclipped_rasterization.pNext = &bad_depth_clip;
    VkPipeline bad_depth_clip_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &unclipped_graphics_info, NULL, &bad_depth_clip_pipeline) ==
        VK_ERROR_FEATURE_NOT_PRESENT);
    assert(bad_depth_clip_pipeline == VK_NULL_HANDLE);
    VkPipelineMultisampleStateCreateInfo alpha_to_one_state = multisample;
    alpha_to_one_state.alphaToOneEnable = VK_TRUE;
    VkGraphicsPipelineCreateInfo alpha_to_one_info = graphics_info;
    alpha_to_one_info.pMultisampleState = &alpha_to_one_state;
    VkPipeline alpha_to_one_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &alpha_to_one_info, NULL,
                                     &alpha_to_one_pipeline) == VK_SUCCESS);
    const VkFormat dynamic_color_format = VK_FORMAT_R8G8B8A8_UNORM;
    const VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &dynamic_color_format,
    };
    VkGraphicsPipelineCreateInfo dynamic_rendering_info = graphics_info;
    dynamic_rendering_info.pNext = &rendering_info;
    dynamic_rendering_info.renderPass = VK_NULL_HANDLE;
    dynamic_rendering_info.subpass = 0;
    VkPipeline dynamic_rendering_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &dynamic_rendering_info, NULL, &dynamic_rendering_pipeline) ==
        VK_SUCCESS);
    const VkViewport multi_viewports[] = {
        {0, 0, 128, 256, 0, 1},
        {128, 0, 128, 256, 0.25f, 0.75f},
    };
    const VkRect2D multi_scissors[] = {
        {{0, 0}, {128, 256}},
        {{128, 0}, {128, 256}},
    };
    const VkPipelineViewportStateCreateInfo multi_viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 2,
        .pViewports = multi_viewports,
        .scissorCount = 2,
        .pScissors = multi_scissors,
    };
    VkGraphicsPipelineCreateInfo multi_viewport_info = graphics_info;
    multi_viewport_info.pViewportState = &multi_viewport_state;
    VkPipeline multi_viewport_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &multi_viewport_info, NULL, &multi_viewport_pipeline) == VK_SUCCESS);
    const VkDynamicState viewport_dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    const VkPipelineDynamicStateCreateInfo viewport_dynamic_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = viewport_dynamic_states,
    };
    VkPipelineViewportStateCreateInfo dynamic_viewport_state =
        multi_viewport_state;
    dynamic_viewport_state.pViewports = NULL;
    dynamic_viewport_state.pScissors = NULL;
    VkGraphicsPipelineCreateInfo dynamic_viewport_info = multi_viewport_info;
    dynamic_viewport_info.pViewportState = &dynamic_viewport_state;
    dynamic_viewport_info.pDynamicState = &viewport_dynamic_info;
    VkPipeline dynamic_viewport_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &dynamic_viewport_info, NULL, &dynamic_viewport_pipeline) == VK_SUCCESS);
    VkPipelineViewportStateCreateInfo invalid_viewport_state =
        multi_viewport_state;
    invalid_viewport_state.viewportCount = 17;
    invalid_viewport_state.scissorCount = 17;
    VkGraphicsPipelineCreateInfo invalid_viewport_info = multi_viewport_info;
    invalid_viewport_info.pViewportState = &invalid_viewport_state;
    VkPipeline invalid_viewport_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &invalid_viewport_info, NULL, &invalid_viewport_pipeline) ==
        VK_ERROR_INITIALIZATION_FAILED);
    assert(invalid_viewport_pipeline == VK_NULL_HANDLE);
    VkAttachmentDescription msaa_attachment = attachment;
    msaa_attachment.samples = VK_SAMPLE_COUNT_4_BIT;
    const VkRenderPassCreateInfo msaa_render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &msaa_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass msaa_render_pass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(device, &msaa_render_pass_info, NULL,
                              &msaa_render_pass) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo msaa_stages[] = {
        stages[0],
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, sample_fragment, "main", NULL},
    };
    VkPipelineMultisampleStateCreateInfo msaa_state = multisample;
    msaa_state.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
    const uint32_t msaa_mask = 0xdu;
    msaa_state.pSampleMask = &msaa_mask;
    VkGraphicsPipelineCreateInfo msaa_info = graphics_info;
    msaa_info.pStages = msaa_stages;
    msaa_info.pMultisampleState = &msaa_state;
    msaa_info.renderPass = msaa_render_pass;
    VkPipeline msaa_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &msaa_info,
                                     NULL, &msaa_pipeline) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo cube_array_stages[] = {
        stages[0],
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, cube_array_fragment, "main", NULL},
    };
    VkGraphicsPipelineCreateInfo cube_array_info = graphics_info;
    cube_array_info.pStages = cube_array_stages;
    VkPipeline cube_array_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &cube_array_info, NULL,
                                     &cube_array_pipeline) == VK_SUCCESS);
    VkPipelineInputAssemblyStateCreateInfo point_input_assembly = input_assembly;
    point_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    VkGraphicsPipelineCreateInfo point_list_info = graphics_info;
    point_list_info.pInputAssemblyState = &point_input_assembly;
    point_list_info.pRasterizationState = &native_rasterization;
    point_list_info.pColorBlendState = &native_blend;
    VkPipeline point_list_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &point_list_info, NULL,
                                     &point_list_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        point_list_pipeline));
    VkPipelineInputAssemblyStateCreateInfo line_input_assembly = input_assembly;
    line_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    const VkPipelineRasterizationLineStateCreateInfo rectangular_line = {
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO,
        .lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR,
    };
    VkPipelineRasterizationStateCreateInfo wide_line_raster = rasterization;
    wide_line_raster.pNext = &rectangular_line;
    wide_line_raster.depthBiasEnable = VK_FALSE;
    wide_line_raster.lineWidth = 8.0f;
    VkGraphicsPipelineCreateInfo line_list_info = graphics_info;
    line_list_info.pInputAssemblyState = &line_input_assembly;
    line_list_info.pRasterizationState = &wide_line_raster;
    line_list_info.pColorBlendState = &native_blend;
    VkPipeline line_list_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &line_list_info, NULL,
                                     &line_list_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        line_list_pipeline));
    VkPipelineRasterizationLineStateCreateInfo unsupported_bresenham =
        rectangular_line;
    unsupported_bresenham.lineRasterizationMode =
        VK_LINE_RASTERIZATION_MODE_BRESENHAM;
    VkPipelineRasterizationStateCreateInfo unsupported_line_raster =
        wide_line_raster;
    unsupported_line_raster.pNext = &unsupported_bresenham;
    VkGraphicsPipelineCreateInfo unsupported_line_info = line_list_info;
    unsupported_line_info.pRasterizationState = &unsupported_line_raster;
    VkPipeline unsupported_line_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &unsupported_line_info, NULL, &unsupported_line_pipeline) ==
        VK_ERROR_FEATURE_NOT_PRESENT);
    VkPipelineInputAssemblyStateCreateInfo line_strip_input_assembly =
        input_assembly;
    line_strip_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    VkGraphicsPipelineCreateInfo line_strip_info = line_list_info;
    line_strip_info.pInputAssemblyState = &line_strip_input_assembly;
    VkPipeline line_strip_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &line_strip_info, NULL,
                                     &line_strip_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        line_strip_pipeline));
    VkPipelineInputAssemblyStateCreateInfo triangle_strip_input_assembly =
        input_assembly;
    triangle_strip_input_assembly.topology =
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkGraphicsPipelineCreateInfo triangle_strip_info = graphics_info;
    triangle_strip_info.pInputAssemblyState =
        &triangle_strip_input_assembly;
    triangle_strip_info.pRasterizationState = &native_rasterization;
    triangle_strip_info.pColorBlendState = &native_blend;
    VkPipeline triangle_strip_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &triangle_strip_info, NULL,
                                     &triangle_strip_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        triangle_strip_pipeline));
    VkPipelineInputAssemblyStateCreateInfo restart_input_assembly =
        triangle_strip_input_assembly;
    restart_input_assembly.primitiveRestartEnable = VK_TRUE;
    VkGraphicsPipelineCreateInfo restart_info = triangle_strip_info;
    restart_info.pInputAssemblyState = &restart_input_assembly;
    VkPipeline restart_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &restart_info, NULL, &restart_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(restart_pipeline));
    VkPipelineInputAssemblyStateCreateInfo invalid_list_restart =
        input_assembly;
    invalid_list_restart.primitiveRestartEnable = VK_TRUE;
    VkGraphicsPipelineCreateInfo invalid_list_restart_info =
        native_graphics_info;
    invalid_list_restart_info.pInputAssemblyState = &invalid_list_restart;
    VkPipeline invalid_list_restart_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &invalid_list_restart_info, NULL, &invalid_list_restart_pipeline) ==
        VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_list_restart_pipeline == VK_NULL_HANDLE);
    VkPipelineRasterizationStateCreateInfo culled_rasterization =
        native_rasterization;
    culled_rasterization.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
    culled_rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkGraphicsPipelineCreateInfo culled_info = native_graphics_info;
    culled_info.pRasterizationState = &culled_rasterization;
    VkPipeline culled_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &culled_info, NULL, &culled_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(culled_pipeline));
    VkPipelineRasterizationStateCreateInfo discard_rasterization =
        native_rasterization;
    discard_rasterization.rasterizerDiscardEnable = VK_TRUE;
    VkGraphicsPipelineCreateInfo discard_info = native_graphics_info;
    discard_info.pRasterizationState = &discard_rasterization;
    VkPipeline discard_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &discard_info, NULL, &discard_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(discard_pipeline));
    VkPipelineInputAssemblyStateCreateInfo triangle_fan_input_assembly =
        input_assembly;
    triangle_fan_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    VkGraphicsPipelineCreateInfo triangle_fan_info = native_graphics_info;
    triangle_fan_info.pInputAssemblyState = &triangle_fan_input_assembly;
    VkPipeline triangle_fan_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &triangle_fan_info, NULL,
                                     &triangle_fan_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        triangle_fan_pipeline));
    const VkDynamicState line_width_state = VK_DYNAMIC_STATE_LINE_WIDTH;
    const VkPipelineDynamicStateCreateInfo dynamic_line_width = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1,
        .pDynamicStates = &line_width_state,
    };
    wide_line_raster.lineWidth = 0.0f;
    VkGraphicsPipelineCreateInfo dynamic_line_info = line_list_info;
    dynamic_line_info.pDynamicState = &dynamic_line_width;
    VkPipeline dynamic_line_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &dynamic_line_info, NULL,
                                     &dynamic_line_pipeline) == VK_SUCCESS);
    wide_line_raster.lineWidth = 0.5f;
    VkPipeline invalid_line_width = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &line_list_info, NULL,
                                     &invalid_line_width) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_line_width == VK_NULL_HANDLE);
    wide_line_raster.lineWidth = 64.125f;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &line_list_info, NULL,
                                     &invalid_line_width) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_line_width == VK_NULL_HANDLE);
    VkPipelineRasterizationStateCreateInfo non_solid_raster = rasterization;
    VkGraphicsPipelineCreateInfo non_solid_info = graphics_info;
    non_solid_info.pRasterizationState = &non_solid_raster;
    non_solid_raster.polygonMode = VK_POLYGON_MODE_LINE;
    VkPipeline line_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &non_solid_info, NULL,
                                     &line_pipeline) == VK_SUCCESS);
    non_solid_raster.polygonMode = VK_POLYGON_MODE_POINT;
    VkPipeline point_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &non_solid_info, NULL,
                                     &point_pipeline) == VK_SUCCESS);
    non_solid_raster.polygonMode = VK_POLYGON_MODE_FILL_RECTANGLE_NV;
    VkPipeline invalid_polygon_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &non_solid_info, NULL,
                                     &invalid_polygon_pipeline) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_polygon_pipeline == VK_NULL_HANDLE);
    VkPipelineColorBlendStateCreateInfo invalid_logic_blend = blend;
    invalid_logic_blend.logicOp = (VkLogicOp)VK_LOGIC_OP_MAX_ENUM;
    VkGraphicsPipelineCreateInfo invalid_logic_info = graphics_info;
    invalid_logic_info.pColorBlendState = &invalid_logic_blend;
    VkPipeline invalid_logic_pipeline = VK_NULL_HANDLE;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &invalid_logic_info, NULL,
                                     &invalid_logic_pipeline) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_logic_pipeline == VK_NULL_HANDLE);

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
    vkDestroyPipeline(device, dynamic_viewport_pipeline, NULL);
    vkDestroyPipeline(device, multi_viewport_pipeline, NULL);
    vkDestroyPipeline(device, cube_array_pipeline, NULL);
    vkDestroyPipeline(device, msaa_pipeline, NULL);
    vkDestroyPipeline(device, geometry_pipeline, NULL);
    vkDestroyPipeline(device, instance_pipeline, NULL);
    vkDestroyPipeline(device, point_pipeline, NULL);
    vkDestroyPipeline(device, line_pipeline, NULL);
    vkDestroyPipeline(device, point_list_pipeline, NULL);
    vkDestroyPipeline(device, dynamic_line_pipeline, NULL);
    vkDestroyPipeline(device, triangle_fan_pipeline, NULL);
    vkDestroyPipeline(device, discard_pipeline, NULL);
    vkDestroyPipeline(device, culled_pipeline, NULL);
    vkDestroyPipeline(device, restart_pipeline, NULL);
    vkDestroyPipeline(device, triangle_strip_pipeline, NULL);
    vkDestroyPipeline(device, line_strip_pipeline, NULL);
    vkDestroyPipeline(device, line_list_pipeline, NULL);
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyPipeline(device, native_graphics_pipeline, NULL);
vkDestroyPipeline(device, clamp_graphics_pipeline, NULL);
vkDestroyPipeline(device, unclipped_graphics_pipeline, NULL);
vkDestroyPipeline(device, clipped_clamp_graphics_pipeline, NULL);
    vkDestroyPipeline(device, alpha_to_one_pipeline, NULL);
    vkDestroyPipeline(device, dynamic_rendering_pipeline, NULL);
    vkDestroyPipeline(device, compute_pipeline, NULL);
    vkDestroyPipeline(device, scalar_compute_pipeline, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyRenderPass(device, msaa_render_pass, NULL);
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    vkDestroyPipelineLayout(device, compute_layout, NULL);
    vkDestroyDescriptorSetLayout(device, resource_set, NULL);
    vkDestroyDescriptorSetLayout(device, sampled_set, NULL);
    vkDestroyDescriptorSetLayout(device, empty_set, NULL);
    vkDestroyShaderModule(device, compute, NULL);
    vkDestroyShaderModule(device, scalar_compute, NULL);
    vkDestroyShaderModule(device, sample_fragment, NULL);
    vkDestroyShaderModule(device, cube_array_fragment, NULL);
    vkDestroyShaderModule(device, tess_evaluation, NULL);
    vkDestroyShaderModule(device, tess_control, NULL);
    vkDestroyShaderModule(device, geometry, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyShaderModule(device, vertex, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
