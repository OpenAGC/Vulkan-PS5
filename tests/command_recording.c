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

#if 0 /* Removed with the remaining legacy command-stream encoder. */
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
#endif

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
        .multiViewport = VK_TRUE,
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
    const VkPushConstantRange compute_push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0u,
        .size = sizeof(uint32_t),
    };
    const uint32_t push_addend = 7u;
    const VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1u,
        .pPushConstantRanges = &compute_push_range,
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
    const VkDescriptorUpdateTemplateEntry descriptor_template_entry = {
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .offset = 0,
        .stride = sizeof(output_info),
    };
    const VkDescriptorUpdateTemplateCreateInfo descriptor_template_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = 1,
        .pDescriptorUpdateEntries = &descriptor_template_entry,
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        .descriptorSetLayout = set_layout,
    };
    VkDescriptorUpdateTemplate descriptor_template = VK_NULL_HANDLE;
    assert(vkCreateDescriptorUpdateTemplate(device, &descriptor_template_info,
        NULL, &descriptor_template) == VK_SUCCESS);
    vkUpdateDescriptorSetWithTemplate(device, descriptor_sets[0],
                                      descriptor_template, &output_info);
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
    const VkSamplerCustomBorderColorCreateInfoEXT custom_border_info = {
        .sType =
            VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
        .customBorderColor = {.float32 = {0.25f, 0.5f, 0.75f, 1.0f}},
        .format = VK_FORMAT_UNDEFINED,
    };
    VkSamplerCreateInfo custom_sampler_info = sampler_info;
    custom_sampler_info.pNext = &custom_border_info;
    custom_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    custom_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    custom_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    custom_sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
    VkSampler custom_sampler;
    assert(vkCreateSampler(device, &custom_sampler_info, NULL,
                           &custom_sampler) == VK_SUCCESS);
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
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
    assert(vk_ps5_pipeline_has_native_shaders(graphics_pipeline));
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(graphics_pipeline));
    const VkFormat dynamic_color_formats[] = {
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    };
    const VkPipelineRenderingCreateInfo pipeline_rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 2,
        .pColorAttachmentFormats = dynamic_color_formats,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT,
        .stencilAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT,
    };
    VkGraphicsPipelineCreateInfo dynamic_rendering_pipeline_info =
        graphics_info;
    dynamic_rendering_pipeline_info.pNext = &pipeline_rendering;
    dynamic_rendering_pipeline_info.renderPass = VK_NULL_HANDLE;
    dynamic_rendering_pipeline_info.subpass = 0;
    VkPipeline dynamic_rendering_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &dynamic_rendering_pipeline_info, NULL,
        &dynamic_rendering_pipeline) == VK_SUCCESS);
    const VkPipelineViewportStateCreateInfo dynamic_viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 2,
        .scissorCount = 2,
    };
    const VkDynamicState dynamic_viewport_states[] = {
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    const VkPipelineDynamicStateCreateInfo dynamic_viewport_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 3,
        .pDynamicStates = dynamic_viewport_states,
    };
    VkGraphicsPipelineCreateInfo dynamic_viewport_pipeline_info = graphics_info;
    dynamic_viewport_pipeline_info.pViewportState = &dynamic_viewport_state;
    dynamic_viewport_pipeline_info.pDynamicState = &dynamic_viewport_info;
    VkPipeline dynamic_viewport_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &dynamic_viewport_pipeline_info, NULL, &dynamic_viewport_pipeline) ==
        VK_SUCCESS);
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
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        static_bias_pipeline));
    VkPipelineRasterizationStateCreateInfo non_solid_raster = rasterization;
    non_solid_raster.depthBiasEnable = VK_FALSE;
    non_solid_raster.polygonMode = VK_POLYGON_MODE_LINE;
    VkGraphicsPipelineCreateInfo non_solid_info = graphics_info;
    non_solid_info.pRasterizationState = &non_solid_raster;
    non_solid_info.pDynamicState = NULL;
    VkPipeline line_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &non_solid_info, NULL,
                                     &line_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(line_pipeline));
    non_solid_raster.polygonMode = VK_POLYGON_MODE_POINT;
    VkPipeline point_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &non_solid_info, NULL,
                                     &point_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(point_pipeline));
    VkPipelineInputAssemblyStateCreateInfo point_input_assembly = input_assembly;
    point_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    VkGraphicsPipelineCreateInfo point_list_info = graphics_info;
    point_list_info.pInputAssemblyState = &point_input_assembly;
    point_list_info.pRasterizationState = &non_solid_raster;
    point_list_info.pDynamicState = NULL;
    VkPipeline point_list_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &point_list_info, NULL,
                                     &point_list_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        point_list_pipeline));
    VkPipelineInputAssemblyStateCreateInfo line_input_assembly = input_assembly;
    line_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    VkPipelineRasterizationStateCreateInfo wide_line_raster = rasterization;
    wide_line_raster.depthBiasEnable = VK_FALSE;
    wide_line_raster.lineWidth = 8.0f;
    VkGraphicsPipelineCreateInfo wide_line_info = graphics_info;
    wide_line_info.pInputAssemblyState = &line_input_assembly;
    wide_line_info.pRasterizationState = &wide_line_raster;
    wide_line_info.pDynamicState = NULL;
    VkPipeline wide_line_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &wide_line_info, NULL,
                                     &wide_line_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        wide_line_pipeline));
    const VkDynamicState line_width_dynamic_state =
        VK_DYNAMIC_STATE_LINE_WIDTH;
    const VkPipelineDynamicStateCreateInfo line_width_dynamic_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1,
        .pDynamicStates = &line_width_dynamic_state,
    };
    wide_line_raster.lineWidth = 0.0f;
    VkGraphicsPipelineCreateInfo dynamic_line_info = wide_line_info;
    dynamic_line_info.pDynamicState = &line_width_dynamic_info;
    VkPipeline dynamic_line_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &dynamic_line_info, NULL,
                                     &dynamic_line_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        dynamic_line_pipeline));
    VkPipelineColorBlendStateCreateInfo logic_blend = blend;
    logic_blend.logicOpEnable = VK_TRUE;
    logic_blend.logicOp = VK_LOGIC_OP_XOR;
    VkGraphicsPipelineCreateInfo logic_info = graphics_info;
    logic_info.pColorBlendState = &logic_blend;
    VkPipeline logic_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                     &logic_info, NULL,
                                     &logic_pipeline) == VK_SUCCESS);
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(logic_pipeline));
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
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        geometry_pipeline));
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
    assert(vk_ps5_pipeline_has_native_graphics_pipeline(
        tessellation_pipeline));

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
    assert(vk_ps5_command_buffer_has_native(command));
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
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
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_RECORDING);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdDispatch(command, 1, 1, 1);
    assert(vkEndCommandBuffer(command) == VK_ERROR_INITIALIZATION_FAILED);
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdSetLineWidth(command, 0.5f);
    assert(vkEndCommandBuffer(command) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vkEndCommandBuffer(command) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    const VkBufferCopy buffer_copies[2] = {
        {0u, 0u, 16u},
        {32u, 32u, 16u},
    };
    const VkBufferMemoryBarrier copy_barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = indirect_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = output_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         2u, copy_barriers, 0u, NULL);
    vkCmdCopyBuffer(command, indirect_buffer, output_buffer, 2u,
                    buffer_copies);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    const VkImageCopy image_copy = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .extent = {256u, 256u, 1u},
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdCopyImage(command, color_image, VK_IMAGE_LAYOUT_GENERAL,
                   color_image_1, VK_IMAGE_LAYOUT_GENERAL, 1u, &image_copy);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    const VkBufferImageCopy buffer_image_copy = {
        .bufferOffset = 0u,
        .bufferRowLength = 8u,
        .bufferImageHeight = 4u,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .imageOffset = {2, 2, 0},
        .imageExtent = {4u, 3u, 1u},
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdCopyBufferToImage(command, indirect_buffer, color_image,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &buffer_image_copy);
    vkCmdCopyImageToBuffer(command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, output_buffer, 1u, &buffer_image_copy);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    const uint32_t update_data[4] = {
        0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdUpdateBuffer(command, output_buffer, 0u, sizeof(update_data),
                      update_data);
    vkCmdFillBuffer(command, output_buffer, 32u, 16u, 0xa5a5a5a5u);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    const VkBufferMemoryBarrier compute_barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = output_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = indirect_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                         0u, NULL, 2u, compute_barriers, 0u, NULL);
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0u, sizeof(push_addend), &push_addend);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDispatch(command, 3, 5, 7);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_dispatch_count(command) == 1u);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0u, 0u, NULL, 2u, compute_barriers, 0u, NULL);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0u, sizeof(push_addend), &push_addend);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDispatchIndirect(command, indirect_buffer, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_dispatch_count(command) == 1u);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    const VkBufferMemoryBarrier native_graphics_barriers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vertex_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = index_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = indirect_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                             VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = output_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
    };
    const VkImageMemoryBarrier native_texture_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture_image,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        },
    };
    const VkRenderPassBeginInfo native_render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {256, 256}},
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u, 0u, NULL, 4u, native_graphics_barriers,
                         1u, &native_texture_barrier);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 0u, 1u, &texture_set,
                            0u, NULL);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBeginRenderPass(command, &native_render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    const VkDeviceSize native_vertex_offset = 0u;
    vkCmdBindVertexBuffers(command, 0u, 1u, &vertex_buffer,
                           &native_vertex_offset);
    vkCmdDraw(command, 3u, 1u, 0u, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindIndexBuffer(command, index_buffer, 0u, VK_INDEX_TYPE_UINT16);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(command) == 2u);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0u, 0u, NULL, 4u, native_graphics_barriers,
                         1u, &native_texture_barrier);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 0u, 1u, &texture_set,
                            0u, NULL);
    vkCmdBeginRenderPass(command, &native_render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    vkCmdBindVertexBuffers(command, 0u, 1u, &vertex_buffer,
                           &native_vertex_offset);
    vkCmdDrawIndirect(command, indirect_buffer, 0u, 2u,
                      sizeof(VkDrawIndirectCommand));
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindIndexBuffer(command, index_buffer, 0u, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(command, indirect_buffer, 64u, 2u,
                             sizeof(VkDrawIndexedIndirectCommand));
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(command) == 4u);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    /* Query reset/begin/end must remain wholly native: once a query command
     * is present, falling back to the legacy stream would silently omit it. */
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u, 0u, NULL, 4u, native_graphics_barriers,
                         1u, &native_texture_barrier);
    vkCmdResetQueryPool(command, query_pool, 0u, 2u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 0u, 1u, &texture_set,
                            0u, NULL);
    vkCmdBeginRenderPass(command, &native_render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    vkCmdBindVertexBuffers(command, 0u, 1u, &vertex_buffer,
                           &native_vertex_offset);
    vkCmdBeginQuery(command, query_pool, 1u,
                    VK_QUERY_CONTROL_PRECISE_BIT);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdDraw(command, 3u, 1u, 0u, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndQuery(command, query_pool, 1u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    /* Native shader execution requires typed descriptor resource state and
     * fails closed; there is no shader-code legacy fallback. */
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdCopyBuffer(command, indirect_buffer, output_buffer, 2u,
                    buffer_copies);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDispatch(command, 3, 5, 7);
    assert(vk_ps5_command_buffer_record_error(command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkEndCommandBuffer(command) == VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    /* The same missing-state failure is stable before a later native copy. */
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDispatch(command, 3, 5, 7);
    assert(vk_ps5_command_buffer_record_error(command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    vkCmdCopyBuffer(command, indirect_buffer, output_buffer, 2u,
                    buffer_copies);
    assert(vk_ps5_command_buffer_record_error(command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkEndCommandBuffer(command) == VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u, 0u, NULL, 4u, native_graphics_barriers,
                         1u, &native_texture_barrier);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
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
const VkSubpassBeginInfo subpass_begin = {
.sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
.contents = VK_SUBPASS_CONTENTS_INLINE,
};
const VkSubpassEndInfo subpass_end = {
.sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
};
vkCmdBeginRenderPass2(command, &render_begin, &subpass_begin);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    const VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer, &vertex_offset);
    const VkViewport dynamic_viewports[] = {
        {0, 0, 128, 256, 0, 1},
        {128, 0, 128, 256, 0.25f, 0.75f},
    };
    const VkRect2D dynamic_scissors[] = {
        {{0, 0}, {128, 256}},
        {{128, 0}, {128, 256}},
    };
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      dynamic_viewport_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 1, 1, &hull_output_set, 0, NULL);
    vkCmdSetViewport(command, 0, 2, dynamic_viewports);
    vkCmdSetScissor(command, 0, 2, dynamic_scissors);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      tessellation_pipeline);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 1, 1, &hull_output_set, 0, NULL);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      geometry_pipeline);
    vkCmdBindIndexBuffer(command, index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(command, 3, 1, 0, 0, 0);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      static_bias_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      logic_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      line_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      point_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      point_list_pipeline);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      wide_line_pipeline);
    vkCmdDraw(command, 2, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      dynamic_line_pipeline);
    vkCmdSetLineWidth(command, 16.0f);
    vkCmdDraw(command, 2, 1, 0, 0);
    vkCmdSetLineWidth(command, 32.0f);
    vkCmdDraw(command, 2, 1, 0, 0);
vkCmdEndRenderPass2(command, &subpass_end);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_EXECUTABLE);

    VkCommandBuffer dynamic_rendering_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &dynamic_rendering_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(dynamic_rendering_command, &begin_info) ==
        VK_SUCCESS);
    const VkRenderingAttachmentInfo dynamic_colors[] = {
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = color_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = color_view_1,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        },
    };
    const VkRenderingAttachmentInfo dynamic_depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo dynamic_rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {256, 256}},
        .layerCount = 1,
        .colorAttachmentCount = 2,
        .pColorAttachments = dynamic_colors,
        .pDepthAttachment = &dynamic_depth_stencil,
        .pStencilAttachment = &dynamic_depth_stencil,
    };
    vkCmdBeginRendering(dynamic_rendering_command, &dynamic_rendering);
    vkCmdBindPipeline(dynamic_rendering_command,
        VK_PIPELINE_BIND_POINT_GRAPHICS, dynamic_rendering_pipeline);
    vkCmdEndRendering(dynamic_rendering_command);
    assert(vkEndCommandBuffer(dynamic_rendering_command) == VK_SUCCESS);

    VkCommandBuffer unset_line_width_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &unset_line_width_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(unset_line_width_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdBeginRenderPass(unset_line_width_command, &render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(unset_line_width_command,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      dynamic_line_pipeline);
    vkCmdDraw(unset_line_width_command, 2, 1, 0, 0);
    vkCmdEndRenderPass(unset_line_width_command);
    assert(vkEndCommandBuffer(unset_line_width_command) ==
           VK_ERROR_INITIALIZATION_FAILED);

    VkCommandBuffer unset_viewport_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &unset_viewport_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(unset_viewport_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdBeginRenderPass(unset_viewport_command, &render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(unset_viewport_command,
                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                      dynamic_viewport_pipeline);
    vkCmdSetDepthBias(unset_viewport_command, 0.0f, 0.0f, 0.0f);
    vkCmdDraw(unset_viewport_command, 3, 1, 0, 0);
    vkCmdEndRenderPass(unset_viewport_command);
    assert(vkEndCommandBuffer(unset_viewport_command) ==
           VK_ERROR_INITIALIZATION_FAILED);

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
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    VkImageCopy partial_image_copy = image_copy;
    partial_image_copy.extent.width = 128u;
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdCopyImage(invalid_copy_command, color_image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, color_image_1,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u,
                   &partial_image_copy);
    assert(vkEndCommandBuffer(invalid_copy_command) == VK_SUCCESS);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdCopyImage(invalid_copy_command, color_image,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, color_image_1,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &image_copy);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdUpdateBuffer(invalid_copy_command, output_buffer, 2u,
                      sizeof(update_data), update_data);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdFillBuffer(invalid_copy_command, output_buffer, 0u, 6u, 0u);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);

    const VkClearColorValue clear_color = {{0u, 0u, 0u, 0u}};
    const VkImageSubresourceRange color_range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdClearColorImage(invalid_copy_command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1u, &color_range);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdBlitImage(invalid_copy_command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, color_image_1, VK_IMAGE_LAYOUT_GENERAL,
        0u, NULL, VK_FILTER_NEAREST);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdClearDepthStencilImage(invalid_copy_command, depth_image,
        VK_IMAGE_LAYOUT_GENERAL, NULL, 0u, NULL);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdClearAttachments(invalid_copy_command, 0u, NULL, 0u, NULL);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdResolveImage(invalid_copy_command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, color_image_1, VK_IMAGE_LAYOUT_GENERAL,
        0u, NULL);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);

    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vk_ps5_command_buffer_native_draw_count(command) == 11u);


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

    assert(vkResetCommandBuffer(command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 0u, 1u, &texture_set,
                            0u, NULL);
    vkCmdBeginRenderPass(command, &native_render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    vkCmdBindVertexBuffers(command, 0u, 1u, &vertex_buffer,
                           &native_vertex_offset);
    vkCmdDraw(command, 3u, 1u, 0u, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindIndexBuffer(command, index_buffer, 0u, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(command) == 2u);
    assert(vkResetFences(device, 1u, &fence) == VK_SUCCESS);
    assert(vkQueueSubmit(queue, 1u, &submit_info, fence) == VK_SUCCESS);
    assert(vkGetFenceStatus(device, fence) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_EXECUTABLE);

    assert(vkResetCommandPool(device, pool, 0u) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
    assert(vk_ps5_command_buffer_native_state(dynamic_rendering_command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);

    vkDestroyFence(device, fence, NULL);
    vkDestroyQueryPool(device, query_pool, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyPipeline(device, tessellation_pipeline, NULL);
    vkDestroyPipeline(device, dynamic_viewport_pipeline, NULL);
    vkDestroyPipeline(device, geometry_pipeline, NULL);
    vkDestroyPipeline(device, point_pipeline, NULL);
    vkDestroyPipeline(device, point_list_pipeline, NULL);
    vkDestroyPipeline(device, dynamic_line_pipeline, NULL);
    vkDestroyPipeline(device, wide_line_pipeline, NULL);
    vkDestroyPipeline(device, line_pipeline, NULL);
    vkDestroyPipeline(device, static_bias_pipeline, NULL);
    vkDestroyPipeline(device, logic_pipeline, NULL);
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyPipeline(device, dynamic_rendering_pipeline, NULL);
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
    vkDestroySampler(device, custom_sampler, NULL);
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
    vkDestroyDescriptorUpdateTemplate(device, descriptor_template, NULL);
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
