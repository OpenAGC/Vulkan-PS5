#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../system_service_exit.h"

#if defined(VULKAN_PS5_INDIRECT_DRAW_PROBE) || \
    defined(VULKAN_PS5_INDIRECT_PARAMETERS_PROBE)
#define VULKAN_PS5_ANY_INDIRECT_PROBE 1
#endif

#if defined(VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE) || \
    defined(VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE)
#define VULKAN_PS5_FRAGMENT_RESULT_PROBE 1
#endif

#ifdef VULKAN_PS5_INDIRECT_DRAW_PROBE
#include "vulkan_ps5_indirect_draw_vert_spv.h"
#define SAMPLE_VERTEX_SPV vulkan_ps5_indirect_draw_vert_spv
#elif defined(VULKAN_PS5_INDIRECT_PARAMETERS_PROBE)
#include "vulkan_ps5_indirect_parameters_vert_spv.h"
#define SAMPLE_VERTEX_SPV vulkan_ps5_indirect_parameters_vert_spv
#else
#include "vulkan_ps5_indexed_textured_vert_spv.h"
#define SAMPLE_VERTEX_SPV vulkan_ps5_indexed_textured_vert_spv
#endif
#if defined(VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE)
#include "vulkan_ps5_custom_border_color_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_custom_border_color_frag_spv
#elif defined(VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE)
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PARTIAL_PROBE
#include "vulkan_ps5_partial_sample_rate_shading_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_partial_sample_rate_shading_frag_spv
#else
#include "vulkan_ps5_sample_rate_shading_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_sample_rate_shading_frag_spv
#endif
#elif defined(VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE)
#include "vulkan_ps5_fragment_stores_atomics_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_fragment_stores_atomics_frag_spv
#elif defined(VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE)
#include "vulkan_ps5_image_cube_array_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_image_cube_array_frag_spv
#elif defined(VULKAN_PS5_IMAGE_GATHER_PROBE)
#include "vulkan_ps5_image_gather_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_image_gather_frag_spv
#elif defined(VULKAN_PS5_DUAL_SRC_BLEND_PROBE)
#include "vulkan_ps5_dual_src_blend_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_dual_src_blend_frag_spv
#else
#include "vulkan_ps5_indexed_textured_frag_spv.h"
#define SAMPLE_FRAGMENT_SPV vulkan_ps5_indexed_textured_frag_spv
#endif

#define TARGET_WIDTH 256u
#define TARGET_HEIGHT 256u

#ifdef VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE
#define SAMPLE_NAME "custom_border_color"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE)
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PARTIAL_PROBE
#define SAMPLE_NAME "partial_sample_rate_shading"
#else
#define SAMPLE_NAME "sample_rate_shading"
#endif
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE)
#define SAMPLE_NAME "fragment_stores_atomics"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE)
#define SAMPLE_NAME "image_cube_array"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_IMAGE_GATHER_PROBE)
#define SAMPLE_NAME "shader_image_gather_extended"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_DUAL_SRC_BLEND_PROBE)
#define SAMPLE_NAME "dual_src_blend"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_VERTEX_DIVISOR_PROBE)
#define SAMPLE_NAME "vertex_divisor"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_INDIRECT_DRAW_PROBE)
#define SAMPLE_NAME "indirect_draw"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_INDIRECT_PARAMETERS_PROBE)
#define SAMPLE_NAME "indirect_parameters"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_NEAREST
#elif defined(VULKAN_PS5_ANISOTROPY_PROBE)
#define SAMPLE_NAME "sampler_anisotropy"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_REPEAT
#define SAMPLE_FILTER VK_FILTER_LINEAR
#elif defined(VULKAN_PS5_MIRROR_CLAMP_PROBE)
#define SAMPLE_NAME "mirror_clamp"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_LINEAR
#else
#define SAMPLE_NAME "indexed_textured"
#define SAMPLE_ADDRESS_MODE VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
#define SAMPLE_FILTER VK_FILTER_LINEAR
#endif

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf(SAMPLE_NAME ": %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

typedef struct Vertex {
    float position[2];
    float uv[2];
} Vertex;

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if ((compatible_types & (1u << i)) &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return i;
    }
    return UINT32_MAX;
}

static VkResult create_buffer(
    VkPhysicalDevice physical, VkDevice device, VkDeviceSize size,
    VkBufferUsageFlags usage, VkBuffer *buffer, VkDeviceMemory *memory,
    void **mapped)
{
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = vkCreateBuffer(device, &info, NULL, buffer);
    if (result != VK_SUCCESS) return result;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);
    uint32_t type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (type == UINT32_MAX) return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type,
    };
    result = vkAllocateMemory(device, &allocation, NULL, memory);
    if (result != VK_SUCCESS) return result;
    result = vkBindBufferMemory(device, *buffer, *memory, 0);
    if (result != VK_SUCCESS) return result;
    return vkMapMemory(device, *memory, 0, requirements.size, 0, mapped);
}

int main(void)
{
    VkInstance instance;
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    VkPhysicalDevice physical;
    uint32_t physical_count = 1u;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u) return 1;
#if defined(VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE)
    VkPhysicalDeviceCustomBorderColorFeaturesEXT supported_custom_border = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
    };
    VkPhysicalDeviceBorderColorSwizzleFeaturesEXT supported_border_swizzle = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT,
        .pNext = &supported_custom_border,
    };
    VkPhysicalDeviceFeatures2 supported_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &supported_border_swizzle,
    };
    vkGetPhysicalDeviceFeatures2(physical, &supported_features);
    if (!supported_custom_border.customBorderColors ||
        !supported_custom_border.customBorderColorWithoutFormat ||
        !supported_border_swizzle.borderColorSwizzleFromImage) {
        printf("custom_border_color: required feature contract is unavailable\n");
        return 1;
    }
    VkPhysicalDeviceCustomBorderColorFeaturesEXT enabled_custom_border = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
        .customBorderColors = VK_TRUE,
        .customBorderColorWithoutFormat = VK_TRUE,
    };
    VkPhysicalDeviceBorderColorSwizzleFeaturesEXT enabled_border_swizzle = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT,
        .pNext = &enabled_custom_border,
        .borderColorSwizzleFromImage = VK_TRUE,
    };
#elif defined(VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.sampleRateShading) {
        printf("sample_rate_shading: required core feature is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .sampleRateShading = VK_TRUE,
    };
#elif defined(VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.imageCubeArray) {
        printf("image_cube_array: required core feature is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .imageCubeArray = VK_TRUE,
    };
#elif defined(VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.fragmentStoresAndAtomics) {
        printf("fragment_stores_atomics: required core feature is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .fragmentStoresAndAtomics = VK_TRUE,
    };
#elif defined(VULKAN_PS5_IMAGE_GATHER_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.shaderImageGatherExtended) {
        printf("shader_image_gather_extended: required core feature is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .shaderImageGatherExtended = VK_TRUE,
    };
#elif defined(VULKAN_PS5_DUAL_SRC_BLEND_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.dualSrcBlend) {
        printf("dual_src_blend: required core feature is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .dualSrcBlend = VK_TRUE,
    };
#elif defined(VULKAN_PS5_INDIRECT_DRAW_PROBE)
    VkPhysicalDeviceShaderDrawParametersFeatures supported_draw_parameters = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,
    };
    VkPhysicalDeviceFeatures2 supported_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &supported_draw_parameters,
    };
    vkGetPhysicalDeviceFeatures2(physical, &supported_features);
    if (!supported_features.features.multiDrawIndirect ||
        !supported_features.features.drawIndirectFirstInstance ||
        !supported_draw_parameters.shaderDrawParameters) {
        printf("indirect_draw: required feature contract is unavailable\n");
        return 1;
    }
    VkPhysicalDeviceShaderDrawParametersFeatures enabled_draw_parameters = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,
        .shaderDrawParameters = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 enabled_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enabled_draw_parameters,
        .features = {
            .multiDrawIndirect = VK_TRUE,
            .drawIndirectFirstInstance = VK_TRUE,
        },
    };
#elif defined(VULKAN_PS5_ANISOTROPY_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.samplerAnisotropy) {
        printf("sampler_anisotropy: required core feature is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .samplerAnisotropy = VK_TRUE,
    };
#elif defined(VULKAN_PS5_VERTEX_DIVISOR_PROBE)
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT supported_divisor = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 supported_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &supported_divisor,
    };
    vkGetPhysicalDeviceFeatures2(physical, &supported_features);
    if (!supported_divisor.vertexAttributeInstanceRateDivisor ||
        supported_divisor.vertexAttributeInstanceRateZeroDivisor) {
        printf("vertex_divisor: required divisor feature contract is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT enabled_divisor = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT,
        .vertexAttributeInstanceRateDivisor = VK_TRUE,
    };
#endif
    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
#if defined(VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE)
    const char *const device_extensions[] = {
        VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
        VK_EXT_BORDER_COLOR_SWIZZLE_EXTENSION_NAME,
    };
#elif defined(VULKAN_PS5_MIRROR_CLAMP_PROBE)
    const char *const device_extensions[] = {
        VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
    };
#elif defined(VULKAN_PS5_VERTEX_DIVISOR_PROBE)
    const char *const device_extensions[] = {
        VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
    };
#endif
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
#if defined(VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE)
        .pNext = &enabled_border_swizzle,
        .enabledExtensionCount = 2u,
        .ppEnabledExtensionNames = device_extensions,
#elif defined(VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE) || \
    defined(VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE) || \
    defined(VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE) || \
    defined(VULKAN_PS5_IMAGE_GATHER_PROBE) || \
    defined(VULKAN_PS5_DUAL_SRC_BLEND_PROBE)
        .pEnabledFeatures = &enabled_features,
#elif defined(VULKAN_PS5_MIRROR_CLAMP_PROBE)
        .enabledExtensionCount = 1u,
        .ppEnabledExtensionNames = device_extensions,
#elif defined(VULKAN_PS5_VERTEX_DIVISOR_PROBE)
        .pNext = &enabled_divisor,
        .enabledExtensionCount = 1u,
        .ppEnabledExtensionNames = device_extensions,
#elif defined(VULKAN_PS5_ANISOTROPY_PROBE)
        .pEnabledFeatures = &enabled_features,
#elif defined(VULKAN_PS5_INDIRECT_DRAW_PROBE)
        .pNext = &enabled_features,
#endif
    };
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));

    const VkImageCreateInfo target_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {TARGET_WIDTH, TARGET_HEIGHT, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples =
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE
            VK_SAMPLE_COUNT_4_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
#else
            VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
#endif
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout =
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE
            VK_IMAGE_LAYOUT_UNDEFINED,
#else
            VK_IMAGE_LAYOUT_PREINITIALIZED,
#endif
    };
    VkImage target;
    VK_CHECK(vkCreateImage(device, &target_info, NULL, &target));
    VkMemoryRequirements target_requirements;
    vkGetImageMemoryRequirements(device, target, &target_requirements);
    uint32_t target_type = find_host_visible_memory_type(
        physical, target_requirements.memoryTypeBits);
    if (target_type == UINT32_MAX) return 1;
    const VkMemoryAllocateInfo target_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = target_requirements.size,
        .memoryTypeIndex = target_type,
    };
    VkDeviceMemory target_memory;
    VK_CHECK(vkAllocateMemory(device, &target_allocation, NULL, &target_memory));
    VK_CHECK(vkBindImageMemory(device, target, target_memory, 0));
    void *target_mapped;
    VK_CHECK(vkMapMemory(device, target_memory, 0, target_requirements.size,
                         0, &target_mapped));
    memset(target_mapped, 0, (size_t)target_requirements.size);
    const VkMappedMemoryRange target_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = target_memory,
        .size = target_requirements.size,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &target_range));

    VkImageCreateInfo texture_info = target_info;
    texture_info.samples = VK_SAMPLE_COUNT_1_BIT;
    texture_info.tiling = VK_IMAGE_TILING_LINEAR;
    texture_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
#ifdef VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE
    texture_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    texture_info.arrayLayers = 12u;
#endif
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
    texture_info.extent.width = 256;
    texture_info.extent.height = 4;
#else
    texture_info.extent.width = 2;
    texture_info.extent.height = 2;
#endif
    texture_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage texture;
    VK_CHECK(vkCreateImage(device, &texture_info, NULL, &texture));
    VkMemoryRequirements texture_requirements;
    vkGetImageMemoryRequirements(device, texture, &texture_requirements);
    uint32_t texture_type = find_host_visible_memory_type(
        physical, texture_requirements.memoryTypeBits);
    if (texture_type == UINT32_MAX) return 1;
    const VkMemoryAllocateInfo texture_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = texture_requirements.size,
        .memoryTypeIndex = texture_type,
    };
    VkDeviceMemory texture_memory;
    VK_CHECK(vkAllocateMemory(device, &texture_allocation, NULL, &texture_memory));
    VK_CHECK(vkBindImageMemory(device, texture, texture_memory, 0));
    uint32_t *texture_mapped;
    VK_CHECK(vkMapMemory(device, texture_memory, 0, texture_requirements.size,
                         0, (void **)&texture_mapped));
    const VkImageSubresource texture_subresource = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
    };
    VkSubresourceLayout texture_layout;
    vkGetImageSubresourceLayout(device, texture, &texture_subresource,
                                &texture_layout);
#ifdef VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE
    for (uint32_t layer = 0u; layer < texture_info.arrayLayers; ++layer) {
        const VkImageSubresource layer_subresource = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, layer,
        };
        VkSubresourceLayout layer_layout;
        vkGetImageSubresourceLayout(device, texture, &layer_subresource,
                                    &layer_layout);
        for (uint32_t y = 0u; y < texture_info.extent.height; ++y) {
            uint32_t *row = (uint32_t *)((uint8_t *)texture_mapped +
                layer_layout.offset + y * layer_layout.rowPitch);
            for (uint32_t x = 0u; x < texture_info.extent.width; ++x)
                row[x] = layer < 6u ? 0xff0000ffu : 0xff00ff00u;
        }
    }
#elif defined(VULKAN_PS5_ANISOTROPY_PROBE)
    for (uint32_t y = 0; y < texture_info.extent.height; ++y) {
        uint32_t *row = (uint32_t *)
            ((uint8_t *)texture_mapped + y * texture_layout.rowPitch);
        for (uint32_t x = 0; x < texture_info.extent.width; ++x)
            row[x] = (x & 1u) ? 0xffffffffu : 0xff000000u;
    }
#else
    texture_mapped[0] = 0xff0000ffu;
    texture_mapped[1] = 0xff00ff00u;
    uint32_t *texture_row1 = (uint32_t *)
        ((uint8_t *)texture_mapped + texture_layout.rowPitch);
    texture_row1[0] = 0xffff0000u;
    texture_row1[1] = 0xffffffffu;
#endif
    const VkMappedMemoryRange texture_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = texture_memory,
        .size = texture_requirements.size,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &texture_range));

#ifdef VULKAN_PS5_VERTEX_DIVISOR_PROBE
    const Vertex vertices[4] = {
        {{9.0f, 9.0f}, {0.25f, 0.25f}},
        {{-0.75f, -0.75f}, {0.75f, 0.75f}},
        {{ 0.75f, -0.75f}, {0.75f, 0.25f}},
        {{ 0.00f,  0.75f}, {0.25f, 0.75f}},
    };
#elif defined(VULKAN_PS5_ANY_INDIRECT_PROBE)
    const Vertex vertices[4] = {
        {{9.0f, 9.0f}, {0.25f, 0.25f}},
        {{-0.35f, -0.50f}, {0.25f, 0.25f}},
        {{ 0.35f, -0.50f}, {0.25f, 0.25f}},
        {{ 0.00f,  0.50f}, {0.25f, 0.25f}},
    };
#elif defined(VULKAN_PS5_ANISOTROPY_PROBE)
    const Vertex vertices[7] = {
        {{9.0f, 9.0f}, {0.0f, 0.5f}},
        {{-0.90f, -0.75f}, {0.000f, 0.5f}},
        {{-0.10f, -0.75f}, {4.375f, 0.5f}},
        {{-0.50f,  0.75f}, {2.1875f, 0.5f}},
        {{ 0.10f, -0.75f}, {0.000f, 0.5f}},
        {{ 0.90f, -0.75f}, {4.375f, 0.5f}},
        {{ 0.50f,  0.75f}, {2.1875f, 0.5f}},
    };
#elif defined(VULKAN_PS5_MIRROR_CLAMP_PROBE)
    const Vertex vertices[4] = {
        {{9.0f, 9.0f}, {-0.5f, -0.5f}},
        {{-0.75f, -0.75f}, {-0.5f, -0.5f}},
        {{ 0.75f, -0.75f}, {-0.5f, -0.5f}},
        {{ 0.00f,  0.75f}, {-0.5f, -0.5f}},
    };
#else
    const Vertex vertices[4] = {
        {{9.0f, 9.0f}, {0.5f, 0.5f}},
        {{-0.75f, -0.75f}, {0.0f, 0.0f}},
        {{ 0.75f, -0.75f}, {1.0f, 0.0f}},
        {{ 0.00f,  0.75f}, {0.5f, 1.0f}},
    };
#endif
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
    const uint16_t indices[6] = {1, 2, 3, 4, 5, 6};
#else
    const uint16_t indices[3] = {1, 2, 3};
#endif
    VkBuffer vertex_buffer, index_buffer;
    VkDeviceMemory vertex_memory, index_memory;
    void *vertex_mapped, *index_mapped;
    VK_CHECK(create_buffer(physical, device, sizeof(vertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vertex_buffer, &vertex_memory,
        &vertex_mapped));
    VK_CHECK(create_buffer(physical, device, sizeof(indices),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &index_buffer, &index_memory,
        &index_mapped));
    memcpy(vertex_mapped, vertices, sizeof(vertices));
    memcpy(index_mapped, indices, sizeof(indices));
    const VkMappedMemoryRange upload_ranges[] = {
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, vertex_memory, 0, VK_WHOLE_SIZE},
        {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, index_memory, 0, VK_WHOLE_SIZE},
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 2, upload_ranges));
#ifdef VULKAN_PS5_FRAGMENT_RESULT_PROBE
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE
    enum { FRAGMENT_RESULT_WORDS = 5u };
#else
    enum { FRAGMENT_RESULT_WORDS = 1u + TARGET_WIDTH * TARGET_HEIGHT };
#endif
    VkBuffer fragment_result_buffer;
    VkDeviceMemory fragment_result_memory;
    uint32_t *fragment_results;
    VK_CHECK(create_buffer(physical, device,
        (VkDeviceSize)FRAGMENT_RESULT_WORDS * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &fragment_result_buffer,
        &fragment_result_memory, (void **)&fragment_results));
    memset(fragment_results, 0,
        (size_t)FRAGMENT_RESULT_WORDS * sizeof(uint32_t));
    const VkMappedMemoryRange fragment_result_range = {
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL,
        fragment_result_memory, 0, VK_WHOLE_SIZE,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &fragment_result_range));
#endif
#ifdef VULKAN_PS5_ANY_INDIRECT_PROBE
#ifdef VULKAN_PS5_INDIRECT_DRAW_PROBE
    const VkDrawIndirectCommand indirect_commands[2] = {
        {3u, 1u, 1u, 1u},
        {3u, 1u, 1u, 2u},
    };
#else
    const VkDrawIndirectCommand indirect_commands[1] = {
        {3u, 1u, 1u, 1u},
    };
#endif
    VkBuffer indirect_buffer;
    VkDeviceMemory indirect_memory;
    void *indirect_mapped;
    VK_CHECK(create_buffer(physical, device, sizeof(indirect_commands),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &indirect_buffer,
        &indirect_memory, &indirect_mapped));
    memcpy(indirect_mapped, indirect_commands, sizeof(indirect_commands));
    const VkMappedMemoryRange indirect_range = {
        VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, indirect_memory, 0,
        VK_WHOLE_SIZE,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &indirect_range));
#endif

    const VkImageViewCreateInfo target_view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = target,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView target_view;
    VK_CHECK(vkCreateImageView(device, &target_view_info, NULL, &target_view));
    VkImageViewCreateInfo texture_view_info = target_view_info;
    texture_view_info.image = texture;
#ifdef VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE
    texture_view_info.components = (VkComponentMapping){
        VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_A,
    };
#endif
#ifdef VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE
    texture_view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    texture_view_info.subresourceRange.layerCount = 12u;
#endif
    VkImageView texture_view;
    VK_CHECK(vkCreateImageView(device, &texture_view_info, NULL, &texture_view));
#ifdef VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE
    const VkSamplerCustomBorderColorCreateInfoEXT custom_border_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
        .customBorderColor = {.float32 = {1.0f, 0.0f, 0.0f, 1.0f}},
        .format = VK_FORMAT_UNDEFINED,
    };
#endif
    const VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
#ifdef VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE
        .pNext = &custom_border_info,
#endif
        .magFilter = SAMPLE_FILTER,
        .minFilter = SAMPLE_FILTER,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = SAMPLE_ADDRESS_MODE,
        .addressModeV = SAMPLE_ADDRESS_MODE,
        .addressModeW = SAMPLE_ADDRESS_MODE,
#ifdef VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE
        .borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT,
#endif
    };
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(device, &sampler_info, NULL, &sampler));
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
    VkSamplerCreateInfo anisotropic_sampler_info = sampler_info;
    anisotropic_sampler_info.anisotropyEnable = VK_TRUE;
    anisotropic_sampler_info.maxAnisotropy = 16.0f;
    VkSampler anisotropic_sampler;
    VK_CHECK(vkCreateSampler(device, &anisotropic_sampler_info, NULL,
                             &anisotropic_sampler));
#endif

    const VkDescriptorSetLayoutBinding texture_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
#ifdef VULKAN_PS5_FRAGMENT_RESULT_PROBE
    const VkDescriptorSetLayoutBinding set_bindings[2] = {
        texture_binding,
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
#define SAMPLE_SET_BINDINGS set_bindings
#define SAMPLE_SET_BINDING_COUNT 2u
#else
#define SAMPLE_SET_BINDINGS (&texture_binding)
#define SAMPLE_SET_BINDING_COUNT 1u
#endif
    const VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = SAMPLE_SET_BINDING_COUNT,
        .pBindings = SAMPLE_SET_BINDINGS,
    };
    VkDescriptorSetLayout set_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &set_layout_info, NULL, &set_layout));
#ifdef VULKAN_PS5_FRAGMENT_RESULT_PROBE
    const VkDescriptorPoolSize pool_sizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    };
#define SAMPLE_POOL_SIZES pool_sizes
#define SAMPLE_POOL_SIZE_COUNT 2u
#else
    const VkDescriptorPoolSize pool_size = {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
        2,
#else
        1,
#endif
    };
#define SAMPLE_POOL_SIZES (&pool_size)
#define SAMPLE_POOL_SIZE_COUNT 1u
#endif
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets =
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
            2,
#else
            1,
#endif
        .poolSizeCount = SAMPLE_POOL_SIZE_COUNT,
        .pPoolSizes = SAMPLE_POOL_SIZES,
    };
    VkDescriptorPool descriptor_pool;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool));
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
    const VkDescriptorSetLayout set_layouts[2] = {set_layout, set_layout};
    VkDescriptorSet descriptor_sets[2];
    const VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 2,
        .pSetLayouts = set_layouts,
    };
    VK_CHECK(vkAllocateDescriptorSets(device, &set_info, descriptor_sets));
    VkDescriptorImageInfo descriptor_images[2] = {
        {sampler, texture_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {anisotropic_sampler, texture_view,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet descriptor_writes[2] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, descriptor_sets[0], 0,
         0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &descriptor_images[0],
         NULL, NULL},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, descriptor_sets[1], 0,
         0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &descriptor_images[1],
         NULL, NULL},
    };
    vkUpdateDescriptorSets(device, 2, descriptor_writes, 0, NULL);
#else
    const VkDescriptorSetAllocateInfo set_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &set_layout,
    };
    VkDescriptorSet descriptor_set;
    VK_CHECK(vkAllocateDescriptorSets(device, &set_info, &descriptor_set));
    const VkDescriptorImageInfo descriptor_image = {
        .sampler = sampler,
        .imageView = texture_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
#ifdef VULKAN_PS5_FRAGMENT_RESULT_PROBE
    const VkDescriptorBufferInfo fragment_result_info = {
        .buffer = fragment_result_buffer,
        .range = (VkDeviceSize)FRAGMENT_RESULT_WORDS * sizeof(uint32_t),
    };
    const VkWriteDescriptorSet descriptor_writes[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &descriptor_image,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &fragment_result_info,
        },
    };
    vkUpdateDescriptorSets(device, 2, descriptor_writes, 0, NULL);
#else
    const VkWriteDescriptorSet descriptor_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &descriptor_image,
    };
    vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, NULL);
#endif
#endif

    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples =
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE
            VK_SAMPLE_COUNT_4_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
#else
            VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
#endif
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout =
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE
            VK_IMAGE_LAYOUT_UNDEFINED,
#else
            VK_IMAGE_LAYOUT_PREINITIALIZED,
#endif
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference color_attachment = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
    };
    const VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass render_pass;
    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .pAttachments = &target_view,
        .width = TARGET_WIDTH,
        .height = TARGET_HEIGHT,
        .layers = 1,
    };
    VkFramebuffer framebuffer;
    VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));

    const VkShaderModuleCreateInfo vertex_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(SAMPLE_VERTEX_SPV),
        .pCode = SAMPLE_VERTEX_SPV,
    };
    const VkShaderModuleCreateInfo fragment_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(SAMPLE_FRAGMENT_SPV),
        .pCode = SAMPLE_FRAGMENT_SPV,
    };
    VkShaderModule vertex_shader, fragment_shader;
    VK_CHECK(vkCreateShaderModule(device, &vertex_shader_info, NULL, &vertex_shader));
    VK_CHECK(vkCreateShaderModule(device, &fragment_shader_info, NULL, &fragment_shader));
    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
    };
    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
                                    &pipeline_layout));
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vertex_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader, "main", NULL},
    };
#ifdef VULKAN_PS5_VERTEX_DIVISOR_PROBE
    const VkVertexInputBindingDescription bindings[] = {
        {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(Vertex), VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
        {1, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
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
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = &divisor_state,
        .vertexBindingDescriptionCount = 2,
        .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributes,
    };
#else
    const VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributes,
    };
#endif
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkViewport viewport = {0, 0, TARGET_WIDTH, TARGET_HEIGHT, 0, 1};
    const VkRect2D scissor = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}};
    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount = 1, .pScissors = &scissor,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples =
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE
            VK_SAMPLE_COUNT_4_BIT,
        .sampleShadingEnable = VK_TRUE,
        .minSampleShading =
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PARTIAL_PROBE
            0.5f,
#else
            1.0f,
#endif
#else
            VK_SAMPLE_COUNT_1_BIT,
#endif
    };
    const VkPipelineColorBlendAttachmentState blend_attachment = {
#ifdef VULKAN_PS5_DUAL_SRC_BLEND_PROBE
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
#endif
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment,
    };
    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .layout = pipeline_layout,
        .renderPass = render_pass,
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &pipeline_info, NULL, &pipeline));

    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo command_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    const VkCommandBufferBeginInfo command_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(command, &command_begin));
    const VkImageMemoryBarrier texture_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
#ifdef VULKAN_PS5_ANY_INDIRECT_PROBE
    const VkBufferMemoryBarrier native_buffer_barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = 0u,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vertex_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = 0u,
            .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = indirect_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 2, native_buffer_barriers, 1, &texture_barrier);
#else
    const VkBufferMemoryBarrier native_buffer_barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = 0u,
            .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vertex_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = 0u,
            .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = index_buffer,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        },
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, NULL, 2, native_buffer_barriers, 1, &texture_barrier);
#endif
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}},
    };
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout, 0, 1, &descriptor_sets[0], 0, NULL);
#else
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
#endif
#ifdef VULKAN_PS5_VERTEX_DIVISOR_PROBE
    const VkBuffer vertex_buffers[] = {vertex_buffer, vertex_buffer};
    const VkDeviceSize vertex_offsets[] = {0, 0};
    vkCmdBindVertexBuffers(command, 0, 2, vertex_buffers, vertex_offsets);
#else
    VkDeviceSize vertex_offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer, &vertex_offset);
#endif
#ifdef VULKAN_PS5_ANY_INDIRECT_PROBE
#ifdef VULKAN_PS5_INDIRECT_DRAW_PROBE
    vkCmdDrawIndirect(command, indirect_buffer, 0u, 2u,
                      sizeof(VkDrawIndirectCommand));
#else
    vkCmdDrawIndirect(command, indirect_buffer, 0u, 1u,
                      sizeof(VkDrawIndirectCommand));
#endif
#else
#ifdef VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE
    vkCmdBindIndexBuffer2(command, index_buffer, 0, sizeof(indices),
        VK_INDEX_TYPE_UINT16);
#else
    vkCmdBindIndexBuffer(command, index_buffer, 0, VK_INDEX_TYPE_UINT16);
#endif
    vkCmdDrawIndexed(command, 3,
#ifdef VULKAN_PS5_VERTEX_DIVISOR_PROBE
        4,
#else
        1,
#endif
        0, 0, 0);
#endif
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout, 0, 1, &descriptor_sets[1], 0, NULL);
    vkCmdDrawIndexed(command, 3, 1, 3, 0, 0);
#endif
    vkCmdEndRenderPass(command);
    VK_CHECK(vkEndCommandBuffer(command));

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1, &target_range));
#ifdef VULKAN_PS5_FRAGMENT_RESULT_PROBE
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1, &fragment_result_range));
#endif

#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PROBE
#ifdef VULKAN_PS5_SAMPLE_RATE_SHADING_PARTIAL_PROBE
    int status = 0;
    if (fragment_results[0] || fragment_results[1] || fragment_results[2] ||
        fragment_results[3] || !fragment_results[4]) {
        printf(SAMPLE_NAME ": mismatch total=%u guards=%u,%u,%u,%u\n",
            fragment_results[4], fragment_results[0], fragment_results[1],
            fragment_results[2], fragment_results[3]);
        status = 1;
    } else {
        printf(SAMPLE_NAME ": PASS total=%u guards=0,0,0,0\n",
            fragment_results[4]);
    }
#else
    uint32_t sample_sum = fragment_results[0] + fragment_results[1] +
        fragment_results[2] + fragment_results[3];
    int status = 0;
    if (!fragment_results[0] || !fragment_results[1] ||
        !fragment_results[2] || !fragment_results[3] ||
        sample_sum != fragment_results[4]) {
        printf(SAMPLE_NAME ": mismatch samples=%u,%u,%u,%u total=%u\n",
            fragment_results[0], fragment_results[1], fragment_results[2],
            fragment_results[3], fragment_results[4]);
        status = 1;
    } else {
        printf(SAMPLE_NAME ": PASS samples=%u,%u,%u,%u total=%u\n",
            fragment_results[0], fragment_results[1], fragment_results[2],
            fragment_results[3], fragment_results[4]);
    }
#endif
#else
    const uint32_t *pixels = target_mapped;
    uint32_t covered = 0u;
    uint32_t opaque = 0u;
    uint32_t distinct[64] = {0};
    uint32_t distinct_count = 0u;
    for (uint32_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        uint32_t pixel = pixels[i];
        if (!pixel) continue;
        covered++;
        if ((pixel >> 24u) == 0xffu) opaque++;
        bool found = false;
        for (uint32_t j = 0; j < distinct_count; ++j)
            found |= distinct[j] == pixel;
        if (!found && distinct_count < 64u)
            distinct[distinct_count++] = pixel;
    }
#if !defined(VULKAN_PS5_ANISOTROPY_PROBE) && \
    !defined(VULKAN_PS5_ANY_INDIRECT_PROBE) && \
    !defined(VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE)
    uint32_t center = pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
        TARGET_WIDTH / 2u];
#endif
    int status = 0;
#ifdef VULKAN_PS5_CUSTOM_BORDER_COLOR_PROBE
#if defined(OPENAGC_PROSPERO)
    uint32_t blue = 0u, unexpected = 0u;
    for (uint32_t i = 0u; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        if (pixels[i] == 0xffff0000u) blue++;
        else if (pixels[i] != 0u) unexpected++;
    }
    if (covered != 18432u || opaque != covered || blue != covered ||
        unexpected != 0u || center != 0xffff0000u || pixels[0] != 0u ||
        pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("custom_border_color: mismatch covered=%u blue=%u unexpected=%u center=%08x\n",
            covered, blue, unexpected, center);
        status = 1;
    } else {
        printf("custom_border_color: PASS covered=%u blue=%u swizzle=BR\n",
            covered, blue);
    }
#else
    (void)center;
    (void)covered;
    (void)opaque;
    printf("custom_border_color: PASS command-recording\n");
#endif
#elif defined(VULKAN_PS5_IMAGE_CUBE_ARRAY_PROBE)
    uint32_t red = 0u, green = 0u, unexpected = 0u;
    for (uint32_t i = 0u; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        if (pixels[i] == 0xff0000ffu) red++;
        else if (pixels[i] == 0xff00ff00u) green++;
        else if (pixels[i] != 0u) unexpected++;
    }
    if (covered != 18432u || opaque != covered || red != 9216u ||
        green != 9216u || unexpected != 0u || pixels[0] != 0u ||
        pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("image_cube_array: mismatch covered=%u red=%u green=%u unexpected=%u\n",
            covered, red, green, unexpected);
        status = 1;
    } else {
        printf("image_cube_array: PASS covered=%u red=%u green=%u cubes=2\n",
            covered, red, green);
    }
#elif defined(VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE)
    uint32_t storage_writes = 0u;
    for (uint32_t i = 1u; i < FRAGMENT_RESULT_WORDS; ++i)
        storage_writes += fragment_results[i] == 0x51a7c0deu;
    if (covered != 18432u || opaque != covered || distinct_count != 1u ||
        center != 0xff00ff00u || fragment_results[0] != covered ||
        storage_writes != covered || fragment_results[1] != 0u ||
        fragment_results[FRAGMENT_RESULT_WORDS - 1u] != 0u) {
        printf("fragment_stores_atomics: mismatch covered=%u atomic=%u stores=%u center=%08x\n",
            covered, fragment_results[0], storage_writes, center);
        status = 1;
    } else {
        printf("fragment_stores_atomics: PASS covered=%u atomic=%u stores=%u marker=51a7c0de\n",
            covered, fragment_results[0], storage_writes);
    }
#elif defined(VULKAN_PS5_DUAL_SRC_BLEND_PROBE)
    if (covered != 18432u || opaque != covered || distinct_count != 2u ||
        center != 0xff00ff00u || pixels[0] != 0u ||
        pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("dual_src_blend: mismatch covered=%u opaque=%u colors=%u center=%08x\n",
            covered, opaque, distinct_count, center);
        status = 1;
    } else {
        printf("dual_src_blend: PASS covered=%u center=%08x src1=green\n",
            covered, center);
    }
#elif defined(VULKAN_PS5_IMAGE_GATHER_PROBE)
    if (covered != 18432u || opaque != covered || distinct_count != 1u ||
        center != 0xffffffffu || pixels[0] != 0u ||
        pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("shader_image_gather_extended: mismatch covered=%u opaque=%u colors=%u center=%08x\n",
            covered, opaque, distinct_count, center);
        status = 1;
    } else {
        printf("shader_image_gather_extended: PASS covered=%u center=%08x offsets=4\n",
            covered, center);
    }
#elif defined(VULKAN_PS5_INDIRECT_DRAW_PROBE)
    uint32_t green = 0u, blue = 0u, red = 0u, white = 0u, unexpected = 0u;
    uint32_t left_green = 0u, right_green = 0u;
    for (uint32_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        if (pixels[i] == 0xff00ff00u) {
            green++;
            if (i % TARGET_WIDTH < TARGET_WIDTH / 2u)
                left_green++;
            else
                right_green++;
        } else if (pixels[i] == 0xffff0000u)
            blue++;
        else if (pixels[i] == 0xff0000ffu)
            red++;
        else if (pixels[i] == 0xffffffffu)
            white++;
        else if (pixels[i] != 0u)
            unexpected++;
    }
    if (green < 10000u || green > 13000u || left_green < 5000u ||
        right_green < 5000u || left_green + 64u < right_green ||
        right_green + 64u < left_green || blue != 0u || red != 0u ||
        white != 0u || unexpected != 0u || covered != green || opaque != covered ||
        pixels[0] != 0u || pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("indirect_draw: mismatch green=%u left=%u right=%u baseVertex=%u baseInstance=%u instanceIndex=%u unexpected=%u covered=%u\n",
            green, left_green, right_green, red, white, blue, unexpected, covered);
        status = 1;
    } else {
        printf("indirect_draw: PASS green=%u left=%u right=%u firstVertex=1 firstInstance=1,2 drawID=0,1 draws=2\n",
            green, left_green, right_green);
    }
#elif defined(VULKAN_PS5_INDIRECT_PARAMETERS_PROBE)
    uint32_t green = 0u, red = 0u, unexpected = 0u;
    for (uint32_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
        if (pixels[i] == 0xff00ff00u)
            green++;
        else if (pixels[i] == 0xff0000ffu)
            red++;
        else if (pixels[i] != 0u)
            unexpected++;
    }
    if (green < 5000u || green > 6500u || red != 0u || unexpected != 0u ||
        covered != green || opaque != covered || pixels[0] != 0u ||
        pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("indirect_parameters: mismatch green=%u red=%u unexpected=%u covered=%u\n",
            green, red, unexpected, covered);
        status = 1;
    } else {
        printf("indirect_parameters: PASS green=%u firstVertex=1 firstInstance=1 draws=1\n",
            green);
    }
#elif defined(VULKAN_PS5_ANISOTROPY_PROBE)
    uint32_t linear_count = 0u, anisotropic_count = 0u;
    uint64_t linear_sum = 0u, anisotropic_sum = 0u;
    uint64_t linear_deviation = 0u, anisotropic_deviation = 0u;
    for (uint32_t y = 0; y < TARGET_HEIGHT; ++y) {
        for (uint32_t x = 0; x < TARGET_WIDTH; ++x) {
            uint32_t pixel = pixels[y * TARGET_WIDTH + x];
            if (!pixel) continue;
            uint32_t value = pixel & 0xffu;
            uint32_t deviation = value > 128u ? value - 128u : 128u - value;
            if (x < TARGET_WIDTH / 2u) {
                linear_count++;
                linear_sum += value;
                linear_deviation += deviation;
            } else {
                anisotropic_count++;
                anisotropic_sum += value;
                anisotropic_deviation += deviation;
            }
        }
    }
    uint32_t linear_mean = linear_count ?
        (uint32_t)(linear_sum / linear_count) : 0u;
    uint32_t anisotropic_mean = anisotropic_count ?
        (uint32_t)(anisotropic_sum / anisotropic_count) : 0u;
    uint32_t linear_mad = linear_count ?
        (uint32_t)(linear_deviation / linear_count) : 0u;
    uint32_t anisotropic_mad = anisotropic_count ?
        (uint32_t)(anisotropic_deviation / anisotropic_count) : UINT32_MAX;
    if (linear_count < 8000u || linear_count > 11000u ||
        anisotropic_count < 8000u || anisotropic_count > 11000u ||
        linear_count + 64u < anisotropic_count ||
        anisotropic_count + 64u < linear_count ||
        linear_mean < 96u || linear_mean > 160u ||
        anisotropic_mean < 112u || anisotropic_mean > 144u ||
        linear_mad < 32u || anisotropic_mad * 4u >= linear_mad * 3u ||
        opaque != covered || pixels[0] != 0u ||
        pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("sampler_anisotropy: mismatch linear=%u/%u/%u aniso=%u/%u/%u opaque=%u covered=%u\n",
            linear_count, linear_mean, linear_mad, anisotropic_count,
            anisotropic_mean, anisotropic_mad, opaque, covered);
        status = 1;
    } else {
        printf("sampler_anisotropy: PASS linear=%u/%u/%u aniso=%u/%u/%u\n",
            linear_count, linear_mean, linear_mad, anisotropic_count,
            anisotropic_mean, anisotropic_mad);
    }
#elif defined(VULKAN_PS5_VERTEX_DIVISOR_PROBE)
    if (covered < 16000u || covered > 21000u || opaque != covered ||
        distinct_count != 1u || center != 0xffffffffu ||
        pixels[0] != 0u || pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("vertex_divisor: mismatch covered=%u opaque=%u colors=%u center=%08x\n",
            covered, opaque, distinct_count, center);
        status = 1;
    } else {
        printf("vertex_divisor: PASS %u pixels center=%08x divisor=2 instances=4\n",
            covered, center);
    }
#elif defined(VULKAN_PS5_MIRROR_CLAMP_PROBE)
    const uint32_t red = center & 0xffu;
    const uint32_t green = (center >> 8u) & 0xffu;
    const uint32_t blue = (center >> 16u) & 0xffu;
    if (covered < 16000u || covered > 21000u || opaque != covered ||
        center == 0xff0000ffu || red < 112u || red > 144u ||
        green < 112u || green > 144u || blue < 112u || blue > 144u ||
        pixels[0] != 0u || pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("mirror_clamp: mismatch covered=%u opaque=%u colors=%u center=%08x\n",
            covered, opaque, distinct_count, center);
        status = 1;
    } else {
        printf("mirror_clamp: PASS %u pixels center=%08x\n", covered, center);
    }
#else
    if (covered < 16000u || covered > 21000u || opaque != covered ||
        distinct_count < 16u || center == 0u || pixels[0] != 0u ||
        pixels[TARGET_WIDTH - 1u] != 0u) {
        printf("indexed_textured: mismatch covered=%u opaque=%u colors=%u center=%08x\n",
            covered, opaque, distinct_count, center);
        status = 1;
    } else {
        printf("indexed_textured: PASS %u pixels %u+ colors\n",
            covered, distinct_count);
    }
#endif
#endif

    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyShaderModule(device, fragment_shader, NULL);
    vkDestroyShaderModule(device, vertex_shader, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(device, set_layout, NULL);
#ifdef VULKAN_PS5_ANISOTROPY_PROBE
    vkDestroySampler(device, anisotropic_sampler, NULL);
#endif
    vkDestroySampler(device, sampler, NULL);
    vkDestroyImageView(device, texture_view, NULL);
    vkDestroyImageView(device, target_view, NULL);
#ifdef VULKAN_PS5_ANY_INDIRECT_PROBE
    vkUnmapMemory(device, indirect_memory);
    vkDestroyBuffer(device, indirect_buffer, NULL);
    vkFreeMemory(device, indirect_memory, NULL);
#endif
#ifdef VULKAN_PS5_FRAGMENT_STORES_ATOMICS_PROBE
    vkUnmapMemory(device, fragment_result_memory);
    vkDestroyBuffer(device, fragment_result_buffer, NULL);
    vkFreeMemory(device, fragment_result_memory, NULL);
#endif
    vkUnmapMemory(device, index_memory);
    vkUnmapMemory(device, vertex_memory);
    vkDestroyBuffer(device, index_buffer, NULL);
    vkDestroyBuffer(device, vertex_buffer, NULL);
    vkFreeMemory(device, index_memory, NULL);
    vkFreeMemory(device, vertex_memory, NULL);
    vkUnmapMemory(device, texture_memory);
    vkDestroyImage(device, texture, NULL);
    vkFreeMemory(device, texture_memory, NULL);
    vkUnmapMemory(device, target_memory);
    vkDestroyImage(device, target, NULL);
    vkFreeMemory(device, target_memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit(SAMPLE_NAME);
#endif
    return status;
}
