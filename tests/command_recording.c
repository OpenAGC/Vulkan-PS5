#include <vulkan/vulkan.h>
#include <agc_driver_debug.h>
#include <agc_pm4.h>
#include <agc_registers.h>
#include <agc_shader.h>

#include "../src/vulkan_ps5_internal.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 256,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer output_buffer;
    assert(vkCreateBuffer(device, &buffer_info, NULL, &output_buffer) == VK_SUCCESS);
    VkMemoryRequirements buffer_requirements;
    vkGetBufferMemoryRequirements(device, output_buffer, &buffer_requirements);
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_requirements.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory output_memory;
    assert(vkAllocateMemory(device, &memory_info, NULL, &output_memory) == VK_SUCCESS);
    assert(vkBindBufferMemory(device, output_buffer, output_memory, 0) == VK_SUCCESS);
    const VkBufferCreateInfo vertex_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4u * 2u * sizeof(float),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer vertex_buffer;
    assert(vkCreateBuffer(device, &vertex_buffer_info, NULL,
                          &vertex_buffer) == VK_SUCCESS);
    VkMemoryRequirements vertex_requirements;
    vkGetBufferMemoryRequirements(device, vertex_buffer, &vertex_requirements);
    VkMemoryAllocateInfo vertex_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = vertex_requirements.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory vertex_memory;
    assert(vkAllocateMemory(device, &vertex_memory_info, NULL,
                            &vertex_memory) == VK_SUCCESS);
    assert(vkBindBufferMemory(device, vertex_buffer, vertex_memory, 0) == VK_SUCCESS);
    float *vertices;
    assert(vkMapMemory(device, vertex_memory, 0, VK_WHOLE_SIZE, 0,
                       (void **)&vertices) == VK_SUCCESS);
    const float vertex_data[8] = {
        9.0f, 9.0f,
        0.0f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,
    };
    memcpy(vertices, vertex_data, sizeof(vertex_data));
    vkUnmapMemory(device, vertex_memory);

    const VkBufferCreateInfo index_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 3u * sizeof(uint16_t),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer index_buffer;
    assert(vkCreateBuffer(device, &index_buffer_info, NULL,
                          &index_buffer) == VK_SUCCESS);
    VkMemoryRequirements index_requirements;
    vkGetBufferMemoryRequirements(device, index_buffer, &index_requirements);
    VkMemoryAllocateInfo index_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = index_requirements.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory index_memory;
    assert(vkAllocateMemory(device, &index_memory_info, NULL,
                            &index_memory) == VK_SUCCESS);
    assert(vkBindBufferMemory(device, index_buffer, index_memory, 0) == VK_SUCCESS);
    uint16_t *indices;
    assert(vkMapMemory(device, index_memory, 0, VK_WHOLE_SIZE, 0,
                       (void **)&indices) == VK_SUCCESS);
    const uint16_t index_data[3] = {1, 2, 3};
    memcpy(indices, index_data, sizeof(index_data));
    vkUnmapMemory(device, index_memory);
    const VkDescriptorSetLayoutBinding output_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &output_binding,
    };
    VkDescriptorSetLayout set_layout;
    assert(vkCreateDescriptorSetLayout(device, &set_layout_info, NULL,
                                       &set_layout) == VK_SUCCESS);
    VkShaderModule module = shader_module(device, argv[1]);
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
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
    const VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };
    const VkDescriptorPoolCreateInfo descriptor_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 3,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes,
    };
    VkDescriptorPool descriptor_pool;
    assert(vkCreateDescriptorPool(device, &descriptor_pool_info, NULL,
                                  &descriptor_pool) == VK_SUCCESS);
    const VkDescriptorSetAllocateInfo set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 2,
        .pSetLayouts = (VkDescriptorSetLayout[]){set_layout, set_layout},
    };
    VkDescriptorSet descriptor_sets[2];
    assert(vkAllocateDescriptorSets(device, &set_allocate_info,
                                    descriptor_sets) == VK_SUCCESS);
    const VkDescriptorBufferInfo output_info = {
        output_buffer, 0, VK_WHOLE_SIZE,
    };
    const VkWriteDescriptorSet descriptor_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_sets[0],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &output_info,
    };
    vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, NULL);
    const VkCopyDescriptorSet descriptor_copy = {
        .sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
        .srcSet = descriptor_sets[0],
        .srcBinding = 0,
        .dstSet = descriptor_sets[1],
        .dstBinding = 0,
        .descriptorCount = 1,
    };
    vkUpdateDescriptorSets(device, 0, NULL, 1, &descriptor_copy);

    const VkImageCreateInfo texture_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {2, 2, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkImage texture_image;
    assert(vkCreateImage(device, &texture_info, NULL,
                         &texture_image) == VK_SUCCESS);
    VkMemoryRequirements texture_requirements;
    vkGetImageMemoryRequirements(device, texture_image, &texture_requirements);
    const VkMemoryAllocateInfo texture_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = texture_requirements.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory texture_memory;
    assert(vkAllocateMemory(device, &texture_memory_info, NULL,
                            &texture_memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device, texture_image, texture_memory, 0) == VK_SUCCESS);
    uint32_t *texture_pixels;
    assert(vkMapMemory(device, texture_memory, 0, VK_WHOLE_SIZE, 0,
                       (void **)&texture_pixels) == VK_SUCCESS);
    const VkImageSubresource texture_subresource = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
    };
    VkSubresourceLayout texture_layout;
    vkGetImageSubresourceLayout(device, texture_image, &texture_subresource,
                                &texture_layout);
    texture_pixels[0] = 0xff0000ffu;
    texture_pixels[1] = 0xff00ff00u;
    uint32_t *texture_row1 = (uint32_t *)
        ((uint8_t *)texture_pixels + texture_layout.rowPitch);
    texture_row1[0] = 0xffff0000u;
    texture_row1[1] = 0xffffffffu;
    vkUnmapMemory(device, texture_memory);
    const VkImageViewCreateInfo texture_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView texture_view;
    assert(vkCreateImageView(device, &texture_view_info, NULL,
                             &texture_view) == VK_SUCCESS);
    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
    };
    VkSampler sampler;
    assert(vkCreateSampler(device, &sampler_info, NULL, &sampler) == VK_SUCCESS);
    const VkDescriptorSetLayoutBinding texture_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo texture_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &texture_binding,
    };
    VkDescriptorSetLayout texture_set_layout;
    assert(vkCreateDescriptorSetLayout(device, &texture_set_layout_info, NULL,
                                       &texture_set_layout) == VK_SUCCESS);
    const VkDescriptorSetAllocateInfo texture_set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &texture_set_layout,
    };
    VkDescriptorSet texture_set;
    assert(vkAllocateDescriptorSets(device, &texture_set_allocate_info,
                                    &texture_set) == VK_SUCCESS);
    const VkDescriptorImageInfo texture_descriptor = {
        .sampler = sampler,
        .imageView = texture_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkWriteDescriptorSet texture_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = texture_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &texture_descriptor,
    };
    vkUpdateDescriptorSets(device, 1, &texture_write, 0, NULL);

    VkShaderModule vertex = shader_module(device, argv[2]);
    VkShaderModule fragment = shader_module(device, argv[3]);
    const VkPipelineLayoutCreateInfo graphics_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &texture_set_layout,
    };
    VkPipelineLayout graphics_layout;
    assert(vkCreatePipelineLayout(device, &graphics_layout_info, NULL,
                                  &graphics_layout) == VK_SUCCESS);
    const VkAttachmentDescription attachments[] = {
        {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format = VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
        },
    };
    const VkAttachmentReference color = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference depth = {
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
        .pDepthStencilAttachment = &depth,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass render_pass;
    assert(vkCreateRenderPass(device, &render_pass_info, NULL,
                              &render_pass) == VK_SUCCESS);
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {256, 256, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkImage color_image;
    assert(vkCreateImage(device, &image_info, NULL, &color_image) == VK_SUCCESS);
    VkMemoryRequirements image_requirements;
    vkGetImageMemoryRequirements(device, color_image, &image_requirements);
    const VkMemoryAllocateInfo image_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory image_memory;
    assert(vkAllocateMemory(device, &image_memory_info, NULL,
                            &image_memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device, color_image, image_memory, 0) == VK_SUCCESS);
    const VkImageViewCreateInfo image_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = color_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
        },
    };
    VkImageView color_view;
    assert(vkCreateImageView(device, &image_view_info, NULL,
                             &color_view) == VK_SUCCESS);
    const VkImageCreateInfo depth_image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = {256, 256, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage depth_image;
    assert(vkCreateImage(device, &depth_image_info, NULL,
                         &depth_image) == VK_SUCCESS);
    VkMemoryRequirements depth_requirements;
    vkGetImageMemoryRequirements(device, depth_image, &depth_requirements);
    assert(depth_requirements.alignment == 65536u);
    assert(depth_requirements.memoryTypeBits == 0x2u);
    const VkMemoryAllocateInfo depth_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = depth_requirements.size,
        .memoryTypeIndex = 1,
    };
    VkDeviceMemory depth_memory;
    assert(vkAllocateMemory(device, &depth_memory_info, NULL,
                            &depth_memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device, depth_image, depth_memory, 0) == VK_SUCCESS);
    void *depth_data;
    assert(vkMapMemory(device, depth_memory, 0, VK_WHOLE_SIZE, 0,
                       &depth_data) == VK_SUCCESS);
    for (size_t i = 0; i < depth_requirements.size / sizeof(uint32_t); ++i)
        ((uint32_t *)depth_data)[i] = 0x3f800000u;
    vkUnmapMemory(device, depth_memory);
    const VkImageViewCreateInfo depth_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1,
        },
    };
    VkImageView depth_view;
    assert(vkCreateImageView(device, &depth_view_info, NULL,
                             &depth_view) == VK_SUCCESS);
    const VkImageView framebuffer_attachments[] = {color_view, depth_view};
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 2,
        .pAttachments = framebuffer_attachments,
        .width = 256,
        .height = 256,
        .layers = 1,
    };
    VkFramebuffer framebuffer;
    assert(vkCreateFramebuffer(device, &framebuffer_info, NULL,
                               &framebuffer) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo graphics_stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", NULL},
    };
    const VkVertexInputBindingDescription vertex_binding = {
        .binding = 0,
        .stride = 2u * sizeof(float),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription vertex_attribute = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &vertex_attribute,
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
    const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .front = {.compareOp = VK_COMPARE_OP_ALWAYS},
        .back = {.compareOp = VK_COMPARE_OP_ALWAYS},
    };
    const VkGraphicsPipelineCreateInfo graphics_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = graphics_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &blend,
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
    vkCmdDispatch(command, 1, 1, 1);
    assert(vkEndCommandBuffer(command) == VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vkEndCommandBuffer(command) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDispatch(command, 3, 5, 7);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 0, 1, &texture_set, 0, NULL);
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {256, 256}},
    };
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer, &vertex_offset);
    vkCmdBindIndexBuffer(command, index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(command, 3, 1, 0, 0, 0);
    vkCmdEndRenderPass(command);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);

    const uint32_t *dwords;
    uint32_t count = vk_ps5_command_buffer_dwords(command, &dwords);
    assert(count > 200);
    bool found_dispatch = false, found_draw = false, found_frame = false;
    bool found_color_target = false, found_depth_surface = false;
    bool found_depth_control = false;
    uint64_t image_address = vk_ps5_memory_gpu_address(image_memory, 0);
    for (uint32_t i = 0; i < count; ++i) {
        if (((dwords[i] >> 8) & 0xffu) == AGC_PM4_OP_DISPATCH_DIRECT) {
            assert(i + 4 < count);
            assert(dwords[i + 1] == 3 && dwords[i + 2] == 5 && dwords[i + 3] == 7);
            assert(i >= 3);
            assert(((dwords[i - 3] >> 8) & 0xffu) == AGC_PM4_OP_SET_SH_REG);
            assert(dwords[i - 1] != OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0));
            found_dispatch = true;
        } else if (((dwords[i] >> 8) & 0xffu) == AGC_PM4_OP_DRAW_INDEX_2) {
            assert(i + 5 < count && dwords[i + 4] == 3);
            uint64_t index_address = vk_ps5_memory_gpu_address(index_memory, 0);
            assert(dwords[i + 2] == ((uint32_t)index_address & ~1u));
            assert(dwords[i + 3] == (uint32_t)(index_address >> 32));
            found_draw = true;
        } else if (((dwords[i] >> 8) & 0xffu) == AGC_PM4_OP_CONTEXT_CONTROL) {
            found_frame = true;
        } else if (((dwords[i] >> 8) & 0xffu) ==
                       AGC_PM4_OP_SET_CONTEXT_REG &&
                   i + 2 < count && dwords[i + 1] == AGC_REG_CB_COLOR0_BASE) {
            found_color_target |=
                dwords[i + 2] == (uint32_t)(image_address >> 8);
        } else if (((dwords[i] >> 8) & 0xffu) ==
                       AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_DB_DEPTH_SIZE_XY) {
            found_depth_surface = true;
        } else if (((dwords[i] >> 8) & 0xffu) ==
                       AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_DB_DEPTH_CONTROL) {
            found_depth_control |= dwords[i + 2] == 0x00700716u;
        }
    }
    assert(found_dispatch && found_draw && found_frame && found_color_target &&
           found_depth_surface && found_depth_control);

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
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyImageView(device, depth_view, NULL);
    vkDestroyImage(device, depth_image, NULL);
    vkFreeMemory(device, depth_memory, NULL);
    vkDestroyImageView(device, color_view, NULL);
    vkDestroyImage(device, color_image, NULL);
    vkFreeMemory(device, image_memory, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    vkDestroyDescriptorSetLayout(device, texture_set_layout, NULL);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyImageView(device, texture_view, NULL);
    vkDestroyImage(device, texture_image, NULL);
    vkFreeMemory(device, texture_memory, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyShaderModule(device, vertex, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    vkDestroyPipelineLayout(device, layout, NULL);
    vkDestroyShaderModule(device, module, NULL);
    vkDestroyDescriptorSetLayout(device, set_layout, NULL);
    vkDestroyBuffer(device, output_buffer, NULL);
    vkFreeMemory(device, output_memory, NULL);
    vkDestroyBuffer(device, index_buffer, NULL);
    vkFreeMemory(device, index_memory, NULL);
    vkDestroyBuffer(device, vertex_buffer, NULL);
    vkFreeMemory(device, vertex_memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
