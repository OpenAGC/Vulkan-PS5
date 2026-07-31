#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(VULKAN_PS5_VARIABLE_POINTERS_PROBE)
#include "vulkan_ps5_variable_pointers_spv.h"
#define vulkan_ps5_compute_spv vulkan_ps5_variable_pointers_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "variable_pointers"
#define VALUE_COUNT 1024u
#define GROUP_COUNT 1u
#elif defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
#include "vulkan_ps5_robust_buffer_spv.h"
#define vulkan_ps5_compute_spv vulkan_ps5_robust_buffer_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "robust_buffer_access"
#define VALUE_COUNT 16u
#define GROUP_COUNT 1u
#elif defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
#include "vulkan_ps5_storage_image_spv.h"
#define vulkan_ps5_compute_spv vulkan_ps5_storage_image_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "storage_image_write_without_format"
#define IMAGE_DIMENSION 64u
#define VALUE_COUNT (IMAGE_DIMENSION * IMAGE_DIMENSION)
#define GROUP_COUNT (IMAGE_DIMENSION / 8u)
#elif defined(VULKAN_PS5_SCALAR_BLOCK_LAYOUT_PROBE)
#include "vulkan_ps5_scalar_block_layout_spv.h"
#define vulkan_ps5_compute_spv vulkan_ps5_scalar_block_layout_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "scalar_block_layout"
#define VALUE_COUNT 16u
#define GROUP_COUNT 1u
#else
#include "vulkan_ps5_compute_spv.h"
#define SAMPLE_LABEL "compute"
#define VALUE_COUNT 1024u
#define GROUP_COUNT (VALUE_COUNT / 64u)
#endif

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf(SAMPLE_LABEL ": %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if ((compatible_types & (1u << i)) != 0 &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
            return i;
    }
    return UINT32_MAX;
}

int main(void)
{
    VkInstance instance;
    VkDevice device;
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
    VkImage image;
    VkImageView image_view;
#else
    VkBuffer buffer;
#endif
    VkDeviceMemory memory;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
    VkShaderModule shader;
    VkPipeline pipeline;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkFence fence;
    void *mapped;

    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t physical_count = 1u;
    VkPhysicalDevice physical;
    VK_CHECK(vkEnumeratePhysicalDevices(
        instance, &physical_count, &physical));
    if (physical_count != 1u) {
        printf("compute: expected one physical device\n");
        return 1;
    }
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
#if defined(VULKAN_PS5_VARIABLE_POINTERS_PROBE)
    VkPhysicalDeviceVariablePointersFeatures variable_pointer_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &variable_pointer_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features2);
    if (!variable_pointer_features.variablePointers ||
        !variable_pointer_features.variablePointersStorageBuffer) {
        printf("variable_pointers: required features are unavailable\n");
        return 1;
    }
    const void *device_features = &variable_pointer_features;
#elif defined(VULKAN_PS5_SCALAR_BLOCK_LAYOUT_PROBE)
    VkPhysicalDeviceScalarBlockLayoutFeatures scalar_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &scalar_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features2);
    if (!scalar_features.scalarBlockLayout) {
        printf("scalar_block_layout: required feature is unavailable\n");
        return 1;
    }
    scalar_features.scalarBlockLayout = VK_TRUE;
    const void *device_features = &scalar_features;
#elif defined(VULKAN_PS5_STORAGE_IMAGE_PROBE) || \
      defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
        !supported_features.shaderStorageImageWriteWithoutFormat
#else
        !supported_features.robustBufferAccess
#endif
    ) {
        printf(SAMPLE_LABEL ": required feature is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures requested_features = {
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
        .shaderStorageImageWriteWithoutFormat = VK_TRUE,
#else
        .robustBufferAccess = VK_TRUE,
#endif
    };
    const void *device_features = NULL;
#else
    const void *device_features = NULL;
#endif
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = device_features,
        .pEnabledFeatures =
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE) || \
    defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
            &requested_features,
#else
            NULL,
#endif
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
#if defined(VULKAN_PS5_SCALAR_BLOCK_LAYOUT_PROBE)
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames =
            (const char *const[]){VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME},
#endif
    };
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));

    const VkDeviceSize buffer_size = VALUE_COUNT * sizeof(uint32_t);
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {IMAGE_DIMENSION, IMAGE_DIMENSION, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
#else
    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
#endif
    uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX) {
        printf("compute: no host-visible compatible memory type\n");
        return 1;
    }
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    VK_CHECK(vkAllocateMemory(device, &memory_info, NULL, &memory));
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
    VK_CHECK(vkBindImageMemory(device, image, memory, 0));
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1u,
            .layerCount = 1u,
        },
    };
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &image_view));
#else
    VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
#endif
    VK_CHECK(vkMapMemory(device, memory, 0, buffer_size, 0, &mapped));
    memset(mapped, 0, (size_t)buffer_size);
#if defined(VULKAN_PS5_VARIABLE_POINTERS_PROBE)
    uint32_t *initial_values = mapped;
    memset(initial_values, 0, (size_t)buffer_size);
    initial_values[0] = 0x00000100u;
    initial_values[1] = 0x00000200u;
#elif defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
    uint32_t *initial_values = mapped;
    initial_values[4] = 0x11223344u;
    initial_values[8] = 0xffffffffu;
#elif defined(VULKAN_PS5_SCALAR_BLOCK_LAYOUT_PROBE)
    uint32_t *initial_values = mapped;
    initial_values[3] = 0x3f400000u;
    initial_values[4] = 0x3e800000u;
    initial_values[6] = 0xcdcdcdcdu;
    initial_values[8] = 0xababababu;
#endif
    const VkMappedMemoryRange mapped_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory,
        .offset = 0,
        .size = buffer_size,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &mapped_range));

    const VkDescriptorSetLayoutBinding bindings[] = {{
        .binding = 0,
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
#else
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
#endif
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    },
#if defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
    {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    },
#endif
    };
    const VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = sizeof(bindings) / sizeof(bindings[0]),
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(
        device, &set_layout_info, NULL, &set_layout));
    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
    };
    VK_CHECK(vkCreatePipelineLayout(
        device, &pipeline_layout_info, NULL, &pipeline_layout));
    const VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_compute_spv),
        .pCode = vulkan_ps5_compute_spv,
    };
    VK_CHECK(vkCreateShaderModule(device, &shader_info, NULL, &shader));
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
    VK_CHECK(vkCreateComputePipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));

    const VkDescriptorPoolSize pool_size = {
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
#else
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
#if defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
        2,
#else
        1,
#endif
#endif
    };
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    VK_CHECK(vkCreateDescriptorPool(
        device, &pool_info, NULL, &descriptor_pool));
    const VkDescriptorSetAllocateInfo descriptor_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &set_layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(
        device, &descriptor_allocate_info, &descriptor_set));
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
    const VkDescriptorImageInfo descriptor_image = {
        .imageView = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
#else
    const VkDescriptorBufferInfo descriptor_buffers[] = {{
        .buffer = buffer,
        .offset = 0,
#if defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
        .range = 4u * sizeof(uint32_t),
#else
        .range = buffer_size,
#endif
    },
#if defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
    {
        .buffer = buffer,
        .offset = 8u * sizeof(uint32_t),
        .range = 4u * sizeof(uint32_t),
    },
#endif
    };
#endif
    const VkWriteDescriptorSet descriptor_writes[] = {{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType =
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &descriptor_image,
#else
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &descriptor_buffers[0],
#endif
    },
#if defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
    {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set,
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &descriptor_buffers[1],
    },
#endif
    };
    vkUpdateDescriptorSets(device,
        sizeof(descriptor_writes) / sizeof(descriptor_writes[0]),
        descriptor_writes, 0, NULL);

    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VK_CHECK(vkCreateCommandPool(
        device, &command_pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo command_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(
        device, &command_allocate_info, &command));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(command, &begin_info));
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
    const VkImageMemoryBarrier image_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0u,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1u,
            .layerCount = 1u,
        },
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, NULL, 0u, NULL,
        1u, &image_barrier);
#endif
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
    vkCmdDispatch(command, GROUP_COUNT,
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
        GROUP_COUNT,
#else
        1,
#endif
        1);
    VK_CHECK(vkEndCommandBuffer(command));

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1, &mapped_range));
    const uint32_t *values = mapped;
#if defined(VULKAN_PS5_VARIABLE_POINTERS_PROBE)
    uint32_t expected[VALUE_COUNT] = {0};
    expected[0] = 0x00000100u;
    expected[1] = 0x00000200u;
    for (uint32_t i = 0; i < 64u; ++i) {
        expected[2u + i] = (i & 1u) ? 0x00000200u : 0x00000100u;
        expected[66u + 2u * i + (i & 1u)] = 0x00000400u + i;
        expected[194u + i] = 0x00000800u + i;
    }
    int status = memcmp(values, expected, sizeof(expected)) != 0;
    if (status) {
        uint32_t mismatch = 0u;
        while (mismatch < VALUE_COUNT && values[mismatch] == expected[mismatch])
            ++mismatch;
        printf("variable_pointers: mismatch index=%u actual=%08x expected=%08x\n",
            mismatch, mismatch < VALUE_COUNT ? values[mismatch] : 0u,
            mismatch < VALUE_COUNT ? expected[mismatch] : 0u);
        for (uint32_t i = 0; i < 8u; ++i) {
            printf("variable_pointers: lane=%u load=%08x store_a=%08x store_b=%08x workgroup=%08x\n",
                i, values[2u + i], values[66u + 2u * i],
                values[67u + 2u * i], values[194u + i]);
        }
        uint32_t unexpected_guards = 0u;
        for (uint32_t i = 258u;
             i < VALUE_COUNT && unexpected_guards < 16u; ++i) {
            if (values[i] == 0u) continue;
            printf("variable_pointers: guard index=%u value=%08x\n",
                i, values[i]);
            ++unexpected_guards;
        }
    } else {
        printf("variable_pointers: PASS invocations=64 storage_load=64 storage_store=64 workgroup=64\n");
    }
#elif defined(VULKAN_PS5_ROBUST_BUFFER_PROBE)
    int status = 0;
#if !defined(OPENAGC_PROSPERO)
    (void)values;
    printf(SAMPLE_LABEL ": PASS command recording\n");
#else
    if (values[4] != 0x11223344u || values[8] != 0u) {
        printf(SAMPLE_LABEL ": mismatch guard=%08x read=%08x\n",
            values[4], values[8]);
        status = 1;
    } else {
        printf(SAMPLE_LABEL ": PASS OOB read=0 OOB store=discarded\n");
    }
#endif
#elif defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
    int status = 0;
#if !defined(OPENAGC_PROSPERO)
    (void)values;
    printf(SAMPLE_LABEL ": PASS command recording\n");
#else
    for (uint32_t y = 0; y < IMAGE_DIMENSION && !status; ++y) {
        for (uint32_t x = 0; x < IMAGE_DIMENSION; ++x) {
            const uint32_t expected = ((x ^ y) & 1u) == 0u ?
                0xffff00ffu : 0xff00ff00u;
            const uint32_t index = y * IMAGE_DIMENSION + x;
            if (values[index] != expected) {
                printf(SAMPLE_LABEL ": mismatch x=%u y=%u actual=%08x expected=%08x\n",
                    x, y, values[index], expected);
                status = 1;
                break;
            }
        }
    }
    if (!status)
        printf(SAMPLE_LABEL ": PASS %u deterministic pixels\n", VALUE_COUNT);
#endif
#elif defined(VULKAN_PS5_SCALAR_BLOCK_LAYOUT_PROBE)
    const uint32_t expected = 0x3f400000u ^ 0x5a5a5a5au;
#if !defined(OPENAGC_PROSPERO)
    (void)values;
    (void)expected;
    int status = 0;
    printf("scalar_block_layout: PASS command recording\n");
#else
    int status = values[6] != expected || values[8] != 0xababababu;
    if (status) {
        printf("scalar_block_layout: mismatch result=%08x expected=%08x guard=%08x\n",
            values[6], expected, values[8]);
    } else {
        printf("scalar_block_layout: PASS stride=12 result_offset=24 result=%08x\n",
            values[6]);
    }
#endif
#else
    int status = 0;
    for (uint32_t i = 0; i < VALUE_COUNT; ++i) {
        uint32_t expected = i ^ 0x5a5a5a5au;
        if (values[i] != expected) {
            printf("compute: mismatch at %u: %08x != %08x\n",
                i, values[i], expected);
            status = 1;
            break;
        }
    }
    if (!status)
        printf("compute: PASS %u deterministic values\n", VALUE_COUNT);
#endif

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyShaderModule(device, shader, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, set_layout, NULL);
    vkUnmapMemory(device, memory);
#if defined(VULKAN_PS5_STORAGE_IMAGE_PROBE)
    vkDestroyImageView(device, image_view, NULL);
    vkDestroyImage(device, image, NULL);
#else
    vkDestroyBuffer(device, buffer, NULL);
#endif
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
#if (defined(VULKAN_PS5_VARIABLE_POINTERS_PROBE) || \
     defined(VULKAN_PS5_STORAGE_IMAGE_PROBE) || \
     defined(VULKAN_PS5_ROBUST_BUFFER_PROBE) || \
     defined(VULKAN_PS5_SCALAR_BLOCK_LAYOUT_PROBE)) && \
    defined(OPENAGC_PROSPERO)
    fflush(stdout);
    vulkan_ps5_system_service_exit(SAMPLE_LABEL);
#endif
    return status;
}
