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

static void create_bound_test_image(VkDevice device,
    const VkImageCreateInfo *create_info, VkImage *image_out,
    VkDeviceMemory *memory_out)
{
    assert(vkCreateImage(device, create_info, NULL, image_out) == VK_SUCCESS);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, *image_out, &requirements);
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = 0u,
    };
    assert(vkAllocateMemory(device, &allocation, NULL, memory_out) ==
        VK_SUCCESS);
    assert(vkBindImageMemory(device, *image_out, *memory_out, 0u) ==
        VK_SUCCESS);
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

static void test_sparse_inline_push_constant_readiness(void)
{
    AgcShaderReflection reflection = AGC_SHADER_REFLECTION_INIT;
    const AgcShaderPushConstantRange full_range = {
        0u, 128u, 4u, 1u << kAgcShaderStageVs,
    };
    const AgcShaderPushConstantRange high_range = {
        48u, 80u, 4u, 1u << kAgcShaderStageVs,
    };

    reflection.stage = kAgcShaderStageVs;
    reflection.inline_push_constant_mask = UINT64_C(0xfffff0ff);
    reflection.user_sgpr_count = 2u;
    reflection.user_sgprs[0] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_BASE_VERTEX, 0u,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0, 1u,
    };
    reflection.user_sgprs[1] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_INLINE_PUSH_CONSTANT, 0u,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 1u, 1u,
    };
    assert(vk_ps5_native_push_constant_required_mask(
        &reflection, &full_range) == UINT64_C(0xfffff0ff));
    assert(vk_ps5_native_push_constant_required_mask(
        &reflection, &high_range) == UINT64_C(0xfffff000));

    reflection.user_sgprs[1] = (AgcShaderUserSgpr){
        AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER, 0u,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 1u, 1u,
    };
    assert(vk_ps5_native_push_constant_required_mask(
        &reflection, &full_range) == UINT64_C(0xffffffff));
    assert(vk_ps5_native_push_constant_required_mask(
        &reflection, &high_range) == UINT64_C(0xfffff000));
}

int main(int argc, char **argv)
{
    assert(argc == 7);
    test_sparse_inline_push_constant_readiness();
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

    const VkFormat ignored_texture_view_format = VK_FORMAT_R8_UNORM;
    const VkImageFormatListCreateInfo empty_texture_format_list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 0u,
        .pViewFormats = &ignored_texture_view_format,
    };
    const VkImageCreateInfo texture_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &empty_texture_format_list,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
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
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView texture_view;
    assert(vkCreateImageView(device, &texture_view_info, NULL,
                             &texture_view) == VK_SUCCESS);
    VkImageViewCreateInfo packed_texture_view_info = texture_view_info;
    packed_texture_view_info.format = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    VkImageView packed_texture_view;
    assert(vkCreateImageView(device, &packed_texture_view_info, NULL,
                             &packed_texture_view) == VK_SUCCESS);
    vkDestroyImageView(device, packed_texture_view, NULL);
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
    VkSamplerCreateInfo unnormalized_sampler_info = sampler_info;
    unnormalized_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    unnormalized_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    unnormalized_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    unnormalized_sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    unnormalized_sampler_info.unnormalizedCoordinates = VK_TRUE;
    VkSampler unnormalized_sampler;
    assert(vkCreateSampler(device, &unnormalized_sampler_info, NULL,
                           &unnormalized_sampler) == VK_SUCCESS);
    VkSamplerCreateInfo nearest_unnormalized_sampler_info =
        unnormalized_sampler_info;
    nearest_unnormalized_sampler_info.minFilter = VK_FILTER_NEAREST;
    nearest_unnormalized_sampler_info.magFilter = VK_FILTER_NEAREST;
    VkSampler nearest_unnormalized_sampler;
    assert(vkCreateSampler(device, &nearest_unnormalized_sampler_info, NULL,
                           &nearest_unnormalized_sampler) == VK_SUCCESS);
    VkSamplerCreateInfo invalid_unnormalized_sampler_info =
        unnormalized_sampler_info;
    VkSampler invalid_unnormalized_sampler = VK_NULL_HANDLE;
    invalid_unnormalized_sampler_info.magFilter = VK_FILTER_NEAREST;
    assert(vkCreateSampler(device, &invalid_unnormalized_sampler_info, NULL,
                           &invalid_unnormalized_sampler) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_unnormalized_sampler == VK_NULL_HANDLE);
    invalid_unnormalized_sampler_info = unnormalized_sampler_info;
    invalid_unnormalized_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    assert(vkCreateSampler(device, &invalid_unnormalized_sampler_info, NULL,
                           &invalid_unnormalized_sampler) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_unnormalized_sampler == VK_NULL_HANDLE);
    invalid_unnormalized_sampler_info = unnormalized_sampler_info;
    invalid_unnormalized_sampler_info.maxLod = 1.0f;
    assert(vkCreateSampler(device, &invalid_unnormalized_sampler_info, NULL,
                           &invalid_unnormalized_sampler) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_unnormalized_sampler == VK_NULL_HANDLE);
    invalid_unnormalized_sampler_info = unnormalized_sampler_info;
    invalid_unnormalized_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    assert(vkCreateSampler(device, &invalid_unnormalized_sampler_info, NULL,
                           &invalid_unnormalized_sampler) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_unnormalized_sampler == VK_NULL_HANDLE);
    invalid_unnormalized_sampler_info = unnormalized_sampler_info;
    invalid_unnormalized_sampler_info.anisotropyEnable = VK_TRUE;
    invalid_unnormalized_sampler_info.maxAnisotropy = 2.0f;
    assert(vkCreateSampler(device, &invalid_unnormalized_sampler_info, NULL,
                           &invalid_unnormalized_sampler) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_unnormalized_sampler == VK_NULL_HANDLE);
    invalid_unnormalized_sampler_info = unnormalized_sampler_info;
    invalid_unnormalized_sampler_info.compareEnable = VK_TRUE;
    assert(vkCreateSampler(device, &invalid_unnormalized_sampler_info, NULL,
                           &invalid_unnormalized_sampler) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_unnormalized_sampler == VK_NULL_HANDLE);
    const VkSamplerBorderColorComponentMappingCreateInfoEXT border_mapping = {
        .sType =
            VK_STRUCTURE_TYPE_SAMPLER_BORDER_COLOR_COMPONENT_MAPPING_CREATE_INFO_EXT,
        .components = {
            VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G,
            VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A,
        },
        .srgb = VK_FALSE,
    };
    const VkSamplerCustomBorderColorCreateInfoEXT custom_border_info = {
        .sType =
            VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
        .pNext = &border_mapping,
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
    VkSamplerBorderColorComponentMappingCreateInfoEXT invalid_border_mapping =
        border_mapping;
    invalid_border_mapping.srgb = 2u;
    VkSamplerCustomBorderColorCreateInfoEXT invalid_custom_border =
        custom_border_info;
    invalid_custom_border.pNext = &invalid_border_mapping;
    VkSamplerCreateInfo invalid_border_sampler_info = custom_sampler_info;
    invalid_border_sampler_info.pNext = &invalid_custom_border;
    VkSampler invalid_border_sampler = VK_NULL_HANDLE;
    assert(vkCreateSampler(device, &invalid_border_sampler_info, NULL,
                           &invalid_border_sampler) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(invalid_border_sampler == VK_NULL_HANDLE);
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
    const VkPushConstantRange graphics_push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
            VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0u,
        .size = sizeof(uint32_t),
    };
    const VkPipelineLayoutCreateInfo graphics_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = graphics_set_layouts,
        .pushConstantRangeCount = 1u,
        .pPushConstantRanges = &graphics_push_range,
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
        /* GENERAL is valid for a color attachment and is used by Eden's
           first-frame presentation render pass. Keep the second attachment
           optimal so the mixed-layout path is covered as well. */
        {0, VK_IMAGE_LAYOUT_GENERAL},
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
    VkAttachmentReference invalid_color = colors[0];
    invalid_color.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkSubpassDescription invalid_color_subpass = subpass;
    invalid_color_subpass.colorAttachmentCount = 1u;
    invalid_color_subpass.pColorAttachments = &invalid_color;
    VkRenderPassCreateInfo invalid_color_render_pass_info = render_pass_info;
    invalid_color_render_pass_info.pSubpasses = &invalid_color_subpass;
    VkRenderPass invalid_color_render_pass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(device, &invalid_color_render_pass_info, NULL,
        &invalid_color_render_pass) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_color_render_pass == VK_NULL_HANDLE);
    VkSubpassDescription unsupported_subpass = subpass;
    unsupported_subpass.inputAttachmentCount = 1u;
    unsupported_subpass.pInputAttachments = colors;
    VkRenderPassCreateInfo unsupported_render_pass_info = render_pass_info;
    unsupported_render_pass_info.pSubpasses = &unsupported_subpass;
    VkRenderPass unsupported_render_pass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(device, &unsupported_render_pass_info, NULL,
        &unsupported_render_pass) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(unsupported_render_pass == VK_NULL_HANDLE);
    unsupported_subpass = subpass;
    unsupported_subpass.pResolveAttachments = colors;
    unsupported_render_pass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(device, &unsupported_render_pass_info, NULL,
        &unsupported_render_pass) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(unsupported_render_pass == VK_NULL_HANDLE);
    const uint32_t preserve_attachment = 0u;
    unsupported_subpass = subpass;
    unsupported_subpass.preserveAttachmentCount = 1u;
    unsupported_subpass.pPreserveAttachments = &preserve_attachment;
    unsupported_render_pass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(device, &unsupported_render_pass_info, NULL,
        &unsupported_render_pass) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(unsupported_render_pass == VK_NULL_HANDLE);
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
    assert(vk_ps5_image_blit_format(color_image) ==
           VK_FORMAT_R8G8B8A8_UNORM);
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
    const VkFormat eden_32_bit_view_formats[] = {
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R16G16_UINT,
        VK_FORMAT_R32_UINT,
        VK_FORMAT_R16G16_SINT,
        VK_FORMAT_R32_SINT,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_A8B8G8R8_SNORM_PACK32,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_A8B8G8R8_SRGB_PACK32,
        VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_A8B8G8R8_UINT_PACK32,
        VK_FORMAT_A8B8G8R8_SINT_PACK32,
        VK_FORMAT_A2B10G10R10_UINT_PACK32,
    };
    const VkImageFormatListCreateInfo eden_32_bit_format_list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = (uint32_t)(sizeof(eden_32_bit_view_formats) /
                                     sizeof(eden_32_bit_view_formats[0])),
        .pViewFormats = eden_32_bit_view_formats,
    };
    VkImageCreateInfo extended_storage_image_info = image_info;
    extended_storage_image_info.pNext = &eden_32_bit_format_list;
    extended_storage_image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
        VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    extended_storage_image_info.format =
        VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    extended_storage_image_info.extent = (VkExtent3D){480u, 480u, 1u};
    extended_storage_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    extended_storage_image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    extended_storage_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    const VkPhysicalDeviceImageFormatInfo2 extended_storage_format_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &eden_32_bit_format_list,
        .format = extended_storage_image_info.format,
        .type = extended_storage_image_info.imageType,
        .tiling = extended_storage_image_info.tiling,
        .usage = extended_storage_image_info.usage,
        .flags = extended_storage_image_info.flags,
    };
    VkImageFormatProperties2 extended_storage_format_properties = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
    };
    assert(vkGetPhysicalDeviceImageFormatProperties2(physical,
               &extended_storage_format_info,
               &extended_storage_format_properties) == VK_SUCCESS);
    const VkFormat no_storage_view_format =
        VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    const VkImageFormatListCreateInfo no_storage_format_list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 1u,
        .pViewFormats = &no_storage_view_format,
    };
    VkPhysicalDeviceImageFormatInfo2 no_storage_format_info =
        extended_storage_format_info;
    no_storage_format_info.pNext = &no_storage_format_list;
    assert(vkGetPhysicalDeviceImageFormatProperties2(physical,
               &no_storage_format_info,
               &extended_storage_format_properties) ==
           VK_ERROR_FORMAT_NOT_SUPPORTED);
    VkImage extended_storage_image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &extended_storage_image_info, NULL,
                         &extended_storage_image) == VK_SUCCESS);
    const VkImageViewUsageCreateInfo storage_view_usage = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT,
    };
    const VkImageViewCreateInfo storage_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &storage_view_usage,
        .image = extended_storage_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_A8B8G8R8_SNORM_PACK32,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        },
    };
    VkImageView storage_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &storage_view_info, NULL,
                             &storage_view) == VK_SUCCESS);
    vkDestroyImageView(device, storage_view, NULL);
    vkDestroyImage(device, extended_storage_image, NULL);
    extended_storage_image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    extended_storage_image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &extended_storage_image_info, NULL,
                         &extended_storage_image) ==
           VK_ERROR_FORMAT_NOT_SUPPORTED);
    assert(extended_storage_image == VK_NULL_HANDLE);
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
    VkImageCreateInfo mutable_clear_image_info = image_info;
    mutable_clear_image_info.pNext = &eden_32_bit_format_list;
    mutable_clear_image_info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    mutable_clear_image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    VkImage mutable_clear_image;
    VkDeviceMemory mutable_clear_memory;
    create_bound_test_image(device, &mutable_clear_image_info,
        &mutable_clear_image, &mutable_clear_memory);
    VkImageViewCreateInfo mutable_clear_view_info = image_view_info;
    mutable_clear_view_info.image = mutable_clear_image;
    mutable_clear_view_info.format = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    VkImageView mutable_clear_view;
    assert(vkCreateImageView(device, &mutable_clear_view_info, NULL,
        &mutable_clear_view) == VK_SUCCESS);
    VkImageViewCreateInfo omitted_compatible_view_info =
        mutable_clear_view_info;
    omitted_compatible_view_info.format =
        VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    VkImageView omitted_compatible_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &omitted_compatible_view_info, NULL,
        &omitted_compatible_view) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(omitted_compatible_view == VK_NULL_HANDLE);
    const VkImageCreateInfo image_3d_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_3D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {32u, 24u, 4u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkImage image_3d_source;
    VkImage image_3d_destination;
    VkDeviceMemory image_3d_source_memory;
    VkDeviceMemory image_3d_destination_memory;
    create_bound_test_image(device, &image_3d_info, &image_3d_source,
        &image_3d_source_memory);
    create_bound_test_image(device, &image_3d_info, &image_3d_destination,
        &image_3d_destination_memory);
    VkImageCreateInfo self_blit_image_info = image_info;
    self_blit_image_info.mipLevels = 2u;
    VkImage self_blit_image;
    VkDeviceMemory self_blit_memory;
    create_bound_test_image(device, &self_blit_image_info, &self_blit_image,
        &self_blit_memory);
    VkImageCreateInfo resolve_source_info = image_info;
    resolve_source_info.samples = VK_SAMPLE_COUNT_4_BIT;
    resolve_source_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    resolve_source_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    resolve_source_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage resolve_source_image;
    const VkResult resolve_create_result = vkCreateImage(device,
        &resolve_source_info, NULL, &resolve_source_image);
    if (resolve_create_result != VK_SUCCESS)
        fprintf(stderr, "resolve source create failed: %d\n",
            resolve_create_result);
    assert(resolve_create_result == VK_SUCCESS);
    VkMemoryRequirements resolve_source_requirements;
    vkGetImageMemoryRequirements(device, resolve_source_image,
        &resolve_source_requirements);
    const VkMemoryAllocateInfo resolve_source_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = resolve_source_requirements.size,
        .memoryTypeIndex = 1u,
    };
    VkDeviceMemory resolve_source_memory;
    assert(vkAllocateMemory(device, &resolve_source_memory_info, NULL,
        &resolve_source_memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device, resolve_source_image,
        resolve_source_memory, 0u) == VK_SUCCESS);
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
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
        .subresourceRange = {
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            0, 1, 0, 1,
        },
    };
    VkImageView depth_view;
    assert(vkCreateImageView(device, &depth_view_info, NULL,
                             &depth_view) == VK_SUCCESS);
    assert(!vk_ps5_image_view_has_native(depth_view));
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
    const VkAttachmentDescription mutable_clear_attachment = {
        .format = VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference mutable_clear_reference = {
        .attachment = 0u,
        .layout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkSubpassDescription mutable_clear_subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &mutable_clear_reference,
    };
    const VkRenderPassCreateInfo mutable_clear_render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1u,
        .pAttachments = &mutable_clear_attachment,
        .subpassCount = 1u,
        .pSubpasses = &mutable_clear_subpass,
    };
    VkRenderPass mutable_clear_render_pass;
    assert(vkCreateRenderPass(device, &mutable_clear_render_pass_info, NULL,
        &mutable_clear_render_pass) == VK_SUCCESS);
    const VkFramebufferCreateInfo mutable_clear_framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = mutable_clear_render_pass,
        .attachmentCount = 1u,
        .pAttachments = &mutable_clear_view,
        .width = 256u,
        .height = 256u,
        .layers = 1u,
    };
    VkFramebuffer mutable_clear_framebuffer;
    assert(vkCreateFramebuffer(device, &mutable_clear_framebuffer_info, NULL,
        &mutable_clear_framebuffer) == VK_SUCCESS);
    VkAttachmentDescription compatible_attachment =
        mutable_clear_attachment;
    compatible_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    compatible_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    compatible_attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference compatible_reference = mutable_clear_reference;
    compatible_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription compatible_subpass = mutable_clear_subpass;
    compatible_subpass.pColorAttachments = &compatible_reference;
    VkRenderPassCreateInfo compatible_render_pass_info =
        mutable_clear_render_pass_info;
    compatible_render_pass_info.pAttachments = &compatible_attachment;
    compatible_render_pass_info.pSubpasses = &compatible_subpass;
    VkRenderPass compatible_render_pass;
    assert(vkCreateRenderPass(device, &compatible_render_pass_info, NULL,
        &compatible_render_pass) == VK_SUCCESS);
    VkAttachmentDescription incompatible_format_attachment =
        mutable_clear_attachment;
    incompatible_format_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    VkRenderPassCreateInfo incompatible_format_render_pass_info =
        mutable_clear_render_pass_info;
    incompatible_format_render_pass_info.pAttachments =
        &incompatible_format_attachment;
    VkRenderPass incompatible_format_render_pass;
    assert(vkCreateRenderPass(device, &incompatible_format_render_pass_info,
        NULL, &incompatible_format_render_pass) == VK_SUCCESS);
    VkAttachmentReference incompatible_reference = mutable_clear_reference;
    incompatible_reference.attachment = VK_ATTACHMENT_UNUSED;
    VkSubpassDescription incompatible_reference_subpass =
        mutable_clear_subpass;
    incompatible_reference_subpass.pColorAttachments =
        &incompatible_reference;
    VkRenderPassCreateInfo incompatible_reference_render_pass_info =
        mutable_clear_render_pass_info;
    incompatible_reference_render_pass_info.pSubpasses =
        &incompatible_reference_subpass;
    VkRenderPass incompatible_reference_render_pass;
    assert(vkCreateRenderPass(device, &incompatible_reference_render_pass_info,
        NULL, &incompatible_reference_render_pass) == VK_SUCCESS);
    VkAttachmentDescription incompatible_flag_attachment =
        mutable_clear_attachment;
    incompatible_flag_attachment.flags =
        VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT;
    VkRenderPassCreateInfo incompatible_flag_render_pass_info =
        mutable_clear_render_pass_info;
    incompatible_flag_render_pass_info.pAttachments =
        &incompatible_flag_attachment;
    VkRenderPass incompatible_flag_render_pass;
    assert(vkCreateRenderPass(device, &incompatible_flag_render_pass_info,
        NULL, &incompatible_flag_render_pass) == VK_SUCCESS);
    const VkFormat imageless_color_formats[] = {
        VK_FORMAT_R8G8B8A8_UNORM,
    };
    const VkFormat imageless_depth_formats[] = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
    };
    const VkFramebufferAttachmentImageInfo imageless_attachment_infos[] = {
        {
            .sType =
                VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .width = 256u,
            .height = 256u,
            .layerCount = 1u,
            .viewFormatCount = 1u,
            .pViewFormats = imageless_color_formats,
        },
        {
            .sType =
                VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .width = 256u,
            .height = 256u,
            .layerCount = 1u,
            .viewFormatCount = 1u,
            .pViewFormats = imageless_color_formats,
        },
        {
            .sType =
                VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .width = 256u,
            .height = 256u,
            .layerCount = 1u,
            .viewFormatCount = 1u,
            .pViewFormats = imageless_depth_formats,
        },
    };
    const VkFramebufferAttachmentsCreateInfo imageless_attachments_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,
        .attachmentImageInfoCount = 3u,
        .pAttachmentImageInfos = imageless_attachment_infos,
    };
    VkFramebufferCreateInfo imageless_framebuffer_info = framebuffer_info;
    imageless_framebuffer_info.pNext = &imageless_attachments_info;
    imageless_framebuffer_info.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
    imageless_framebuffer_info.pAttachments = NULL;
    VkFramebufferCreateInfo missing_imageless_info =
        imageless_framebuffer_info;
    missing_imageless_info.pNext = NULL;
    VkFramebuffer invalid_imageless_framebuffer = VK_NULL_HANDLE;
    assert(vkCreateFramebuffer(device, &missing_imageless_info, NULL,
        &invalid_imageless_framebuffer) == VK_ERROR_INITIALIZATION_FAILED);
    assert(invalid_imageless_framebuffer == VK_NULL_HANDLE);
    VkFramebuffer imageless_framebuffer;
    assert(vkCreateFramebuffer(device, &imageless_framebuffer_info, NULL,
                               &imageless_framebuffer) == VK_SUCCESS);
    const VkPipelineShaderStageCreateInfo graphics_stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", NULL},
    };
    VkVertexInputBindingDescription vertex_bindings[32];
    for (uint32_t binding = 0u; binding < 32u; ++binding) {
        vertex_bindings[binding] = (VkVertexInputBindingDescription){
            .binding = binding,
            .stride = binding == 0u ? 2u * sizeof(float) : 0u,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
    }
    const VkVertexInputAttributeDescription vertex_attribute = {
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 32,
        .pVertexBindingDescriptions = vertex_bindings,
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
            .srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_COLOR,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA,
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
        .depthBoundsTestEnable = VK_TRUE,
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
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
    VkVertexInputBindingDescription zero_stride_binding = vertex_bindings[0];
    zero_stride_binding.stride = 0u;
    VkPipelineVertexInputStateCreateInfo zero_stride_vertex_input =
        vertex_input;
    zero_stride_vertex_input.vertexBindingDescriptionCount = 1u;
    zero_stride_vertex_input.pVertexBindingDescriptions =
        &zero_stride_binding;
    VkGraphicsPipelineCreateInfo zero_stride_info = graphics_info;
    zero_stride_info.pVertexInputState = &zero_stride_vertex_input;
    VkPipeline zero_stride_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &zero_stride_info, NULL, &zero_stride_pipeline) == VK_SUCCESS);
    const VkDynamicState zink_core_dynamic_states[] = {
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
    };
    const VkPipelineDynamicStateCreateInfo zink_core_dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 6u,
        .pDynamicStates = zink_core_dynamic_states,
    };
    VkGraphicsPipelineCreateInfo zink_core_dynamic_info = graphics_info;
    zink_core_dynamic_info.pDynamicState = &zink_core_dynamic_state;
    VkPipeline zink_core_dynamic_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &zink_core_dynamic_info, NULL, &zink_core_dynamic_pipeline) ==
        VK_SUCCESS);
    const VkPipelineColorBlendAttachmentState unused_constant_attachments[] = {
        {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT},
        {.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT},
    };
    VkPipelineColorBlendStateCreateInfo unused_constant_blend = blend;
    unused_constant_blend.pAttachments = unused_constant_attachments;
    VkGraphicsPipelineCreateInfo unused_constant_info = zink_core_dynamic_info;
    unused_constant_info.pColorBlendState = &unused_constant_blend;
    VkPipeline unused_constant_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &unused_constant_info, NULL, &unused_constant_pipeline) == VK_SUCCESS);
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
    const VkPipelineRenderingCreateInfo color_only_pipeline_rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 2,
        .pColorAttachmentFormats = dynamic_color_formats,
    };
    VkPipelineRasterizationStateCreateInfo color_only_raster = rasterization;
    color_only_raster.depthBiasEnable = VK_FALSE;
    VkGraphicsPipelineCreateInfo color_only_dynamic_info = graphics_info;
    color_only_dynamic_info.pNext = &color_only_pipeline_rendering;
    color_only_dynamic_info.pRasterizationState = &color_only_raster;
    color_only_dynamic_info.pDepthStencilState = NULL;
    color_only_dynamic_info.renderPass = VK_NULL_HANDLE;
    color_only_dynamic_info.subpass = 0;
    VkPipeline color_only_dynamic_pipeline;
    assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &color_only_dynamic_info, NULL, &color_only_dynamic_pipeline) ==
        VK_SUCCESS);
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
    const float dynamic_blend_constants[4] = {0.125f, 0.25f, 0.5f, 1.0f};
    vkCmdSetBlendConstants(command, dynamic_blend_constants);
    vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK,
                             0x3cu);
    vkCmdSetStencilCompareMask(command, VK_STENCIL_FACE_FRONT_AND_BACK,
                              0x7eu);
    vkCmdSetStencilWriteMask(command, VK_STENCIL_FACE_FRONT_AND_BACK,
                            0x81u);
    vkCmdSetDepthBounds(command, 0.25f, 0.75f);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      zink_core_dynamic_pipeline);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      unused_constant_pipeline);
    vkCmdSetBlendConstants(command, dynamic_blend_constants);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
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
    const VkRenderPassBeginInfo mutable_clear_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = mutable_clear_render_pass,
        .framebuffer = mutable_clear_framebuffer,
        .renderArea = {{0, 0}, {256u, 256u}},
    };
    const VkClearAttachment mutable_clear_command = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0u,
        .clearValue.color.float32 = {0.125f, 0.25f, 0.5f, 1.0f},
    };
    const VkClearRect mutable_clear_rect = {
        .rect = {{0, 0}, {256u, 256u}},
        .baseArrayLayer = 0u,
        .layerCount = 1u,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBeginRenderPass(command, &mutable_clear_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdClearAttachments(command, 1u, &mutable_clear_command, 1u,
                          &mutable_clear_rect);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(command) == 1u);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    vkDestroyRenderPass(device, mutable_clear_render_pass, NULL);
    mutable_clear_render_pass = VK_NULL_HANDLE;
    VkRenderPassBeginInfo compatible_begin = mutable_clear_begin;
    compatible_begin.renderPass = compatible_render_pass;
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdBeginRenderPass(command, &compatible_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    VkRenderPassBeginInfo incompatible_begin = mutable_clear_begin;
    const VkRenderPass incompatible_render_passes[] = {
        incompatible_format_render_pass,
        incompatible_reference_render_pass,
        incompatible_flag_render_pass,
    };
    for (uint32_t incompatible_index = 0u;
         incompatible_index < sizeof(incompatible_render_passes) /
             sizeof(incompatible_render_passes[0]); ++incompatible_index) {
        incompatible_begin.renderPass =
            incompatible_render_passes[incompatible_index];
        assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
        vkCmdBeginRenderPass(command, &incompatible_begin,
                             VK_SUBPASS_CONTENTS_INLINE);
        assert(vk_ps5_command_buffer_record_error(command) ==
               VK_ERROR_INITIALIZATION_FAILED);
        assert(vkEndCommandBuffer(command) ==
               VK_ERROR_INITIALIZATION_FAILED);
        assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    }
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
    const VkBufferMemoryBarrier copy_to_host_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output_buffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         2u, copy_barriers, 0u, NULL);
    vkCmdCopyBuffer(command, indirect_buffer, output_buffer, 2u,
                    buffer_copies);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL,
                         1u, &copy_to_host_barrier, 0u, NULL);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    /* A 576-row guest upload needs roughly 4,032 native dwords by itself and
     * can arrive late in a busy command buffer.  Keep an aggregate command
     * stream above the old 64-KiB DCB budget as a capacity regression. */
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    for (uint32_t copy_index = 0u; copy_index < 3000u; ++copy_index)
        vkCmdCopyBuffer(command, indirect_buffer, output_buffer, 1u,
                        &buffer_copies[0]);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    const VkMemoryBarrier eden_global_transfer_barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT |
            VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 1u,
                         &eden_global_transfer_barrier, 0u, NULL, 0u, NULL);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    /* Eden uses a broad memory-write scope while returning a transfer source
     * to GENERAL.  MEMORY_WRITE is not evidence of storage-image use: this
     * color/transfer image intentionally has no VK_IMAGE_USAGE_STORAGE_BIT.
     * Preserve its concrete CopySource state until its next typed use. */
    const VkImageMemoryBarrier transfer_source_barriers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = color_image,
            .subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
            },
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = color_image,
            .subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
            },
        },
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &transfer_source_barriers[0]);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &transfer_source_barriers[1]);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    const VkClearColorValue barrier_clear_color = {
        .uint32 = {0x11u, 0x22u, 0x33u, 0x44u},
    };
    const VkImageSubresourceRange barrier_clear_range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    vkCmdClearColorImage(command, color_image, VK_IMAGE_LAYOUT_GENERAL,
                         &barrier_clear_color, 1u, &barrier_clear_range);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    VkImageMemoryBarrier generic_after_write_barriers[] = {
        transfer_source_barriers[0], transfer_source_barriers[1],
    };
    generic_after_write_barriers[0].dstAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    generic_after_write_barriers[0].newLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    generic_after_write_barriers[1].srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;
    generic_after_write_barriers[1].oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &generic_after_write_barriers[0]);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &generic_after_write_barriers[1]);
    assert(vk_ps5_command_buffer_record_error(command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkEndCommandBuffer(command) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    /* Eden returns transfer-written color images to GENERAL with a
     * conservative scope covering every shader and attachment role.  The
     * color aspect and COLOR_ATTACHMENT usage make ColorTarget the concrete
     * native destination, including the required write dependency. */
    generic_after_write_barriers[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &generic_after_write_barriers[0]);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &generic_after_write_barriers[1]);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
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
    const VkBufferMemoryBarrier disjoint_copy_state = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = indirect_buffer,
        .offset = 96u,
        .size = 32u,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         1u, &disjoint_copy_state, 0u, NULL);
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

    const VkBufferMemoryBarrier whole_write_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output_buffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    const VkBufferMemoryBarrier partial_read_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output_buffer,
        .offset = 0u,
        .size = 16u,
    };
    VkBufferMemoryBarrier partial_zero_source_barrier = partial_read_barrier;
    partial_zero_source_barrier.srcAccessMask = 0u;
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                         0u, NULL, 1u, &whole_write_barrier, 0u, NULL);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                         0u, NULL, 1u, &partial_read_barrier, 0u, NULL);
    /* A partial transition makes the ICD's whole-buffer mirror mixed.  The
     * next zero-source barrier must query this exact range from OpenAGC. */
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                         0u, NULL, 1u, &partial_zero_source_barrier,
                         0u, NULL);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    const uint32_t vertex_push_value = UINT32_C(0x11223344);
    const uint32_t fragment_push_value = UINT32_C(0xaabbccdd);
    uint32_t cached_push_value = 0u;
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPushConstants(command, graphics_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0u,
                       sizeof(vertex_push_value), &vertex_push_value);
    vkCmdPushConstants(command, graphics_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0u,
                       sizeof(fragment_push_value), &fragment_push_value);
    assert(vk_ps5_command_buffer_push_constant_word(command,
        kAgcShaderStageVs, 0u, &cached_push_value));
    assert(cached_push_value == vertex_push_value);
    assert(vk_ps5_command_buffer_push_constant_word(command,
        kAgcShaderStagePs, 0u, &cached_push_value));
    assert(cached_push_value == fragment_push_value);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
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
    const VkImageMemoryBarrier native_separate_layout_barriers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depth_image,
            .subresourceRange = {
                VK_IMAGE_ASPECT_DEPTH_BIT, 0u, 1u, 0u, 1u,
            },
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depth_image,
            .subresourceRange = {
                VK_IMAGE_ASPECT_STENCIL_BIT, 0u, 1u, 0u, 1u,
            },
        },
    };
    const VkRenderPassBeginInfo native_render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {256, 256}},
    };
    const VkRenderPassAttachmentBeginInfo imageless_attachment_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
        .attachmentCount = 3u,
        .pAttachments = framebuffer_attachments,
    };
    const VkRenderPassBeginInfo imageless_render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = &imageless_attachment_begin,
        .renderPass = render_pass,
        .framebuffer = imageless_framebuffer,
        .renderArea = {{0, 0}, {256, 256}},
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    const VkRect2D native_color_target_control_rect = {
        .offset = {0, 0},
        .extent = {256u, 256u},
    };
    assert(vk_ps5_command_buffer_native_color_target_control(command,
        graphics_pipeline, 0u, &native_color_target_control_rect) ==
        VK_ERROR_INITIALIZATION_FAILED);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u, 0u, NULL, 3u, native_graphics_barriers + 1u,
                         1u, &native_texture_barrier);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         0u, 0u, NULL, 0u, NULL, 2u,
                         native_separate_layout_barriers);
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
    const VkClearAttachment partial_clear_attachments[] = {
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 1u,
            .clearValue.color.float32 = {0.25f, 0.5f, 0.75f, 1.0f},
        },
        {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                VK_IMAGE_ASPECT_STENCIL_BIT,
            .clearValue.depthStencil = {0.375f, 0x5au},
        },
    };
    const VkClearRect partial_clear_rect = {
        .rect = {{32, 48}, {96u, 80u}},
        .baseArrayLayer = 0u,
        .layerCount = 1u,
    };
    vkCmdClearAttachments(command, 2u, partial_clear_attachments,
        1u, &partial_clear_rect);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdSetDepthBias(command, 2.0f, 0.25f, -1.5f);
    const VkDeviceSize native_vertex_offset = 0u;
    vkCmdBindVertexBuffers2(command, 0u, 1u, &vertex_buffer,
                            &native_vertex_offset, NULL, NULL);
    vkCmdDraw(command, 3u, 1u, 0u, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBindIndexBuffer(command, index_buffer, 0u, VK_INDEX_TYPE_UINT16);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdBeginRenderPass(command, &imageless_render_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdEndRenderPass(command);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(command) == 4u);
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

    /* Eden resolves occlusion samples into a host-visible transfer buffer
     * after leaving the render pass. The synchronous PS5 submit path reduces
     * OpenAGC's opaque per-RB record before signaling Vulkan completion. */
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdCopyQueryPoolResults(command, VK_NULL_HANDLE, UINT32_MAX, 0u,
                              VK_NULL_HANDLE, UINT64_MAX, 0u, ~0u);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    vkCmdCopyQueryPoolResults(command, query_pool, 0u, 2u,
                              output_buffer, 32u, sizeof(uint64_t),
                              VK_QUERY_RESULT_WAIT_BIT |
                                  VK_QUERY_RESULT_64_BIT);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    const VkBufferMemoryBarrier query_copy_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output_buffer,
        .offset = 32u,
        .size = 2u * sizeof(uint64_t),
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         1u, &query_copy_barrier, 0u, NULL);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);

    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdCopyQueryPoolResults(command, query_pool, 0u, 1u,
                              output_buffer, 0u, 4u,
                              VK_QUERY_RESULT_64_BIT);
    assert(vk_ps5_command_buffer_record_error(command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
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
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkEndCommandBuffer(command) == VK_ERROR_FEATURE_NOT_PRESENT);
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

    /* Descriptor readiness must use the exact bound range. A partial buffer
     * barrier intentionally leaves the coarse whole-buffer mirror undefined. */
    const VkDescriptorBufferInfo partial_output_info = {
        output_buffer, 16u, 24u,
    };
    const VkWriteDescriptorSet partial_output_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_sets[1],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &partial_output_info,
    };
    vkUpdateDescriptorSets(device, 1u, &partial_output_write, 0u, NULL);
    const VkBufferMemoryBarrier partial_compute_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output_buffer,
        .offset = 16u,
        .size = 24u,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u,
                         0u, NULL, 1u, &partial_compute_barrier, 0u, NULL);
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0u, sizeof(push_addend), &push_addend);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                            0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDispatch(command, 1, 1, 1);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_dispatch_count(command) == 1u);
    assert(vkResetCommandBuffer(command, 0) == VK_SUCCESS);
    vkUpdateDescriptorSets(device, 0, NULL, 1, &descriptor_copy);

    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u, 0u, NULL, 4u, native_graphics_barriers,
                         1u, &native_texture_barrier);
    VkBufferMemoryBarrier descriptor_use_transition =
        native_graphics_barriers[3];
    descriptor_use_transition.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    descriptor_use_transition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 1u,
                         &descriptor_use_transition, 0u, NULL);
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
        /* Preserve the visible portion of a signed rectangle. */
        {{-64, 0}, {192, 256}},
        /* The right edge extends past the 256-wide framebuffer. Vulkan clips
         * this effectively unbounded guest scissor to the attachment. */
        {{128, 0}, {INT32_MAX, 256}},
    };
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      dynamic_viewport_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_layout, 1, 1, &hull_output_set, 0, NULL);
    vkCmdSetViewport(command, 0, 2, dynamic_viewports);
    vkCmdSetScissor(command, 0, 2, dynamic_scissors);
    vkCmdDraw(command, 3, 1, 0, 0);
    assert(vk_ps5_command_buffer_record_error(command) == VK_SUCCESS);
    const VkRect2D empty_dynamic_scissors[] = {
        {{-10, 0}, {5, 16}},
        {{300, 0}, {16, 16}},
    };
    vkCmdSetScissor(command, 0, 2, empty_dynamic_scissors);
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
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue.color = {{0.25f, 0.5f, 0.75f, 1.0f}},
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
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.depthStencil = {0.625f, 0x3cu},
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
    assert(vk_ps5_command_buffer_record_error(dynamic_rendering_command) ==
        VK_SUCCESS);
    vkCmdBindPipeline(dynamic_rendering_command,
        VK_PIPELINE_BIND_POINT_GRAPHICS, dynamic_rendering_pipeline);
    assert(vk_ps5_command_buffer_record_error(dynamic_rendering_command) ==
        VK_SUCCESS);
    vkCmdBindPipeline(dynamic_rendering_command,
        VK_PIPELINE_BIND_POINT_GRAPHICS, color_only_dynamic_pipeline);
    assert(vk_ps5_command_buffer_record_error(dynamic_rendering_command) ==
        VK_SUCCESS);
    vkCmdSetDepthBias(dynamic_rendering_command, 4.0f, 0.5f, -2.0f);
    assert(vk_ps5_command_buffer_record_error(dynamic_rendering_command) ==
        VK_SUCCESS);
    vkCmdEndRendering(dynamic_rendering_command);
    assert(vkEndCommandBuffer(dynamic_rendering_command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(
        dynamic_rendering_command) == 2u);

    VkCommandBuffer depth_only_rendering_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &depth_only_rendering_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(depth_only_rendering_command, &begin_info) ==
        VK_SUCCESS);
    const VkRenderingInfo depth_only_rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{16, 24}, {128, 96}},
        .layerCount = 1,
        .pDepthAttachment = &dynamic_depth_stencil,
        .pStencilAttachment = &dynamic_depth_stencil,
    };
    vkCmdBeginRendering(depth_only_rendering_command, &depth_only_rendering);
    assert(vk_ps5_command_buffer_record_error(
        depth_only_rendering_command) == VK_SUCCESS);
    vkCmdEndRendering(depth_only_rendering_command);
    assert(vkEndCommandBuffer(depth_only_rendering_command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(
        depth_only_rendering_command) == 1u);

    VkCommandBuffer blit_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info, &blit_command) ==
        VK_SUCCESS);
    assert(vkBeginCommandBuffer(blit_command, &begin_info) == VK_SUCCESS);
    const VkImageBlit blit_regions[] = {
        {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .srcOffsets = {{16, 32, 0}, {144, 160, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .dstOffsets = {{32, 48, 0}, {224, 208, 1}},
        },
        {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .srcOffsets = {{192, 32, 0}, {64, 160, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .dstOffsets = {{224, 224, 0}, {96, 96, 1}},
        },
    };
    vkCmdBlitImage(blit_command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, color_image_1,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &blit_regions[0], VK_FILTER_NEAREST);
    vkCmdBlitImage(blit_command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, color_image_1,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &blit_regions[1], VK_FILTER_LINEAR);
    assert(vk_ps5_command_buffer_record_error(blit_command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(blit_command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(blit_command) == 2u);

    VkCommandBuffer self_blit_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &self_blit_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(self_blit_command, &begin_info) ==
        VK_SUCCESS);
    const VkImageBlit self_blit_region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .srcOffsets = {{16, 24, 0}, {144, 152, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 1u, 0u, 1u},
        .dstOffsets = {{8, 12, 0}, {72, 76, 1}},
    };
    vkCmdBlitImage(self_blit_command, self_blit_image,
        VK_IMAGE_LAYOUT_GENERAL, self_blit_image, VK_IMAGE_LAYOUT_GENERAL,
        1u, &self_blit_region, VK_FILTER_LINEAR);
    assert(vk_ps5_command_buffer_record_error(self_blit_command) ==
        VK_SUCCESS);
    assert(vkEndCommandBuffer(self_blit_command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(self_blit_command) == 1u);

    VkCommandBuffer blit_3d_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &blit_3d_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(blit_3d_command, &begin_info) == VK_SUCCESS);
    const VkImageBlit blit_3d_regions[] = {
        {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .srcOffsets = {{0, 0, 0}, {32, 24, 4}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .dstOffsets = {{0, 0, 0}, {16, 12, 2}},
        },
        {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .srcOffsets = {{0, 0, 0}, {32, 24, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .dstOffsets = {{16, 12, 2}, {32, 24, 4}},
        },
        {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .srcOffsets = {{0, 0, 0}, {32, 24, 4}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
            .dstOffsets = {{0, 0, 0}, {32, 24, 1}},
        },
    };
    vkCmdBlitImage(blit_3d_command, image_3d_source,
        VK_IMAGE_LAYOUT_GENERAL, image_3d_destination,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &blit_3d_regions[0], VK_FILTER_LINEAR);
    vkCmdBlitImage(blit_3d_command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, image_3d_destination,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &blit_3d_regions[1], VK_FILTER_NEAREST);
    vkCmdBlitImage(blit_3d_command, image_3d_source,
        VK_IMAGE_LAYOUT_GENERAL, color_image_1,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &blit_3d_regions[2], VK_FILTER_LINEAR);
    assert(vk_ps5_command_buffer_record_error(blit_3d_command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(blit_3d_command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(blit_3d_command) == 5u);

    VkCommandBuffer resolve_command;
    assert(vkAllocateCommandBuffers(device, &allocate_info,
        &resolve_command) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(resolve_command, &begin_info) == VK_SUCCESS);
    const VkImageResolve resolve_region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .srcOffset = {24, 32, 0},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .dstOffset = {48, 64, 0},
        .extent = {128u, 96u, 1u},
    };
    vkCmdResolveImage(resolve_command, resolve_source_image,
        VK_IMAGE_LAYOUT_GENERAL, color_image_1, VK_IMAGE_LAYOUT_GENERAL,
        1u, &resolve_region);
    assert(vk_ps5_command_buffer_record_error(resolve_command) == VK_SUCCESS);
    assert(vkEndCommandBuffer(resolve_command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_draw_count(resolve_command) == 1u);

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
    assert(vkEndCommandBuffer(invalid_copy_command) == VK_SUCCESS);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdClearColorImage(invalid_copy_command, color_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &clear_color, 1u,
        &color_range);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    VkImageBlit crossed_self_blits[] = {
        self_blit_region,
        self_blit_region,
    };
    crossed_self_blits[1].srcSubresource.mipLevel = 1u;
    crossed_self_blits[1].srcOffsets[0] = (VkOffset3D){8, 12, 0};
    crossed_self_blits[1].srcOffsets[1] = (VkOffset3D){72, 76, 1};
    crossed_self_blits[1].dstSubresource.mipLevel = 0u;
    crossed_self_blits[1].dstOffsets[0] = (VkOffset3D){160, 160, 0};
    crossed_self_blits[1].dstOffsets[1] = (VkOffset3D){224, 224, 1};
    vkCmdBlitImage(invalid_copy_command, self_blit_image,
        VK_IMAGE_LAYOUT_GENERAL, self_blit_image, VK_IMAGE_LAYOUT_GENERAL,
        2u, crossed_self_blits, VK_FILTER_NEAREST);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdClearColorImage(invalid_copy_command, depth_image,
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
    VkImageBlit invalid_3d_blit = blit_3d_regions[0];
    invalid_3d_blit.srcOffsets[1].z = 5;
    vkCmdBlitImage(invalid_copy_command, image_3d_source,
        VK_IMAGE_LAYOUT_GENERAL, image_3d_destination,
        VK_IMAGE_LAYOUT_GENERAL, 1u, &invalid_3d_blit, VK_FILTER_NEAREST);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    VkImageBlit feedback_blit = self_blit_region;
    feedback_blit.dstSubresource.mipLevel = 0u;
    feedback_blit.dstOffsets[0] = (VkOffset3D){160, 160, 0};
    feedback_blit.dstOffsets[1] = (VkOffset3D){224, 224, 1};
    vkCmdBlitImage(invalid_copy_command, self_blit_image,
        VK_IMAGE_LAYOUT_GENERAL, self_blit_image, VK_IMAGE_LAYOUT_GENERAL,
        1u, &feedback_blit, VK_FILTER_NEAREST);
    assert(strcmp(vk_ps5_command_buffer_debug_last_command(
                      invalid_copy_command),
                  "vkCmdBlitImage") == 0);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdClearDepthStencilImage(invalid_copy_command, depth_image,
        VK_IMAGE_LAYOUT_GENERAL, NULL, 0u, NULL);
    assert(strcmp(vk_ps5_command_buffer_debug_last_command(
                      invalid_copy_command),
                  "vkCmdClearDepthStencilImage") == 0);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdClearAttachments(invalid_copy_command, 0u, NULL, 0u, NULL);
    assert(strcmp(vk_ps5_command_buffer_debug_last_command(
                      invalid_copy_command),
                  "vkCmdClearAttachments") == 0);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_INITIALIZATION_FAILED);
    assert(vkResetCommandBuffer(invalid_copy_command, 0u) == VK_SUCCESS);
    assert(vkBeginCommandBuffer(invalid_copy_command, &begin_info) ==
           VK_SUCCESS);
    vkCmdResolveImage(invalid_copy_command, color_image,
        VK_IMAGE_LAYOUT_GENERAL, color_image_1, VK_IMAGE_LAYOUT_GENERAL,
        0u, NULL);
    assert(strcmp(vk_ps5_command_buffer_debug_last_command(
                      invalid_copy_command),
                  "vkCmdResolveImage") == 0);
    assert(vkEndCommandBuffer(invalid_copy_command) ==
           VK_ERROR_FEATURE_NOT_PRESENT);

    assert(vk_ps5_command_buffer_native_stream_complete(command));
    assert(vk_ps5_command_buffer_native_draw_count(command) == 12u);


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
    const AgcCommandBufferSubmit *native_submit =
        agcDriverDebugLastDcbSubmit();
    assert(native_submit && native_submit->command_address);
    assert(has_register_value(
        (const uint32_t *)(uintptr_t)native_submit->command_address,
        native_submit->dword_count, AGC_PM4_OP_SET_CONTEXT_REG,
        AGC_REG_CB_BLEND_RED, UINT32_C(0x3e800000)));

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
    assert(setenv("VULKAN_PS5_RECORD_ONLY", "1", 1) == 0);
    assert(vkQueueSubmit(queue, 1u, &submit_info, fence) == VK_SUCCESS);
    assert(unsetenv("VULKAN_PS5_RECORD_ONLY") == 0);
    assert(vkGetFenceStatus(device, fence) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_EXECUTABLE);

    /* Vulkan permits completed resources to be destroyed while executable
       command buffers still reference them. The adapter retains the native
       objects until command recycling releases OpenAGC's references. */
    vkDestroyBuffer(device, index_buffer, NULL);
    vkFreeMemory(device, index_memory, NULL);
    assert(vk_ps5_deferred_native_count(device) > 0u);
    assert(vkResetCommandPool(device, pool, 0u) == VK_SUCCESS);
    assert(vk_ps5_deferred_native_count(device) == 0u);
    assert(vk_ps5_command_buffer_native_state(command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
    assert(vk_ps5_command_buffer_native_state(dynamic_rendering_command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
    assert(vk_ps5_command_buffer_native_state(depth_only_rendering_command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
    assert(vk_ps5_command_buffer_native_state(blit_command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
    assert(vk_ps5_command_buffer_native_state(blit_3d_command) ==
           AGC_COMMAND_BUFFER_STATE_INITIAL);
    assert(vk_ps5_command_buffer_native_state(resolve_command) ==
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
    vkDestroyPipeline(device, unused_constant_pipeline, NULL);
    vkDestroyPipeline(device, zink_core_dynamic_pipeline, NULL);
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    vkDestroyPipeline(device, zero_stride_pipeline, NULL);
    vkDestroyPipeline(device, dynamic_rendering_pipeline, NULL);
    vkDestroyPipeline(device, color_only_dynamic_pipeline, NULL);
    vkDestroyFramebuffer(device, imageless_framebuffer, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyFramebuffer(device, mutable_clear_framebuffer, NULL);
    vkDestroyRenderPass(device, incompatible_flag_render_pass, NULL);
    vkDestroyRenderPass(device, incompatible_reference_render_pass, NULL);
    vkDestroyRenderPass(device, incompatible_format_render_pass, NULL);
    vkDestroyRenderPass(device, compatible_render_pass, NULL);
    vkDestroyRenderPass(device, mutable_clear_render_pass, NULL);
    vkDestroyImageView(device, mutable_clear_view, NULL);
    vkDestroyImage(device, mutable_clear_image, NULL);
    vkFreeMemory(device, mutable_clear_memory, NULL);
    vkDestroyImageView(device, depth_view, NULL);
    vkDestroyImage(device, depth_image, NULL);
    vkFreeMemory(device, depth_memory, NULL);
    vkDestroyImageView(device, color_view_1, NULL);
    vkDestroyImage(device, color_image_1, NULL);
    vkFreeMemory(device, image_memory_1, NULL);
    vkDestroyImage(device, resolve_source_image, NULL);
    vkFreeMemory(device, resolve_source_memory, NULL);
    vkDestroyImage(device, image_3d_destination, NULL);
    vkFreeMemory(device, image_3d_destination_memory, NULL);
    vkDestroyImage(device, image_3d_source, NULL);
    vkFreeMemory(device, image_3d_source_memory, NULL);
    vkDestroyImage(device, self_blit_image, NULL);
    vkFreeMemory(device, self_blit_memory, NULL);
    vkDestroyImageView(device, color_view, NULL);
    vkDestroyImage(device, color_image, NULL);
    vkFreeMemory(device, image_memory, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    vkDestroyDescriptorSetLayout(device, hull_output_set_layout, NULL);
    vkDestroyDescriptorSetLayout(device, texture_set_layout, NULL);
    vkDestroySampler(device, anisotropic_sampler, NULL);
    vkDestroySampler(device, nearest_unnormalized_sampler, NULL);
    vkDestroySampler(device, unnormalized_sampler, NULL);
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
    vkDestroyBuffer(device, indirect_buffer, NULL);
    vkFreeMemory(device, indirect_memory, NULL);
    vkDestroyBuffer(device, vertex_buffer, NULL);
    vkFreeMemory(device, vertex_memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
