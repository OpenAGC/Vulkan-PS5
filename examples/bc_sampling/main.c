#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bc_probe_assets.h"
#include "vulkan_ps5_bc_sampling_spv.h"

#if defined(OPENAGC_PROSPERO)
#include "../system_service_exit.h"
#endif

#if defined(VULKAN_PS5_BC_SAMPLING_SMOKE)
static const uint8_t probe_asset_indices[] = {0u, 4u, 8u, 10u, 12u};
#define BC_FORMAT_COUNT 5u
#define BC_PASS_ORACLE "bc_sampling: PASS formats=5 sampled=5 exact-blocks"
#else
#define BC_FORMAT_COUNT 14u
#define BC_PASS_ORACLE "bc_sampling: PASS formats=14 sampled=14 exact-blocks"
#endif
#define OUTPUT_WORD_COUNT (BC_FORMAT_COUNT * 4u)
#define BC_FEATURES (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | \
    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | \
    VK_FORMAT_FEATURE_TRANSFER_DST_BIT | \
    VK_FORMAT_FEATURE_BLIT_SRC_BIT)

typedef struct SampleImage {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    void *mapped;
} SampleImage;

static const BcProbeAsset *probe_asset(uint32_t probe_index)
{
#if defined(VULKAN_PS5_BC_SAMPLING_SMOKE)
    return &bc_probe_assets[probe_asset_indices[probe_index]];
#else
    return &bc_probe_assets[probe_index];
#endif
}

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t index = 0u; index < properties.memoryTypeCount; ++index) {
        const VkMemoryPropertyFlags flags =
            properties.memoryTypes[index].propertyFlags;
        if ((compatible_types & (1u << index)) != 0u &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return index;
    }
    return UINT32_MAX;
}

#if defined(OPENAGC_PROSPERO)
static float float_from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float absolute_float(float value)
{
    return value < 0.0f ? -value : value;
}

static int result_matches(uint32_t format_index, const uint32_t actual[4])
{
    const BcProbeAsset *asset = probe_asset(format_index);
    const int srgb = asset->vk_format == 134u ||
        asset->vk_format == 136u || asset->vk_format == 138u ||
        asset->vk_format == 146u;
    const float tolerance = asset->vk_format == 143u ||
        asset->vk_format == 144u ? (1.0f / 256.0f) :
        srgb ? (1.0f / 1024.0f) : (1.0f / 2048.0f);
    for (uint32_t component = 0u; component < 4u; ++component) {
        const float observed = float_from_bits(actual[component]);
        const float expected =
            float_from_bits(asset->expected_float_bits[component]);
        if (absolute_float(observed - expected) > tolerance)
            return 0;
    }
    return 1;
}
#endif

static int run_probe(void)
{
    int status = 1;
    VkResult result = VK_SUCCESS;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkBuffer output_buffer = VK_NULL_HANDLE;
    VkDeviceMemory output_memory = VK_NULL_HANDLE;
    void *output_mapping = NULL;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_sets[BC_FORMAT_COUNT] = {0};
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    SampleImage images[BC_FORMAT_COUNT] = {0};

#define VK_TRY(expression) do { \
    result = (expression); \
    if (result != VK_SUCCESS) { \
        printf("bc_sampling: %s failed (%d)\n", #expression, result); \
        goto cleanup; \
    } \
} while (0)

    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_TRY(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t physical_count = 1u;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VK_TRY(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u) {
        puts("bc_sampling: expected one physical device");
        goto cleanup;
    }
    const float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0u,
        .queueCount = 1u,
        .pQueuePriorities = &queue_priority,
    };
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &queue_info,
    };
    VK_TRY(vkCreateDevice(physical, &device_info, NULL, &device));
    vkGetDeviceQueue(device, 0u, 0u, &queue);

    VkMappedMemoryRange image_ranges[BC_FORMAT_COUNT];
    VkImageMemoryBarrier image_barriers[BC_FORMAT_COUNT];
    for (uint32_t index = 0u; index < BC_FORMAT_COUNT; ++index) {
        const BcProbeAsset *asset = probe_asset(index);
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(
            physical, (VkFormat)asset->vk_format, &properties);
        if (properties.linearTilingFeatures != BC_FEATURES ||
            properties.optimalTilingFeatures != BC_FEATURES ||
            properties.bufferFeatures != 0u) {
            printf("bc_sampling: feature mismatch format=%s linear=%08x "
                "optimal=%08x buffer=%08x\n", asset->name,
                properties.linearTilingFeatures,
                properties.optimalTilingFeatures, properties.bufferFeatures);
            goto cleanup;
        }
        const VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = (VkFormat)asset->vk_format,
            .extent = {4u, 4u, 1u},
            .mipLevels = 1u,
            .arrayLayers = 1u,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        };
        VK_TRY(vkCreateImage(device, &image_info, NULL,
            &images[index].image));
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, images[index].image,
            &requirements);
        const uint32_t memory_type = find_host_visible_memory_type(
            physical, requirements.memoryTypeBits);
        if (memory_type == UINT32_MAX) {
            printf("bc_sampling: no host-visible memory for %s\n",
                asset->name);
            goto cleanup;
        }
        const VkMemoryAllocateInfo allocation_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type,
        };
        VK_TRY(vkAllocateMemory(device, &allocation_info, NULL,
            &images[index].memory));
        VK_TRY(vkBindImageMemory(device, images[index].image,
            images[index].memory, 0u));
        VK_TRY(vkMapMemory(device, images[index].memory, 0u,
            requirements.size, 0u, &images[index].mapped));
        const VkImageSubresource subresource = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u,
        };
        VkSubresourceLayout layout;
        vkGetImageSubresourceLayout(device, images[index].image,
            &subresource, &layout);
        if (layout.size < asset->block_size ||
            layout.offset > requirements.size ||
            asset->block_size > requirements.size - layout.offset) {
            printf("bc_sampling: invalid layout for %s\n", asset->name);
            goto cleanup;
        }
        memset(images[index].mapped, 0, requirements.size);
        memcpy((uint8_t *)images[index].mapped + layout.offset,
            asset->block, asset->block_size);
        image_ranges[index] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = images[index].memory,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        };
        const VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[index].image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = (VkFormat)asset->vk_format,
            .components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
            },
        };
        VK_TRY(vkCreateImageView(device, &view_info, NULL,
            &images[index].view));
        image_barriers[index] = (VkImageMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[index].image,
            .subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
            },
        };
    }
    VK_TRY(vkFlushMappedMemoryRanges(device, BC_FORMAT_COUNT, image_ranges));

    const VkDeviceSize output_size = OUTPUT_WORD_COUNT * sizeof(uint32_t);
    const VkBufferCreateInfo output_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = output_size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_TRY(vkCreateBuffer(device, &output_info, NULL, &output_buffer));
    VkMemoryRequirements output_requirements;
    vkGetBufferMemoryRequirements(device, output_buffer, &output_requirements);
    const uint32_t output_memory_type = find_host_visible_memory_type(
        physical, output_requirements.memoryTypeBits);
    if (output_memory_type == UINT32_MAX) {
        puts("bc_sampling: no host-visible output memory");
        goto cleanup;
    }
    const VkMemoryAllocateInfo output_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = output_requirements.size,
        .memoryTypeIndex = output_memory_type,
    };
    VK_TRY(vkAllocateMemory(device, &output_allocation, NULL, &output_memory));
    VK_TRY(vkBindBufferMemory(device, output_buffer, output_memory, 0u));
    VK_TRY(vkMapMemory(device, output_memory, 0u, output_requirements.size,
        0u, &output_mapping));
    memset(output_mapping, 0xcd, output_requirements.size);
    const VkMappedMemoryRange output_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = output_memory,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    VK_TRY(vkFlushMappedMemoryRanges(device, 1u, &output_range));

    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
    };
    VK_TRY(vkCreateSampler(device, &sampler_info, NULL, &sampler));
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0u,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };
    const VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2u,
        .pBindings = bindings,
    };
    VK_TRY(vkCreateDescriptorSetLayout(device, &set_layout_info, NULL,
        &set_layout));
    const VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0u,
        .size = sizeof(uint32_t),
    };
    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1u,
        .pPushConstantRanges = &push_range,
    };
    VK_TRY(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
        &pipeline_layout));
    const VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_bc_sampling_spv),
        .pCode = vulkan_ps5_bc_sampling_spv,
    };
    VK_TRY(vkCreateShaderModule(device, &shader_info, NULL, &shader));
    const VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shader,
            .pName = "main",
        },
        .layout = pipeline_layout,
    };
    VK_TRY(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u,
        &pipeline_info, NULL, &pipeline));
    const VkDescriptorPoolSize pool_sizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BC_FORMAT_COUNT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BC_FORMAT_COUNT},
    };
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = BC_FORMAT_COUNT,
        .poolSizeCount = 2u,
        .pPoolSizes = pool_sizes,
    };
    VK_TRY(vkCreateDescriptorPool(device, &pool_info, NULL,
        &descriptor_pool));
    VkDescriptorSetLayout layouts[BC_FORMAT_COUNT];
    for (uint32_t index = 0u; index < BC_FORMAT_COUNT; ++index)
        layouts[index] = set_layout;
    const VkDescriptorSetAllocateInfo descriptor_allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = BC_FORMAT_COUNT,
        .pSetLayouts = layouts,
    };
    VK_TRY(vkAllocateDescriptorSets(device, &descriptor_allocate,
        descriptor_sets));
    const VkDescriptorBufferInfo output_descriptor = {
        .buffer = output_buffer,
        .offset = 0u,
        .range = output_size,
    };
    for (uint32_t index = 0u; index < BC_FORMAT_COUNT; ++index) {
        const VkDescriptorImageInfo image_descriptor = {
            .sampler = sampler,
            .imageView = images[index].view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet writes[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_sets[index],
                .dstBinding = 0u,
                .descriptorCount = 1u,
                .descriptorType =
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &image_descriptor,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_sets[index],
                .dstBinding = 1u,
                .descriptorCount = 1u,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &output_descriptor,
            },
        };
        vkUpdateDescriptorSets(device, 2u, writes, 0u, NULL);
    }

    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VK_TRY(vkCreateCommandPool(device, &command_pool_info, NULL,
        &command_pool));
    const VkCommandBufferAllocateInfo command_allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command = VK_NULL_HANDLE;
    VK_TRY(vkAllocateCommandBuffers(device, &command_allocate, &command));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_TRY(vkBeginCommandBuffer(command, &begin_info));
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, NULL, 0u, NULL,
        BC_FORMAT_COUNT, image_barriers);
    const VkBufferMemoryBarrier output_to_compute = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output_buffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, NULL, 1u,
        &output_to_compute, 0u, NULL);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    for (uint32_t index = 0u; index < BC_FORMAT_COUNT; ++index) {
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_layout, 0u, 1u, &descriptor_sets[index], 0u, NULL);
        vkCmdPushConstants(command, pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(index), &index);
        vkCmdDispatch(command, 1u, 1u, 1u);
    }
    const VkBufferMemoryBarrier output_to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output_buffer,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 1u, &output_to_host,
        0u, NULL);
    VK_TRY(vkEndCommandBuffer(command));
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_TRY(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1u,
        .pCommandBuffers = &command,
    };
    VK_TRY(vkQueueSubmit(queue, 1u, &submit_info, fence));
    result = vkWaitForFences(device, 1u, &fence, VK_TRUE,
        UINT64_C(2000000000));
    if (result != VK_SUCCESS) {
        printf("bc_sampling: two-second fence wait failed (%d)\n", result);
        goto cleanup;
    }
    VK_TRY(vkInvalidateMappedMemoryRanges(device, 1u, &output_range));

#if defined(OPENAGC_PROSPERO)
    const uint32_t *values = output_mapping;
    for (uint32_t index = 0u; index < BC_FORMAT_COUNT; ++index) {
        const uint32_t *actual = values + index * 4u;
        if (!result_matches(index, actual)) {
            printf("bc_sampling: mismatch format=%s "
                "actual=%08x,%08x,%08x,%08x "
                "expected=%08x,%08x,%08x,%08x\n",
                probe_asset(index)->name,
                actual[0], actual[1], actual[2], actual[3],
                probe_asset(index)->expected_float_bits[0],
                probe_asset(index)->expected_float_bits[1],
                probe_asset(index)->expected_float_bits[2],
                probe_asset(index)->expected_float_bits[3]);
            goto cleanup;
        }
    }
    puts(BC_PASS_ORACLE);
#else
    printf("bc_sampling: PASS command recording formats=%u\n",
        BC_FORMAT_COUNT);
#endif
    status = 0;

cleanup:
    if (device != VK_NULL_HANDLE) {
        if (fence != VK_NULL_HANDLE)
            vkDestroyFence(device, fence, NULL);
        if (command_pool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, command_pool, NULL);
        if (descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, NULL);
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline, NULL);
        if (shader != VK_NULL_HANDLE)
            vkDestroyShaderModule(device, shader, NULL);
        if (pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, NULL);
        if (set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, set_layout, NULL);
        if (sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, sampler, NULL);
        if (output_mapping != NULL)
            vkUnmapMemory(device, output_memory);
        if (output_buffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, output_buffer, NULL);
        if (output_memory != VK_NULL_HANDLE)
            vkFreeMemory(device, output_memory, NULL);
        for (uint32_t index = 0u; index < BC_FORMAT_COUNT; ++index) {
            if (images[index].view != VK_NULL_HANDLE)
                vkDestroyImageView(device, images[index].view, NULL);
            if (images[index].mapped != NULL)
                vkUnmapMemory(device, images[index].memory);
            if (images[index].image != VK_NULL_HANDLE)
                vkDestroyImage(device, images[index].image, NULL);
            if (images[index].memory != VK_NULL_HANDLE)
                vkFreeMemory(device, images[index].memory, NULL);
        }
        vkDestroyDevice(device, NULL);
    }
    if (instance != VK_NULL_HANDLE)
        vkDestroyInstance(instance, NULL);
    return status;

#undef VK_TRY
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    puts("bc_sampling: stage=start");
    const int status = run_probe();
    printf("bc_sampling: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("bc_sampling");
#endif
    return status;
}
