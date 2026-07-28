#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdint.h>

static uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t bits)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0u; i < properties.memoryTypeCount; ++i)
        if (bits & (1u << i)) return i;
    assert(0 && "memory type not found");
    return 0u;
}

int main(void)
{
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);
    uint32_t physical_count = 1u;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, &physical) ==
           VK_SUCCESS);

    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(physical, &supported);
    assert(supported.imageCubeArray == VK_TRUE);
    VkImageFormatProperties format_properties;
    assert(vkGetPhysicalDeviceImageFormatProperties(physical,
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_LINEAR,
        VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        &format_properties) == VK_SUCCESS);
    assert(format_properties.maxArrayLayers >= 12u);
    assert(format_properties.sampleCounts == VK_SAMPLE_COUNT_1_BIT);
    assert(vkGetPhysicalDeviceImageFormatProperties(physical,
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_3D, VK_IMAGE_TILING_LINEAR,
        VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        &format_properties) == VK_ERROR_FORMAT_NOT_SUPPORTED);

    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1u,
        .pQueuePriorities = &priority,
    };
    const VkPhysicalDeviceFeatures enabled = {
        .imageCubeArray = VK_TRUE,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &queue_info,
        .pEnabledFeatures = &enabled,
    };
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {4u, 4u, 1u},
        .mipLevels = 1u,
        .arrayLayers = 12u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkImage image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &image_info, NULL, &image) == VK_SUCCESS);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    const VkMemoryAllocateInfo allocation_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = find_memory_type(physical,
                                             requirements.memoryTypeBits),
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &allocation_info, NULL, &memory) ==
           VK_SUCCESS);
    assert(vkBindImageMemory(device, image, memory, 0u) == VK_SUCCESS);
    const VkImageSubresource layer_zero = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u};
    const VkImageSubresource layer_six = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 6u};
    VkSubresourceLayout layout_zero, layout_six;
    vkGetImageSubresourceLayout(device, image, &layer_zero, &layout_zero);
    vkGetImageSubresourceLayout(device, image, &layer_six, &layout_six);
    assert(layout_zero.offset == 0u);
    assert(layout_six.offset == 6u * layout_zero.arrayPitch);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
        .format = image_info.format,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 12u,
        },
    };
    VkImageView cube_array_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &view_info, NULL, &cube_array_view) ==
           VK_SUCCESS);
    view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view_info.subresourceRange.baseArrayLayer = 6u;
    view_info.subresourceRange.layerCount = 6u;
    VkImageView cube_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &view_info, NULL, &cube_view) ==
           VK_SUCCESS);
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    view_info.subresourceRange.baseArrayLayer = 2u;
    view_info.subresourceRange.layerCount = 4u;
    VkImageView array_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &view_info, NULL, &array_view) ==
           VK_SUCCESS);
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.subresourceRange.baseArrayLayer = 11u;
    view_info.subresourceRange.layerCount = 1u;
    VkImageView layer_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &view_info, NULL, &layer_view) ==
           VK_SUCCESS);
    view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    view_info.subresourceRange.baseArrayLayer = 0u;
    view_info.subresourceRange.layerCount = 7u;
    VkImageView invalid_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &view_info, NULL, &invalid_view) ==
           VK_ERROR_FEATURE_NOT_PRESENT);
    assert(invalid_view == VK_NULL_HANDLE);

    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    assert(vkCreateSampler(device, &sampler_info, NULL, &sampler) == VK_SUCCESS);
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0u,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1u,
        .pBindings = &binding,
    };
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    assert(vkCreateDescriptorSetLayout(device, &set_layout_info, NULL,
                                       &set_layout) == VK_SUCCESS);
    const VkDescriptorPoolSize pool_size = {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u,
    };
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1u,
        .poolSizeCount = 1u,
        .pPoolSizes = &pool_size,
    };
    VkDescriptorPool pool = VK_NULL_HANDLE;
    assert(vkCreateDescriptorPool(device, &pool_info, NULL, &pool) ==
           VK_SUCCESS);
    const VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = 1u,
        .pSetLayouts = &set_layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    assert(vkAllocateDescriptorSets(device, &set_info, &set) == VK_SUCCESS);
    const VkDescriptorImageInfo descriptor = {
        sampler, cube_array_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .descriptorCount = 1u,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptor,
    };
    vkUpdateDescriptorSets(device, 1u, &write, 0u, NULL);

    vkDestroyDescriptorPool(device, pool, NULL);
    vkDestroyDescriptorSetLayout(device, set_layout, NULL);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyImageView(device, layer_view, NULL);
    vkDestroyImageView(device, array_view, NULL);
    vkDestroyImageView(device, cube_view, NULL);
    vkDestroyImageView(device, cube_array_view, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyImage(device, image, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
