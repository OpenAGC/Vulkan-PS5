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

static bool has_register_value(const uint32_t *commands, uint32_t used,
                               uint32_t opcode, uint32_t offset,
                               uint32_t value)
{
    uint32_t cursor = 0;
    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || cursor + length > used)
            return false;
        if (agcPm4Opcode(commands[cursor]) == opcode && length >= 3u) {
            uint32_t base = commands[cursor + 1u];
            for (uint32_t i = 0; i < length - 2u; ++i)
                if (base + i == offset &&
                    commands[cursor + 2u + i] == value)
                    return true;
        }
        cursor += length;
    }
    return false;
}

static uint32_t count_register_value(const uint32_t *commands, uint32_t used,
                                     uint32_t opcode, uint32_t value)
{
    uint32_t cursor = 0;
    uint32_t count = 0;
    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || cursor + length > used)
            return 0u;
        if (agcPm4Opcode(commands[cursor]) == opcode)
            for (uint32_t i = 0; i < length - 2u; ++i)
                count += commands[cursor + 2u + i] == value;
        cursor += length;
    }
    return count;
}

int main(int argc, char **argv)
{
    assert(argc == 7);
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
    const VkPhysicalDeviceFeatures enabled_features = {
        .occlusionQueryPrecise = VK_TRUE,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .pEnabledFeatures = &enabled_features,
    };
    VkDevice device;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);

    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 256,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
    const VkBufferCreateInfo indirect_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 128u,
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer indirect_buffer;
    assert(vkCreateBuffer(device, &indirect_buffer_info, NULL,
                          &indirect_buffer) == VK_SUCCESS);
    VkMemoryRequirements indirect_requirements;
    vkGetBufferMemoryRequirements(device, indirect_buffer,
                                  &indirect_requirements);
    const VkMemoryAllocateInfo indirect_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = indirect_requirements.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory indirect_memory;
    assert(vkAllocateMemory(device, &indirect_memory_info, NULL,
                            &indirect_memory) == VK_SUCCESS);
    assert(vkBindBufferMemory(device, indirect_buffer, indirect_memory, 0) ==
           VK_SUCCESS);
    uint8_t *indirect_data;
    assert(vkMapMemory(device, indirect_memory, 0, VK_WHOLE_SIZE, 0,
                       (void **)&indirect_data) == VK_SUCCESS);
    const VkDrawIndirectCommand draws[2] = {
        {3u, 1u, 0u, 0u},
        {3u, 1u, 0u, 1u},
    };
    const VkDrawIndexedIndirectCommand indexed_draws[2] = {
        {3u, 1u, 0u, 0, 0u},
        {3u, 1u, 0u, 0, 1u},
    };
    memcpy(indirect_data, draws, sizeof(draws));
    memcpy(indirect_data + 64u, indexed_draws, sizeof(indexed_draws));
    vkUnmapMemory(device, indirect_memory);
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
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };
    const VkDescriptorPoolCreateInfo descriptor_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 4,
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
    VkSamplerCreateInfo anisotropic_sampler_info = sampler_info;
    anisotropic_sampler_info.anisotropyEnable = VK_TRUE;
    anisotropic_sampler_info.maxAnisotropy = 8.0f;
    VkSampler anisotropic_sampler;
    assert(vkCreateSampler(device, &anisotropic_sampler_info, NULL,
                           &anisotropic_sampler) == VK_SUCCESS);
    VkSamplerCreateInfo invalid_anisotropic_sampler_info =
        anisotropic_sampler_info;
    invalid_anisotropic_sampler_info.maxAnisotropy = 0.5f;
    VkSampler invalid_sampler = VK_NULL_HANDLE;
    assert(vkCreateSampler(device, &invalid_anisotropic_sampler_info, NULL,
                           &invalid_sampler) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_sampler == VK_NULL_HANDLE);
    invalid_anisotropic_sampler_info.maxAnisotropy = 17.0f;
    assert(vkCreateSampler(device, &invalid_anisotropic_sampler_info, NULL,
                           &invalid_sampler) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_sampler == VK_NULL_HANDLE);
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
    const VkDescriptorSetLayoutBinding hull_output_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo hull_output_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &hull_output_binding,
    };
    VkDescriptorSetLayout hull_output_set_layout;
    assert(vkCreateDescriptorSetLayout(device, &hull_output_set_layout_info,
        NULL, &hull_output_set_layout) == VK_SUCCESS);
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
    const VkDescriptorSetAllocateInfo hull_output_set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &hull_output_set_layout,
    };
    VkDescriptorSet hull_output_set;
    assert(vkAllocateDescriptorSets(device, &hull_output_set_allocate_info,
        &hull_output_set) == VK_SUCCESS);
    const VkWriteDescriptorSet hull_output_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = hull_output_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &output_info,
    };
    vkUpdateDescriptorSets(device, 1, &hull_output_write, 0, NULL);

    VkShaderModule vertex = shader_module(device, argv[2]);
    VkShaderModule fragment = shader_module(device, argv[3]);
    VkShaderModule geometry = shader_module(device, argv[4]);
    VkShaderModule tess_control = shader_module(device, argv[5]);
    VkShaderModule tess_evaluation = shader_module(device, argv[6]);
    const VkDescriptorSetLayout graphics_set_layouts[] = {
        texture_set_layout, hull_output_set_layout,
    };
    const VkPipelineLayoutCreateInfo graphics_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = graphics_set_layouts,
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
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
        },
    };
    const VkAttachmentReference colors[] = {
        {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };
    const VkAttachmentReference depth = {
        2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 2,
        .pColorAttachments = colors,
        .pDepthStencilAttachment = &depth,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 3,
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
    VkImage color_image_1;
    assert(vkCreateImage(device, &image_info, NULL,
                         &color_image_1) == VK_SUCCESS);
    VkMemoryRequirements image_requirements_1;
    vkGetImageMemoryRequirements(device, color_image_1, &image_requirements_1);
    const VkMemoryAllocateInfo image_memory_info_1 = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_requirements_1.size,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory image_memory_1;
    assert(vkAllocateMemory(device, &image_memory_info_1, NULL,
                            &image_memory_1) == VK_SUCCESS);
    assert(vkBindImageMemory(device, color_image_1, image_memory_1, 0) ==
           VK_SUCCESS);
    VkImageViewCreateInfo image_view_info_1 = image_view_info;
    image_view_info_1.image = color_image_1;
    VkImageView color_view_1;
    assert(vkCreateImageView(device, &image_view_info_1, NULL,
                             &color_view_1) == VK_SUCCESS);
    const VkImageCreateInfo depth_image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
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
        .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
        .subresourceRange = {
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            0, 1, 0, 1,
        },
    };
    VkImageView depth_view;
    assert(vkCreateImageView(device, &depth_view_info, NULL,
                             &depth_view) == VK_SUCCESS);
    const VkImageView framebuffer_attachments[] = {
        color_view, color_view_1, depth_view,
    };
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 3,
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
        .depthBiasEnable = VK_TRUE,
        .lineWidth = 1,
    };
    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_DEPTH_BIAS,
    };
    const VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1u,
        .pDynamicStates = dynamic_states,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blend_attachments[] = {
        {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT},
        {
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT,
        },
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments = blend_attachments,
        .blendConstants = {0.25f, 0.5f, 0.75f, 1.0f},
    };
    const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
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
        .pDynamicState = &dynamic_state,
        .layout = graphics_layout,
        .renderPass = render_pass,
    };
    VkPipeline graphics_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &graphics_info, NULL,
                                     &graphics_pipeline) == VK_SUCCESS);
    VkPipelineRasterizationStateCreateInfo static_bias_raster = rasterization;
    static_bias_raster.depthClampEnable = VK_TRUE;
    static_bias_raster.depthBiasConstantFactor = 2.0f;
    static_bias_raster.depthBiasClamp = 0.25f;
    static_bias_raster.depthBiasSlopeFactor = -1.5f;
    VkGraphicsPipelineCreateInfo static_bias_info = graphics_info;
    static_bias_info.pRasterizationState = &static_bias_raster;
    static_bias_info.pDynamicState = NULL;
    VkPipeline static_bias_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &static_bias_info, NULL,
                                     &static_bias_pipeline) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo geometry_stages[] = {
        graphics_stages[0],
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_GEOMETRY_BIT, geometry, "main", NULL},
        graphics_stages[1],
    };
    VkGraphicsPipelineCreateInfo geometry_info = graphics_info;
    geometry_info.stageCount = 3;
    geometry_info.pStages = geometry_stages;
    VkPipeline geometry_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &geometry_info, NULL,
                                     &geometry_pipeline) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo tessellation_stages[] = {
        graphics_stages[0],
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tess_control, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, tess_evaluation,
         "main", NULL},
        graphics_stages[1],
    };
    const VkPipelineInputAssemblyStateCreateInfo patch_input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
    };
    const VkPipelineTessellationStateCreateInfo tessellation_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .patchControlPoints = 3,
    };
    VkGraphicsPipelineCreateInfo tessellation_info = graphics_info;
    tessellation_info.stageCount = 4;
    tessellation_info.pStages = tessellation_stages;
    tessellation_info.pInputAssemblyState = &patch_input_assembly;
    tessellation_info.pTessellationState = &tessellation_state;
    VkPipeline tessellation_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &tessellation_info, NULL,
                                     &tessellation_pipeline) == VK_SUCCESS);

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
    const VkQueryPoolCreateInfo query_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = 2,
    };
    VkQueryPool query_pool;
    assert(vkCreateQueryPool(device, &query_info, NULL, &query_pool) == VK_SUCCESS);
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
    const VkBufferCopy buffer_copies[2] = {
        {0u, 0u, 16u},
        {32u, 32u, 16u},
    };
    vkCmdCopyBuffer(command, indirect_buffer, output_buffer, 2u,
                    buffer_copies);
    vkCmdResetQueryPool(command, query_pool, 0, 2);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDispatch(command, 3, 5, 7);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      geometry_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 0, 1, &texture_set, 0, NULL);
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {256, 256}},
    };
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    vkCmdBeginQuery(command, query_pool, 1, VK_QUERY_CONTROL_PRECISE_BIT);
    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer, &vertex_offset);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      tessellation_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 1, 1, &hull_output_set, 0, NULL);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      geometry_pipeline);
    vkCmdBindIndexBuffer(command, index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(command, 3, 1, 0, 0, 0);
    vkCmdDrawIndirect(command, indirect_buffer, 0u, 1u,
                      sizeof(VkDrawIndirectCommand));
    vkCmdDrawIndirect(command, indirect_buffer, 0u, 2u,
                      sizeof(VkDrawIndirectCommand));
    vkCmdDrawIndexedIndirect(command, indirect_buffer, 64u, 2u,
                             sizeof(VkDrawIndexedIndirectCommand));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      static_bias_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndQuery(command, query_pool, 1);
    vkCmdEndRenderPass(command);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);

    VkCommandBuffer invalid_indirect_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &invalid_indirect_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_indirect_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdBindPipeline(invalid_indirect_command,
                      VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pipeline);
    vkCmdBindDescriptorSets(invalid_indirect_command,
        VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout, 0, 1,
        &texture_set, 0, NULL);
    vkCmdBeginRenderPass(invalid_indirect_command, &render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindVertexBuffers(invalid_indirect_command, 0, 1, &vertex_buffer,
                           &vertex_offset);
    vkCmdDrawIndirect(invalid_indirect_command, output_buffer, 0u, 0u, 0u);
    vkCmdDrawIndirect(invalid_indirect_command, indirect_buffer, 124u, 1u,
                      sizeof(VkDrawIndirectCommand));
    vkCmdEndRenderPass(invalid_indirect_command);
    assert(vkEndCommandBuffer(invalid_indirect_command) ==
           VK_ERROR_INITIALIZATION_FAILED);

    VkCommandBuffer invalid_copy_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &invalid_copy_command) == VK_SUCCESS);
    const VkBufferCopy overlapping_copy = {0u, 4u, 8u};
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdCopyBuffer(invalid_copy_command, output_buffer, output_buffer, 1u,
                    &overlapping_copy);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    const VkBufferCopy unaligned_copy = {0u, 2u, 4u};
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdCopyBuffer(invalid_copy_command, indirect_buffer, output_buffer, 1u,
                    &unaligned_copy);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    const VkBufferCopy out_of_bounds_copy = {120u, 0u, 16u};
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdCopyBuffer(invalid_copy_command, indirect_buffer, output_buffer, 1u,
                    &out_of_bounds_copy);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    const VkBufferCopy usage_copy = {0u, 0u, 16u};
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdCopyBuffer(invalid_copy_command, vertex_buffer, output_buffer, 1u,
                    &usage_copy);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);

    const uint32_t *dwords;
    uint32_t count = vk_ps5_command_buffer_dwords(command, &dwords);
    assert(count > 200);
    uint32_t indirect_descriptor_table =
        vk_ps5_command_buffer_indirect_descriptor_table(command);
    assert(indirect_descriptor_table != 0u);
    assert(vk_ps5_command_buffer_indirect_descriptor_entry(command, 1u) !=
           0u);
    uint32_t indirect_descriptor_register =
        vk_ps5_command_buffer_indirect_descriptor_register(command);
    assert(indirect_descriptor_register != 0u);
    assert(has_register_value(dwords, count, AGC_PM4_OP_SET_SH_REG,
                              indirect_descriptor_register,
                              indirect_descriptor_table));
    assert(has_register_value(dwords, count, AGC_PM4_OP_SET_SH_REG,
                              AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 11u,
                              0x210a2108u));
    assert(count_register_value(dwords, count, AGC_PM4_OP_SET_SH_REG,
                                0x210a2108u) >= 2u);
    assert(count_register_value(dwords, count, AGC_PM4_OP_SET_SH_REG,
                                OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(1)) == 0u);
    bool found_dispatch = false, found_draw = false, found_frame = false;
    uint32_t indirect_count = 0u, indexed_indirect_count = 0u;
    uint32_t dma_copy_count = 0u;
    bool found_tess_draw = false, found_tess_context = false;
    bool found_tess_ring_size = false, found_tess_offchip = false;
    bool found_tess_ring_base = false, found_tess_ring_base_hi = false;
    bool found_tess_hull_lds = false;
    bool found_color_target = false, found_color_target_1 = false;
    bool found_dual_export = false, found_depth_surface = false;
    bool found_independent_blend = false, found_blend_mask = false;
    bool found_blend_constants = false;
    bool found_depth_control = false, found_stencil_control = false;
    bool found_depth_bias_format = false, found_depth_bias_values = false;
    bool found_depth_bias_enable = false;
    bool found_vulkan_clip_control = false, found_depth_clamp = false;
    uint32_t occlusion_snapshots = 0;
    bool found_query_reset = false, found_query_availability = false;
    uint32_t last_single_sh_register = UINT32_MAX;
    uint32_t last_single_sh_value = UINT32_MAX;
    uint32_t indirect_base_vertex_register = UINT32_MAX;
    uint32_t indirect_start_instance_register = UINT32_MAX;
    uint32_t indirect_draw_index_register = UINT32_MAX;
    uint64_t image_address = vk_ps5_memory_gpu_address(image_memory, 0);
    for (uint32_t i = 0; i < count;) {
        uint32_t packet_length = agcPm4Length(dwords[i]);
        assert(packet_length != 0u && i + packet_length <= count);
        uint32_t opcode = agcPm4Opcode(dwords[i]);
        if (opcode == AGC_PM4_OP_DISPATCH_DIRECT) {
            assert(i + 4 < count);
            assert(dwords[i + 1] == 3 && dwords[i + 2] == 5 && dwords[i + 3] == 7);
            assert(i >= 3);
            assert(((dwords[i - 3] >> 8) & 0xffu) == AGC_PM4_OP_SET_SH_REG);
            assert(dwords[i - 1] != OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0));
            found_dispatch = true;
        } else if (opcode == AGC_PM4_OP_DMA_DATA) {
            assert(i + 7u < count && dma_copy_count < 2u);
            uint64_t source_address = vk_ps5_memory_gpu_address(
                indirect_memory, (VkDeviceSize)dma_copy_count * 32u);
            uint64_t destination_address = vk_ps5_memory_gpu_address(
                output_memory, (VkDeviceSize)dma_copy_count * 32u);
            assert(dwords[i + 2u] == 16u);
            assert(dwords[i + 3u] == (uint32_t)destination_address);
            assert(dwords[i + 4u] == (uint32_t)(destination_address >> 32u));
            assert(dwords[i + 5u] == (uint32_t)source_address);
            assert(dwords[i + 6u] == (uint32_t)(source_address >> 32u));
            dma_copy_count++;
        } else if (opcode == AGC_PM4_OP_DRAW_INDEX_2) {
            assert(i + 5 < count && dwords[i + 4] == 3);
            uint64_t index_address = vk_ps5_memory_gpu_address(index_memory, 0);
            assert(dwords[i + 2] == ((uint32_t)index_address & ~1u));
            assert(dwords[i + 3] == (uint32_t)(index_address >> 32));
            found_draw = true;
        } else if (opcode == AGC_PM4_OP_DRAW_INDIRECT) {
            assert(i + 4 < count);
            assert(dwords[i + 2] != 0u && dwords[i + 3] != 0u);
            assert(dwords[i + 4] == 2u);
            if (indirect_base_vertex_register == UINT32_MAX) {
                indirect_base_vertex_register = dwords[i + 2];
                indirect_start_instance_register = dwords[i + 3];
                indirect_draw_index_register = last_single_sh_register;
            }
            assert(dwords[i + 2] == indirect_base_vertex_register);
            assert(dwords[i + 3] == indirect_start_instance_register);
            assert(last_single_sh_register == indirect_draw_index_register);
            assert(last_single_sh_value == (indirect_count == 2u ? 1u : 0u));
            indirect_count++;
        } else if (opcode == AGC_PM4_OP_DRAW_INDEX_INDIRECT) {
            assert(i + 4 < count);
            assert(dwords[i + 2] != 0u && dwords[i + 3] != 0u);
            assert(dwords[i + 4] == 0u);
            assert(dwords[i + 2] == indirect_base_vertex_register);
            assert(dwords[i + 3] == indirect_start_instance_register);
            assert(last_single_sh_register == indirect_draw_index_register);
            assert(last_single_sh_value == indexed_indirect_count);
            indexed_indirect_count++;
        } else if (opcode == AGC_PM4_OP_DRAW_INDIRECT_MULTI) {
            assert(!"DrawID pipeline must expand non-indexed multi draws");
        } else if (opcode == AGC_PM4_OP_DRAW_INDEX_INDIRECT_MULTI) {
            assert(!"DrawID pipeline must expand indexed multi draws");
        } else if (opcode == AGC_PM4_OP_DRAW_INDEX_AUTO) {
            found_tess_draw |= i + 2 < count && dwords[i + 1] == 3u;
        } else if (opcode == AGC_PM4_OP_CONTEXT_CONTROL) {
            found_frame = true;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG &&
                   i + 2 < count && dwords[i + 1] == AGC_REG_CB_COLOR0_BASE) {
            found_color_target |=
                dwords[i + 2] == (uint32_t)(image_address >> 8);
        } else if (opcode == AGC_PM4_OP_SET_SH_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_SPI_SHADER_PGM_RSRC2_HS) {
            const uint32_t encoded_lds =
                (dwords[i + 2] >> 18u) & 0x1ffu;
            found_tess_hull_lds |= encoded_lds != 0u &&
                (encoded_lds & 1u) == 0u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 9 < count &&
                   dwords[i + 1] == AGC_REG_CB_BLEND0_CONTROL) {
            found_independent_blend |= packet_length == 10u &&
                dwords[i + 2] == 0x00010001u &&
                dwords[i + 3] == 0x60010504u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_CB_TARGET_MASK) {
            found_blend_mask |= dwords[i + 2] == 0x6fu;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 5 < count &&
                   dwords[i + 1] == AGC_REG_CB_BLEND_RED) {
            found_blend_constants |= packet_length == 6u &&
                dwords[i + 2] == 0x3e800000u &&
                dwords[i + 3] == 0x3f000000u &&
                dwords[i + 4] == 0x3f400000u &&
                dwords[i + 5] == 0x3f800000u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] ==
                       AGC_REG_PA_SU_POLY_OFFSET_DB_FMT_CNTL) {
            found_depth_bias_format |= packet_length == 3u &&
                dwords[i + 2] == 0x000001e9u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 6 < count &&
                   dwords[i + 1] == AGC_REG_PA_SU_POLY_OFFSET_CLAMP) {
            found_depth_bias_values |= packet_length == 7u &&
                dwords[i + 2] == 0x3e800000u &&
                dwords[i + 3] == 0xc1c00000u &&
                dwords[i + 4] == 0x40000000u &&
                dwords[i + 5] == 0xc1c00000u &&
                dwords[i + 6] == 0x40000000u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_PA_SU_SC_MODE_CNTL) {
            found_depth_bias_enable |=
                (dwords[i + 2] & AGC_GFX1013_DEPTH_BIAS_RASTER_MODE) ==
                    AGC_GFX1013_DEPTH_BIAS_RASTER_MODE;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_PA_CL_CLIP_CNTL) {
            found_vulkan_clip_control |=
                dwords[i + 2] == AGC_GFX1013_VULKAN_CLIP_CONTROL;
            found_depth_clamp |=
                dwords[i + 2] == AGC_GFX1013_DEPTH_CLAMP_CLIP_CONTROL;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_CB_COLOR0_BASE + 15u) {
            uint64_t image_address_1 =
                vk_ps5_memory_gpu_address(image_memory_1, 0);
            found_color_target_1 |=
                dwords[i + 2] == (uint32_t)(image_address_1 >> 8);
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_SPI_SHADER_COL_FORMAT) {
            found_dual_export |= dwords[i + 2] == 0x44u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_DB_DEPTH_SIZE_XY) {
            found_depth_surface = true;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_DB_DEPTH_CONTROL) {
            found_depth_control |= dwords[i + 2] == 0x00700797u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_DB_STENCIL_CONTROL) {
            found_stencil_control |= dwords[i + 2] == 0x00030030u;
        } else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_VGT_TF_PARAM) {
            found_tess_context |= dwords[i + 2] == 0x61u;
        } else if (opcode == AGC_PM4_OP_SET_UCONFIG_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_VGT_TF_RING_SIZE) {
            found_tess_ring_size |= dwords[i + 2] ==
                AGC_GFX1013_TESS_FACTOR_RING_SIZE / 4u;
        } else if (opcode == AGC_PM4_OP_SET_UCONFIG_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_VGT_HS_OFFCHIP_PARAM) {
            found_tess_offchip |= dwords[i + 2] ==
                AGC_GFX1013_TESS_OFFCHIP_PARAM;
        } else if (opcode == AGC_PM4_OP_SET_UCONFIG_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_VGT_TF_MEMORY_BASE) {
            found_tess_ring_base = true;
        } else if (opcode == AGC_PM4_OP_SET_UCONFIG_REG && i + 2 < count &&
                   dwords[i + 1] == AGC_REG_VGT_TF_MEMORY_BASE_HI) {
            found_tess_ring_base_hi = true;
        } else if (opcode == AGC_PM4_OP_EVENT_WRITE &&
                   i + 3 < count && dwords[i + 1] == 0x115u) {
            ++occlusion_snapshots;
        } else if (opcode == AGC_PM4_OP_WRITE_DATA) {
            assert(i + 3 < count && dwords[i + 1] == 0x00100100u);
            found_query_reset = true;
        } else if (opcode == AGC_PM4_OP_RELEASE_MEM &&
                   i + 7 < count && dwords[i + 5] == 1u) {
            found_query_availability = true;
        }
        if (opcode == AGC_PM4_OP_SET_SH_REG && packet_length == 3u) {
            last_single_sh_register = dwords[i + 1u];
            last_single_sh_value = dwords[i + 2u];
        }
        i += packet_length;
    }
    assert(found_dispatch && found_draw && dma_copy_count == 2u &&
           indirect_count == 3u &&
           indexed_indirect_count == 2u && found_tess_draw &&
           found_tess_context && found_tess_ring_size &&
           found_tess_offchip && found_tess_ring_base &&
           found_tess_ring_base_hi && found_tess_hull_lds &&
           found_frame && found_color_target &&
           found_color_target_1 && found_dual_export &&
           found_independent_blend && found_blend_mask &&
           found_blend_constants &&
           found_depth_bias_format && found_depth_bias_values &&
           found_depth_bias_enable &&
           found_vulkan_clip_control && found_depth_clamp &&
           found_depth_surface && found_depth_control && found_stencil_control &&
           found_query_reset && occlusion_snapshots == 2 &&
           found_query_availability);

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

    uint64_t query_results[] = {UINT64_MAX, UINT64_MAX};
    assert(vkGetQueryPoolResults(device, query_pool, 1, 1,
        sizeof(query_results), query_results, sizeof(query_results),
        VK_QUERY_RESULT_64_BIT |
        VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_NOT_READY);
    assert(query_results[0] == UINT64_MAX && query_results[1] == 0u);
    query_results[0] = UINT64_MAX;
    assert(vkGetQueryPoolResults(device, query_pool, 1, 1,
        sizeof(query_results[0]), query_results, sizeof(query_results[0]),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_PARTIAL_BIT) == VK_SUCCESS);
    assert(query_results[0] == 0u);

    vkDestroyFence(device, fence, NULL);
    vkDestroyQueryPool(device, query_pool, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyPipeline(device, tessellation_pipeline, NULL);
    vkDestroyPipeline(device, geometry_pipeline, NULL);
    vkDestroyPipeline(device, static_bias_pipeline, NULL);
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyImageView(device, depth_view, NULL);
    vkDestroyImage(device, depth_image, NULL);
    vkFreeMemory(device, depth_memory, NULL);
    vkDestroyImageView(device, color_view_1, NULL);
    vkDestroyImage(device, color_image_1, NULL);
    vkFreeMemory(device, image_memory_1, NULL);
    vkDestroyImageView(device, color_view, NULL);
    vkDestroyImage(device, color_image, NULL);
    vkFreeMemory(device, image_memory, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    vkDestroyDescriptorSetLayout(device, hull_output_set_layout, NULL);
    vkDestroyDescriptorSetLayout(device, texture_set_layout, NULL);
    vkDestroySampler(device, anisotropic_sampler, NULL);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyImageView(device, texture_view, NULL);
    vkDestroyImage(device, texture_image, NULL);
    vkFreeMemory(device, texture_memory, NULL);
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyShaderModule(device, geometry, NULL);
    vkDestroyShaderModule(device, tess_evaluation, NULL);
    vkDestroyShaderModule(device, tess_control, NULL);
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
    vkDestroyBuffer(device, indirect_buffer, NULL);
    vkFreeMemory(device, indirect_memory, NULL);
    vkDestroyBuffer(device, vertex_buffer, NULL);
    vkFreeMemory(device, vertex_memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
