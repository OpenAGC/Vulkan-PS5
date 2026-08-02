#include <vulkan/vulkan.h>

#include "../src/vulkan_ps5_internal.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static VkDescriptorSetLayout create_layout(
    VkDevice device, const VkDescriptorSetLayoutBinding *bindings,
    uint32_t binding_count)
{
    const VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = binding_count,
        .pBindings = bindings,
    };
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    assert(vkCreateDescriptorSetLayout(device, &info, NULL, &layout) ==
           VK_SUCCESS);
    return layout;
}

static VkResult create_range_template(
    VkDevice device, VkDescriptorSetLayout layout, VkDescriptorType type,
    uint32_t dst_binding, uint32_t dst_array_element,
    uint32_t descriptor_count,
    VkDescriptorUpdateTemplate *update_template)
{
    const VkDescriptorUpdateTemplateEntry entry = {
        .dstBinding = dst_binding,
        .dstArrayElement = dst_array_element,
        .descriptorCount = descriptor_count,
        .descriptorType = type,
        .offset = 0u,
        .stride = type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ?
            sizeof(VkDescriptorBufferInfo) : sizeof(VkDescriptorImageInfo),
    };
    const VkDescriptorUpdateTemplateCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = 1u,
        .pDescriptorUpdateEntries = &entry,
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        .descriptorSetLayout = layout,
    };
    return vkCreateDescriptorUpdateTemplate(
        device, &info, NULL, update_template);
}

static VkResult create_spanning_template(
    VkDevice device, VkDescriptorSetLayout layout, VkDescriptorType type,
    VkDescriptorUpdateTemplate *update_template)
{
    return create_range_template(device, layout, type, 0u, 0u, 2u,
                                 update_template);
}

static void assert_buffer_descriptor(VkDescriptorSet set, uint32_t binding,
                                     uint32_t array_element,
                                     const VkDescriptorBufferInfo *expected)
{
    VkDescriptorBufferInfo actual;
    assert(vk_ps5_descriptor_set_buffer_info(
        set, binding, array_element, &actual));
    assert(actual.buffer == expected->buffer);
    assert(actual.offset == expected->offset);
    assert(actual.range == expected->range);
}

int main(void)
{
    const VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "descriptor-updates",
        .apiVersion = VK_API_VERSION_1_2,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application_info,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);

    uint32_t physical_count = 1u;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(
        instance, &physical_count, &physical) == VK_SUCCESS);
    assert(physical_count == 1u && physical != VK_NULL_HANDLE);

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
    };
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) ==
           VK_SUCCESS);

    VkBuffer buffers[4] = {VK_NULL_HANDLE};
    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 64u,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    for (uint32_t i = 0u; i < 4u; ++i)
        assert(vkCreateBuffer(device, &buffer_info, NULL, &buffers[i]) ==
               VK_SUCCESS);

    const VkDescriptorSetLayoutBinding valid_bindings[] = {
        {
            .binding = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 0u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    VkDescriptorSetLayout valid_layout = create_layout(
        device, valid_bindings, 2u);
    const VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 6u,
    };
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 3u,
        .poolSizeCount = 1u,
        .pPoolSizes = &pool_size,
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    assert(vkCreateDescriptorPool(device, &pool_info, NULL, &pool) ==
           VK_SUCCESS);
    const VkDescriptorSetLayout set_layouts[] = {
        valid_layout, valid_layout, valid_layout,
    };
    const VkDescriptorSetAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = 3u,
        .pSetLayouts = set_layouts,
    };
    VkDescriptorSet sets[3] = {VK_NULL_HANDLE};
    assert(vkAllocateDescriptorSets(device, &allocate_info, sets) ==
           VK_SUCCESS);

    const VkDescriptorBufferInfo template_values[] = {
        {buffers[0], 4u, 16u},
        {buffers[1], 8u, 24u},
    };
    VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
    assert(create_spanning_template(device, valid_layout,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    &update_template) == VK_SUCCESS);
    vkUpdateDescriptorSetWithTemplate(
        device, sets[0], update_template, template_values);
    assert_buffer_descriptor(sets[0], 0u, 0u, &template_values[0]);
    assert_buffer_descriptor(sets[0], 1u, 0u, &template_values[1]);

    const VkDescriptorBufferInfo write_values[] = {
        {buffers[2], 12u, 28u},
        {buffers[3], 16u, 32u},
    };
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = sets[1],
        .dstBinding = 0u,
        .dstArrayElement = 0u,
        .descriptorCount = 2u,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = write_values,
    };
    vkUpdateDescriptorSets(device, 1u, &write, 0u, NULL);
    assert_buffer_descriptor(sets[1], 0u, 0u, &write_values[0]);
    assert_buffer_descriptor(sets[1], 1u, 0u, &write_values[1]);

    const VkCopyDescriptorSet copy = {
        .sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
        .srcSet = sets[0],
        .srcBinding = 0u,
        .srcArrayElement = 0u,
        .dstSet = sets[2],
        .dstBinding = 0u,
        .dstArrayElement = 0u,
        .descriptorCount = 2u,
    };
    vkUpdateDescriptorSets(device, 0u, NULL, 1u, &copy);
    assert_buffer_descriptor(sets[2], 0u, 0u, &template_values[0]);
    assert_buffer_descriptor(sets[2], 1u, 0u, &template_values[1]);

    const VkDescriptorUpdateTemplateEntry unaligned_entry = {
        .dstBinding = 0u,
        .dstArrayElement = 0u,
        .descriptorCount = 2u,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .offset = 1u,
        .stride = sizeof(VkDescriptorBufferInfo) + 1u,
    };
    const VkDescriptorUpdateTemplateCreateInfo unaligned_template_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .descriptorUpdateEntryCount = 1u,
        .pDescriptorUpdateEntries = &unaligned_entry,
        .templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        .descriptorSetLayout = valid_layout,
    };
    VkDescriptorUpdateTemplate unaligned_template = VK_NULL_HANDLE;
    assert(vkCreateDescriptorUpdateTemplate(
        device, &unaligned_template_info, NULL, &unaligned_template) ==
        VK_SUCCESS);
    uint8_t unaligned_data[2u + 2u * sizeof(VkDescriptorBufferInfo)] = {0u};
    memcpy(unaligned_data + unaligned_entry.offset, &write_values[0],
           sizeof(write_values[0]));
    memcpy(unaligned_data + unaligned_entry.offset + unaligned_entry.stride,
           &write_values[1], sizeof(write_values[1]));
    vkUpdateDescriptorSetWithTemplate(
        device, sets[0], unaligned_template, unaligned_data);
    assert_buffer_descriptor(sets[0], 0u, 0u, &write_values[0]);
    assert_buffer_descriptor(sets[0], 1u, 0u, &write_values[1]);

    const VkDescriptorSetLayoutBinding skipped_bindings[] = {
        valid_bindings[1],
        {
            .binding = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 0u,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    VkDescriptorSetLayout skipped_layout = create_layout(
        device, skipped_bindings, 3u);
    VkDescriptorUpdateTemplate skipped_template = VK_NULL_HANDLE;
    assert(create_spanning_template(device, skipped_layout,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    &skipped_template) == VK_SUCCESS);

    VkDescriptorSetLayoutBinding sparse_bindings[] = {
        valid_bindings[1], valid_bindings[0],
    };
    sparse_bindings[1].binding = 2u;
    VkDescriptorSetLayout sparse_layout = create_layout(
        device, sparse_bindings, 2u);
    VkDescriptorUpdateTemplate sparse_template = VK_NULL_HANDLE;
    assert(create_spanning_template(device, sparse_layout,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    &sparse_template) == VK_SUCCESS);

    const VkDescriptorSetLayoutBinding offset_bindings[] = {
        {
            .binding = 2u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 2u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 0u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 2u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    VkDescriptorSetLayout offset_layout = create_layout(
        device, offset_bindings, 2u);
    VkDescriptorUpdateTemplate offset_template = VK_NULL_HANDLE;
    assert(create_range_template(device, offset_layout,
                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 0u, 3u, 1u, &offset_template) == VK_SUCCESS);

    VkDescriptorUpdateTemplate invalid_template = VK_NULL_HANDLE;
    assert(create_range_template(device, offset_layout,
                                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 0u, 5u, 1u, &invalid_template) ==
           VK_ERROR_INITIALIZATION_FAILED);

    const VkDescriptorPoolSize secondary_pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 16u,
    };
    const VkDescriptorPoolCreateInfo secondary_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 5u,
        .poolSizeCount = 1u,
        .pPoolSizes = &secondary_pool_size,
    };
    VkDescriptorPool secondary_pool = VK_NULL_HANDLE;
    assert(vkCreateDescriptorPool(device, &secondary_pool_info, NULL,
                                  &secondary_pool) == VK_SUCCESS);
    const VkDescriptorSetLayout secondary_layouts[] = {
        skipped_layout, sparse_layout, offset_layout, offset_layout,
        offset_layout,
    };
    const VkDescriptorSetAllocateInfo secondary_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = secondary_pool,
        .descriptorSetCount = 5u,
        .pSetLayouts = secondary_layouts,
    };
    VkDescriptorSet secondary_sets[5] = {VK_NULL_HANDLE};
    assert(vkAllocateDescriptorSets(device, &secondary_allocate_info,
                                    secondary_sets) == VK_SUCCESS);

    vkUpdateDescriptorSetWithTemplate(
        device, secondary_sets[0], skipped_template, template_values);
    assert_buffer_descriptor(
        secondary_sets[0], 0u, 0u, &template_values[0]);
    assert_buffer_descriptor(
        secondary_sets[0], 2u, 0u, &template_values[1]);

    vkUpdateDescriptorSetWithTemplate(
        device, secondary_sets[1], sparse_template, template_values);
    assert_buffer_descriptor(
        secondary_sets[1], 0u, 0u, &template_values[0]);
    assert_buffer_descriptor(
        secondary_sets[1], 2u, 0u, &template_values[1]);

    vkUpdateDescriptorSetWithTemplate(
        device, secondary_sets[2], offset_template, &write_values[0]);
    assert_buffer_descriptor(
        secondary_sets[2], 2u, 1u, &write_values[0]);

    const VkDescriptorBufferInfo offset_write_values[] = {
        {buffers[0], 20u, 20u},
        {buffers[1], 24u, 20u},
        {buffers[2], 28u, 20u},
    };
    const VkWriteDescriptorSet offset_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = secondary_sets[3],
        .dstBinding = 0u,
        .dstArrayElement = 1u,
        .descriptorCount = 3u,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = offset_write_values,
    };
    vkUpdateDescriptorSets(device, 1u, &offset_write, 0u, NULL);
    assert_buffer_descriptor(
        secondary_sets[3], 0u, 1u, &offset_write_values[0]);
    assert_buffer_descriptor(
        secondary_sets[3], 2u, 0u, &offset_write_values[1]);
    assert_buffer_descriptor(
        secondary_sets[3], 2u, 1u, &offset_write_values[2]);

    const VkCopyDescriptorSet offset_copy = {
        .sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
        .srcSet = secondary_sets[3],
        .srcBinding = 0u,
        .srcArrayElement = 1u,
        .dstSet = secondary_sets[4],
        .dstBinding = 0u,
        .dstArrayElement = 1u,
        .descriptorCount = 3u,
    };
    vkUpdateDescriptorSets(device, 0u, NULL, 1u, &offset_copy);
    assert_buffer_descriptor(
        secondary_sets[4], 0u, 1u, &offset_write_values[0]);
    assert_buffer_descriptor(
        secondary_sets[4], 2u, 0u, &offset_write_values[1]);
    assert_buffer_descriptor(
        secondary_sets[4], 2u, 1u, &offset_write_values[2]);

    VkDescriptorSetLayoutBinding invalid_bindings[] = {
        valid_bindings[1], valid_bindings[0],
    };

    invalid_bindings[1] = valid_bindings[0];
    invalid_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkDescriptorSetLayout invalid_type_layout = create_layout(
        device, invalid_bindings, 2u);
    assert(create_spanning_template(device, invalid_type_layout,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    &invalid_template) ==
           VK_ERROR_INITIALIZATION_FAILED);

    invalid_bindings[1] = valid_bindings[0];
    invalid_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayout invalid_stage_layout = create_layout(
        device, invalid_bindings, 2u);
    assert(create_spanning_template(device, invalid_stage_layout,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    &invalid_template) ==
           VK_ERROR_INITIALIZATION_FAILED);

    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    assert(vkCreateSampler(device, &sampler_info, NULL, &sampler) ==
           VK_SUCCESS);
    const VkDescriptorSetLayoutBinding immutable_bindings[] = {
        {
            .binding = 0u,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = &sampler,
        },
        {
            .binding = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    VkDescriptorSetLayout invalid_immutable_layout = create_layout(
        device, immutable_bindings, 2u);
    assert(create_spanning_template(device, invalid_immutable_layout,
                                    VK_DESCRIPTOR_TYPE_SAMPLER,
                                    &invalid_template) ==
           VK_ERROR_INITIALIZATION_FAILED);

    vkDestroyDescriptorUpdateTemplate(device, skipped_template, NULL);
    vkDestroyDescriptorUpdateTemplate(device, unaligned_template, NULL);
    vkDestroyDescriptorSetLayout(device, invalid_immutable_layout, NULL);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyDescriptorSetLayout(device, invalid_stage_layout, NULL);
    vkDestroyDescriptorSetLayout(device, invalid_type_layout, NULL);
    vkDestroyDescriptorPool(device, secondary_pool, NULL);
    vkDestroyDescriptorUpdateTemplate(device, offset_template, NULL);
    vkDestroyDescriptorSetLayout(device, offset_layout, NULL);
    vkDestroyDescriptorUpdateTemplate(device, sparse_template, NULL);
    vkDestroyDescriptorSetLayout(device, sparse_layout, NULL);
    vkDestroyDescriptorSetLayout(device, skipped_layout, NULL);
    vkDestroyDescriptorUpdateTemplate(device, update_template, NULL);
    vkDestroyDescriptorPool(device, pool, NULL);
    vkDestroyDescriptorSetLayout(device, valid_layout, NULL);
    for (uint32_t i = 0u; i < 4u; ++i)
        vkDestroyBuffer(device, buffers[i], NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
