#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_ps5_format_sample_float_spv.h"
#include "vulkan_ps5_format_sample_sint_spv.h"
#include "vulkan_ps5_format_sample_uint_spv.h"

#include "../system_service_exit.h"

#define FORMAT_COUNT 38u
#define COMMAND_COUNT FORMAT_COUNT
#define PIPELINE_COUNT 3u
#define RESULT_WORDS 4u

typedef enum NumericClass {
    NUMERIC_FLOAT = 0,
    NUMERIC_UINT = 1,
    NUMERIC_SINT = 2,
} NumericClass;

typedef struct FormatCase {
    VkFormat format;
    const char *name;
    NumericClass numeric_class;
    VkClearColorValue clear;
    VkClearColorValue expected;
} FormatCase;

typedef struct RawTexel {
    uint32_t words[4];
    uint32_t byte_count;
} RawTexel;

typedef struct TestImage {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
} TestImage;

typedef struct ShaderCode {
    const uint32_t *words;
    size_t size;
} ShaderCode;

static const FormatCase format_cases[FORMAT_COUNT] = {
    {VK_FORMAT_R8_UNORM, "r8_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.0f, 0.0f, 1.0f}},
        {.float32 = {64.0f / 255.0f, 0.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R8_SNORM, "r8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 0.0f, 0.0f, 1.0f}},
        {.float32 = {64.0f / 127.0f, 0.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R8_UINT, "r8_uint", NUMERIC_UINT,
        {.uint32 = {0xabu, 0u, 0u, 1u}},
        {.uint32 = {0xabu, 0u, 0u, 1u}}},
    {VK_FORMAT_R8_SINT, "r8_sint", NUMERIC_SINT,
        {.int32 = {-2, 0, 0, 1}}, {.int32 = {-2, 0, 0, 1}}},
    {VK_FORMAT_R8G8_UNORM, "rg8_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.75f, 0.0f, 1.0f}},
        {.float32 = {64.0f / 255.0f, 191.0f / 255.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R8G8_SNORM, "rg8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 0.0f, 1.0f}},
        {.float32 = {64.0f / 127.0f, -64.0f / 127.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R8G8_UINT, "rg8_uint", NUMERIC_UINT,
        {.uint32 = {0x34u, 0xcdu, 0u, 1u}},
        {.uint32 = {0x34u, 0xcdu, 0u, 1u}}},
    {VK_FORMAT_R8G8_SINT, "rg8_sint", NUMERIC_SINT,
        {.int32 = {-2, 123, 0, 1}}, {.int32 = {-2, 123, 0, 1}}},
    {VK_FORMAT_A8B8G8R8_SNORM_PACK32, "rgba8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 1.0f, -1.0f}},
        {.float32 = {64.0f / 127.0f, -64.0f / 127.0f, 1.0f, -1.0f}}},
    {VK_FORMAT_A8B8G8R8_UINT_PACK32, "rgba8_uint", NUMERIC_UINT,
        {.uint32 = {0x12u, 0x34u, 0x56u, 0x78u}},
        {.uint32 = {0x12u, 0x34u, 0x56u, 0x78u}}},
    {VK_FORMAT_A8B8G8R8_SINT_PACK32, "rgba8_sint", NUMERIC_SINT,
        {.int32 = {-1, -2, 0x34, -128}},
        {.int32 = {-1, -2, 0x34, -128}}},
    {VK_FORMAT_A2B10G10R10_UINT_PACK32, "rgb10a2_uint", NUMERIC_UINT,
        {.uint32 = {0x123u, 0x234u, 0x345u, 2u}},
        {.uint32 = {0x123u, 0x234u, 0x345u, 2u}}},
    {VK_FORMAT_A2R10G10B10_UNORM_PACK32, "bgr10a2_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.5f, 0.75f, 1.0f}},
        {.float32 = {256.0f / 1023.0f, 512.0f / 1023.0f,
            767.0f / 1023.0f, 1.0f}}},
    {VK_FORMAT_R5G6B5_UNORM_PACK16, "r5g6b5_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {.float32 = {16.0f / 31.0f, 1.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_B5G6R5_UNORM_PACK16, "b5g6r5_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {.float32 = {16.0f / 31.0f, 1.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R5G5B5A1_UNORM_PACK16, "r5g5b5a1_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {.float32 = {16.0f / 31.0f, 1.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_A1R5G5B5_UNORM_PACK16, "a1r5g5b5_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {.float32 = {16.0f / 31.0f, 1.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT, "a4b4g4r4_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {.float32 = {8.0f / 15.0f, 1.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R4G4_UNORM_PACK8, "r4g4_unorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 1.0f, 0.0f, 1.0f}},
        {.float32 = {8.0f / 15.0f, 1.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, "rgb9e5_ufloat", NUMERIC_FLOAT,
        {.float32 = {1.0f, 0.5f, 0.25f, 1.0f}},
        {.float32 = {1.0f, 0.5f, 0.25f, 1.0f}}},
    {VK_FORMAT_R16_UNORM, "r16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.0f, 0.0f, 1.0f}},
        {.float32 = {16384.0f / 65535.0f, 0.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R16_SNORM, "r16_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 0.0f, 0.0f, 1.0f}},
        {.float32 = {16384.0f / 32767.0f, 0.0f, 0.0f, 1.0f}}},
    {VK_FORMAT_R16_UINT, "r16_uint", NUMERIC_UINT,
        {.uint32 = {0x1234u, 0u, 0u, 1u}},
        {.uint32 = {0x1234u, 0u, 0u, 1u}}},
    {VK_FORMAT_R16_SINT, "r16_sint", NUMERIC_SINT,
        {.int32 = {-2, 0, 0, 1}}, {.int32 = {-2, 0, 0, 1}}},
    {VK_FORMAT_R16G16_UNORM, "rg16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.75f, 0.0f, 1.0f}},
        {.float32 = {16384.0f / 65535.0f, 49151.0f / 65535.0f,
            0.0f, 1.0f}}},
    {VK_FORMAT_R16G16_SNORM, "rg16_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 0.0f, 1.0f}},
        {.float32 = {16384.0f / 32767.0f, -16384.0f / 32767.0f,
            0.0f, 1.0f}}},
    {VK_FORMAT_R16G16_UINT, "rg16_uint", NUMERIC_UINT,
        {.uint32 = {0x1234u, 0xabcdu, 0u, 1u}},
        {.uint32 = {0x1234u, 0xabcdu, 0u, 1u}}},
    {VK_FORMAT_R16G16_SINT, "rg16_sint", NUMERIC_SINT,
        {.int32 = {-2, 12345, 0, 1}}, {.int32 = {-2, 12345, 0, 1}}},
    {VK_FORMAT_R16G16B16A16_UNORM, "rgba16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.0f, 0.25f, 0.5f, 1.0f}},
        {.float32 = {0.0f, 16384.0f / 65535.0f,
            32768.0f / 65535.0f, 1.0f}}},
    {VK_FORMAT_R16G16B16A16_SNORM, "rgba16_snorm", NUMERIC_FLOAT,
        {.float32 = {-1.0f, -0.5f, 0.5f, 1.0f}},
        {.float32 = {-1.0f, -16384.0f / 32767.0f,
            16384.0f / 32767.0f, 1.0f}}},
    {VK_FORMAT_R16G16B16A16_UINT, "rgba16_uint", NUMERIC_UINT,
        {.uint32 = {0x0123u, 0x4567u, 0x89abu, 0xcdefu}},
        {.uint32 = {0x0123u, 0x4567u, 0x89abu, 0xcdefu}}},
    {VK_FORMAT_R16G16B16A16_SINT, "rgba16_sint", NUMERIC_SINT,
        {.int32 = {-1, -32768, 12345, -23456}},
        {.int32 = {-1, -32768, 12345, -23456}}},
    {VK_FORMAT_R32_UINT, "r32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x89abcdef), 0u, 0u, 1u}},
        {.uint32 = {UINT32_C(0x89abcdef), 0u, 0u, 1u}}},
    {VK_FORMAT_R32_SINT, "r32_sint", NUMERIC_SINT,
        {.int32 = {-987654321, 0, 0, 1}},
        {.int32 = {-987654321, 0, 0, 1}}},
    {VK_FORMAT_R32G32_UINT, "rg32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 1u}},
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 1u}}},
    {VK_FORMAT_R32G32_SINT, "rg32_sint", NUMERIC_SINT,
        {.int32 = {-1, INT32_MIN, 0, 1}},
        {.int32 = {-1, INT32_MIN, 0, 1}}},
    {VK_FORMAT_R32G32B32A32_UINT, "rgba32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531)}},
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531)}}},
    {VK_FORMAT_R32G32B32A32_SINT, "rgba32_sint", NUMERIC_SINT,
        {.int32 = {-1, INT32_MIN, 123456789, -987654321}},
        {.int32 = {-1, INT32_MIN, 123456789, -987654321}}},
};

static const RawTexel raw_texels[FORMAT_COUNT] = {
    {{UINT32_C(0x00000040), 0u, 0u, 0u}, 1u},
    {{UINT32_C(0x00000040), 0u, 0u, 0u}, 1u},
    {{UINT32_C(0x000000ab), 0u, 0u, 0u}, 1u},
    {{UINT32_C(0x000000fe), 0u, 0u, 0u}, 1u},
    {{UINT32_C(0x0000bf40), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x0000c040), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x0000cd34), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x00007bfe), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x817fc040), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0x78563412), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0x8034feff), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0xb458d123), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0xd00802ff), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0x000087e0), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x000007f0), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x000087c1), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x0000c3e0), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x0000f0f8), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x000000f8), 0u, 0u, 0u}, 1u},
    {{UINT32_C(0x81010100), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0x00004000), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x00004000), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x00001234), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0x0000fffe), 0u, 0u, 0u}, 2u},
    {{UINT32_C(0xbfff4000), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0xc0004000), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0xabcd1234), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0x3039fffe), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0x40000000), UINT32_C(0xffff8000), 0u, 0u}, 8u},
    {{UINT32_C(0xc0008001), UINT32_C(0x7fff4000), 0u, 0u}, 8u},
    {{UINT32_C(0x45670123), UINT32_C(0xcdef89ab), 0u, 0u}, 8u},
    {{UINT32_C(0x8000ffff), UINT32_C(0xa4603039), 0u, 0u}, 8u},
    {{UINT32_C(0x89abcdef), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0xc521974f), 0u, 0u, 0u}, 4u},
    {{UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 0u}, 8u},
    {{UINT32_C(0xffffffff), UINT32_C(0x80000000), 0u, 0u}, 8u},
    {{UINT32_C(0x01234567), UINT32_C(0x89abcdef),
        UINT32_C(0x13579bdf), UINT32_C(0xfdb97531)}, 16u},
    {{UINT32_C(0xffffffff), UINT32_C(0x80000000),
        UINT32_C(0x075bcd15), UINT32_C(0xc521974f)}, 16u},
};

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0u; i < properties.memoryTypeCount; ++i) {
        if ((compatible_types & (1u << i)) != 0u &&
            (properties.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
            return i;
    }
    return UINT32_MAX;
}

static VkResult create_test_image(VkPhysicalDevice physical, VkDevice device,
    const FormatCase *format_case, const RawTexel *raw_texel,
    TestImage *test_image)
{
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format_case->format,
        .extent = {1u, 1u, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VkResult result = vkCreateImage(device, &image_info, NULL,
        &test_image->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, test_image->image, &requirements);
    const uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    result = vkAllocateMemory(device, &allocation, NULL, &test_image->memory);
    if (result == VK_SUCCESS)
        result = vkBindImageMemory(device, test_image->image,
            test_image->memory, 0u);
    if (result != VK_SUCCESS)
        return result;

    uint8_t *mapped = NULL;
    result = vkMapMemory(device, test_image->memory, 0u, requirements.size,
        0u, (void **)&mapped);
    if (result != VK_SUCCESS)
        return result;
    memset(mapped, 0xa5, requirements.size);
    const VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, test_image->image, &subresource,
        &layout);
    memcpy(mapped + layout.offset, raw_texel->words, raw_texel->byte_count);
    const VkMappedMemoryRange mapped_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = test_image->memory,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    result = vkFlushMappedMemoryRanges(device, 1u, &mapped_range);
    vkUnmapMemory(device, test_image->memory);
    if (result != VK_SUCCESS)
        return result;
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = test_image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format_case->format,
        .components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        },
    };
    return vkCreateImageView(device, &view_info, NULL, &test_image->view);
}

static VkResult create_pipeline(VkDevice device, VkPipelineLayout layout,
    const ShaderCode *code, VkShaderModule *module, VkPipeline *pipeline)
{
    const VkShaderModuleCreateInfo module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code->size,
        .pCode = code->words,
    };
    VkResult result = vkCreateShaderModule(device, &module_info, NULL, module);
    if (result != VK_SUCCESS)
        return result;
    const VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = *module,
            .pName = "main",
        },
        .layout = layout,
    };
    return vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u,
        &pipeline_info, NULL, pipeline);
}

#if defined(OPENAGC_PROSPERO)
static void expected_words(const FormatCase *format_case, uint32_t words[4])
{
    if (format_case->numeric_class == NUMERIC_FLOAT) {
        memcpy(words, format_case->expected.float32, 4u * sizeof(uint32_t));
    } else if (format_case->numeric_class == NUMERIC_UINT) {
        memcpy(words, format_case->expected.uint32, 4u * sizeof(uint32_t));
    } else {
        memcpy(words, format_case->expected.int32, 4u * sizeof(uint32_t));
    }
}
#endif

static int run_probe(void)
{
    int status = 1;
    VkResult result;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkBuffer output = VK_NULL_HANDLE;
    VkDeviceMemory output_memory = VK_NULL_HANDLE;
    uint32_t *mapped = NULL;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule modules[PIPELINE_COUNT] = {VK_NULL_HANDLE};
    VkPipeline pipelines[PIPELINE_COUNT] = {VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    TestImage images[FORMAT_COUNT] = {0};

#define VK_TRY(expression) do { \
    result = (expression); \
    if (result != VK_SUCCESS) { \
        printf("format_sampling: %s failed (%d)\n", #expression, result); \
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
    if (physical_count != 1u)
        goto cleanup;
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
    VK_TRY(vkCreateDevice(physical, &device_info, NULL, &device));

    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        result = create_test_image(physical, device, &format_cases[i],
            &raw_texels[i], &images[i]);
        if (result != VK_SUCCESS) {
            printf("format_sampling: create %s failed (%d)\n",
                format_cases[i].name, result);
            goto cleanup;
        }
    }
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

    const VkDeviceSize output_size =
        FORMAT_COUNT * RESULT_WORDS * sizeof(uint32_t);
    const VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = output_size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_TRY(vkCreateBuffer(device, &buffer_info, NULL, &output));
    VkMemoryRequirements output_requirements;
    vkGetBufferMemoryRequirements(device, output, &output_requirements);
    const uint32_t output_memory_type = find_host_visible_memory_type(
        physical, output_requirements.memoryTypeBits);
    if (output_memory_type == UINT32_MAX)
        goto cleanup;
    const VkMemoryAllocateInfo output_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = output_requirements.size,
        .memoryTypeIndex = output_memory_type,
    };
    VK_TRY(vkAllocateMemory(device, &output_allocation, NULL, &output_memory));
    VK_TRY(vkBindBufferMemory(device, output, output_memory, 0u));
    VK_TRY(vkMapMemory(device, output_memory, 0u, output_requirements.size,
        0u, (void **)&mapped));
    memset(mapped, 0xa5, output_requirements.size);
    const VkMappedMemoryRange output_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = output_memory,
        .offset = 0u,
        .size = VK_WHOLE_SIZE,
    };
    VK_TRY(vkFlushMappedMemoryRanges(device, 1u, &output_range));

    const VkDescriptorSetLayoutBinding bindings[] = {
        {0u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1u,
            VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u,
            VK_SHADER_STAGE_COMPUTE_BIT, NULL},
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
    const ShaderCode shader_codes[PIPELINE_COUNT] = {
        {vulkan_ps5_format_sample_float_spv,
            sizeof(vulkan_ps5_format_sample_float_spv)},
        {vulkan_ps5_format_sample_uint_spv,
            sizeof(vulkan_ps5_format_sample_uint_spv)},
        {vulkan_ps5_format_sample_sint_spv,
            sizeof(vulkan_ps5_format_sample_sint_spv)},
    };
    for (uint32_t i = 0u; i < PIPELINE_COUNT; ++i)
        VK_TRY(create_pipeline(device, pipeline_layout, &shader_codes[i],
            &modules[i], &pipelines[i]));

    const VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FORMAT_COUNT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FORMAT_COUNT},
    };
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FORMAT_COUNT,
        .poolSizeCount = 2u,
        .pPoolSizes = pool_sizes,
    };
    VK_TRY(vkCreateDescriptorPool(device, &pool_info, NULL,
        &descriptor_pool));
    VkDescriptorSetLayout set_layouts[FORMAT_COUNT];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i)
        set_layouts[i] = set_layout;
    const VkDescriptorSetAllocateInfo set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = FORMAT_COUNT,
        .pSetLayouts = set_layouts,
    };
    VkDescriptorSet descriptor_sets[FORMAT_COUNT];
    VK_TRY(vkAllocateDescriptorSets(device, &set_allocate_info,
        descriptor_sets));
    VkDescriptorImageInfo descriptor_images[FORMAT_COUNT];
    VkDescriptorBufferInfo descriptor_buffers[FORMAT_COUNT];
    VkWriteDescriptorSet descriptor_writes[FORMAT_COUNT * 2u];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        descriptor_images[i] = (VkDescriptorImageInfo) {
            .sampler = sampler,
            .imageView = images[i].view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        descriptor_buffers[i] = (VkDescriptorBufferInfo) {
            .buffer = output,
            .offset = 0u,
            .range = output_size,
        };
        descriptor_writes[2u * i] = (VkWriteDescriptorSet) {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets[i],
            .dstBinding = 0u,
            .descriptorCount = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &descriptor_images[i],
        };
        descriptor_writes[2u * i + 1u] = (VkWriteDescriptorSet) {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets[i],
            .dstBinding = 1u,
            .descriptorCount = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &descriptor_buffers[i],
        };
    }
    vkUpdateDescriptorSets(device, FORMAT_COUNT * 2u, descriptor_writes,
        0u, NULL);

    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VK_TRY(vkCreateCommandPool(device, &command_pool_info, NULL,
        &command_pool));
    const VkCommandBufferAllocateInfo command_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = COMMAND_COUNT,
    };
    VkCommandBuffer commands[COMMAND_COUNT] = {VK_NULL_HANDLE};
    VK_TRY(vkAllocateCommandBuffers(device, &command_allocate_info, commands));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0u, 0u, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_TRY(vkCreateFence(device, &fence_info, NULL, &fence));

    VkImageMemoryBarrier to_sample[FORMAT_COUNT];
    const VkImageSubresourceRange image_range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        to_sample[i] = (VkImageMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].image,
            .subresourceRange = image_range,
        };
    }
    const VkBufferMemoryBarrier output_to_shader = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output,
        .offset = 0u,
        .size = output_size,
    };
    const VkBufferMemoryBarrier output_to_host = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = output,
        .offset = 0u,
        .size = output_size,
    };
    for (uint32_t command_index = 0u; command_index < COMMAND_COUNT;
         ++command_index) {
        VkCommandBuffer command = commands[command_index];
        const uint32_t first =
            command_index * FORMAT_COUNT / COMMAND_COUNT;
        const uint32_t end =
            (command_index + 1u) * FORMAT_COUNT / COMMAND_COUNT;
        const uint32_t count = end - first;
        VK_TRY(vkBeginCommandBuffer(command, &begin_info));
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, NULL,
            1u, &output_to_shader,
            count, &to_sample[first]);
        for (uint32_t i = first; i < first + count; ++i) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipelines[format_cases[i].numeric_class]);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline_layout, 0u, 1u, &descriptor_sets[i], 0u, NULL);
            vkCmdPushConstants(command, pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(i), &i);
            vkCmdDispatch(command, 1u, 1u, 1u);
        }
        vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 1u,
            &output_to_host, 0u, NULL);
        VK_TRY(vkEndCommandBuffer(command));
        const VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1u,
            .pCommandBuffers = &command,
        };
        VK_TRY(vkQueueSubmit(queue, 1u, &submit_info, fence));
        result = vkWaitForFences(device, 1u, &fence, VK_TRUE,
            UINT64_C(2000000000));
        if (result != VK_SUCCESS) {
            printf("format_sampling: two-second fence wait failed "
                "command=%u (%d)\n", command_index, result);
            goto cleanup;
        }
        if (command_index + 1u != COMMAND_COUNT)
            VK_TRY(vkResetFences(device, 1u, &fence));
    }
    VK_TRY(vkInvalidateMappedMemoryRanges(device, 1u, &output_range));

#if defined(OPENAGC_PROSPERO)
    uint32_t mismatch_count = 0u;
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        uint32_t expected[4];
        expected_words(&format_cases[i], expected);
        const uint32_t *actual = &mapped[i * RESULT_WORDS];
        if (memcmp(actual, expected, sizeof(expected)) != 0) {
            printf("format_sampling: mismatch format=%s "
                "got=%08x,%08x,%08x,%08x "
                "expected=%08x,%08x,%08x,%08x\n",
                format_cases[i].name,
                actual[0], actual[1], actual[2], actual[3],
                expected[0], expected[1], expected[2], expected[3]);
            ++mismatch_count;
        }
    }
    if (mismatch_count != 0u) {
        printf("format_sampling: mismatches=%u\n", mismatch_count);
        goto cleanup;
    }
    printf("format_sampling: PASS formats=%u components=%u exact-bits\n",
        FORMAT_COUNT, FORMAT_COUNT * RESULT_WORDS);
#else
    puts("format_sampling: PASS command recording");
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
        for (uint32_t i = 0u; i < PIPELINE_COUNT; ++i) {
            if (pipelines[i] != VK_NULL_HANDLE)
                vkDestroyPipeline(device, pipelines[i], NULL);
            if (modules[i] != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, modules[i], NULL);
        }
        if (pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, NULL);
        if (set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, set_layout, NULL);
        if (mapped != NULL)
            vkUnmapMemory(device, output_memory);
        if (output != VK_NULL_HANDLE)
            vkDestroyBuffer(device, output, NULL);
        if (output_memory != VK_NULL_HANDLE)
            vkFreeMemory(device, output_memory, NULL);
        if (sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, sampler, NULL);
        for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
            if (images[i].view != VK_NULL_HANDLE)
                vkDestroyImageView(device, images[i].view, NULL);
            if (images[i].image != VK_NULL_HANDLE)
                vkDestroyImage(device, images[i].image, NULL);
            if (images[i].memory != VK_NULL_HANDLE)
                vkFreeMemory(device, images[i].memory, NULL);
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
    puts("format_sampling: stage=start");
    const int status = run_probe();
    printf("format_sampling: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("format_sampling");
#endif
    return status;
}
