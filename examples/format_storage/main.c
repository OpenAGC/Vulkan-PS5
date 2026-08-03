#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vulkan_ps5_format_storage_float_spv.h"
#include "vulkan_ps5_format_storage_sint_spv.h"
#include "vulkan_ps5_format_storage_uint_spv.h"

#include "../system_service_exit.h"

#define IMAGE_WIDTH 4u
#define IMAGE_HEIGHT 4u
#define FORMAT_COUNT 30u
#define TOTAL_IMAGE_COUNT (FORMAT_COUNT + 1u)
#define PIPELINE_COUNT 3u

typedef enum NumericClass {
    NUMERIC_FLOAT = 0,
    NUMERIC_UINT = 1,
    NUMERIC_SINT = 2,
} NumericClass;

typedef struct FormatCase {
    VkFormat format;
    const char *name;
    NumericClass numeric_class;
    VkClearColorValue value;
    uint32_t expected[4];
    uint32_t byte_count;
} FormatCase;

typedef struct TestImage {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize allocation_size;
    VkDeviceSize image_offset;
    VkDeviceSize row_pitch;
} TestImage;

typedef struct ShaderCode {
    const uint32_t *words;
    size_t size;
} ShaderCode;

static const FormatCase format_cases[FORMAT_COUNT] = {
    {VK_FORMAT_R8_UNORM, "r8_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x40), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8_SNORM, "r8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x40), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8_UINT, "r8_uint", NUMERIC_UINT,
        {.uint32 = {0xabu, 0u, 0u, 1u}},
        {UINT32_C(0xab), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8_SINT, "r8_sint", NUMERIC_SINT,
        {.int32 = {-2, 0, 0, 1}},
        {UINT32_C(0xfe), 0u, 0u, 0u}, 1u},
    {VK_FORMAT_R8G8_UNORM, "rg8_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.75f, 0.0f, 1.0f}},
        {UINT32_C(0xbf40), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R8G8_SNORM, "rg8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 0.0f, 1.0f}},
        {UINT32_C(0xc040), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R8G8_UINT, "rg8_uint", NUMERIC_UINT,
        {.uint32 = {0x34u, 0xcdu, 0u, 1u}},
        {UINT32_C(0xcd34), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R8G8_SINT, "rg8_sint", NUMERIC_SINT,
        {.int32 = {-2, 123, 0, 1}},
        {UINT32_C(0x7bfe), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_A8B8G8R8_SNORM_PACK32, "rgba8_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 1.0f, -1.0f}},
        {UINT32_C(0x817fc040), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_A8B8G8R8_UINT_PACK32, "rgba8_uint", NUMERIC_UINT,
        {.uint32 = {0x12u, 0x34u, 0x56u, 0x78u}},
        {UINT32_C(0x78563412), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_A8B8G8R8_SINT_PACK32, "rgba8_sint", NUMERIC_SINT,
        {.int32 = {-1, -2, 0x34, -128}},
        {UINT32_C(0x8034feff), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_A2B10G10R10_UINT_PACK32, "rgb10a2_uint", NUMERIC_UINT,
        {.uint32 = {0x123u, 0x234u, 0x345u, 2u}},
        {UINT32_C(0xb458d123), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16_UNORM, "r16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x4000), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16_SNORM, "r16_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, 0.0f, 0.0f, 1.0f}},
        {UINT32_C(0x4000), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16_UINT, "r16_uint", NUMERIC_UINT,
        {.uint32 = {0x1234u, 0u, 0u, 1u}},
        {UINT32_C(0x1234), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16_SINT, "r16_sint", NUMERIC_SINT,
        {.int32 = {-2, 0, 0, 1}},
        {UINT32_C(0xfffe), 0u, 0u, 0u}, 2u},
    {VK_FORMAT_R16G16_UNORM, "rg16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.25f, 0.75f, 0.0f, 1.0f}},
        {UINT32_C(0xbfff4000), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16_SNORM, "rg16_snorm", NUMERIC_FLOAT,
        {.float32 = {0.5f, -0.5f, 0.0f, 1.0f}},
        {UINT32_C(0xc0004000), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16_UINT, "rg16_uint", NUMERIC_UINT,
        {.uint32 = {0x1234u, 0xabcdu, 0u, 1u}},
        {UINT32_C(0xabcd1234), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16_SINT, "rg16_sint", NUMERIC_SINT,
        {.int32 = {-2, 12345, 0, 1}},
        {UINT32_C(0x3039fffe), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R16G16B16A16_UNORM, "rgba16_unorm", NUMERIC_FLOAT,
        {.float32 = {0.0f, 0.25f, 0.5f, 1.0f}},
        {UINT32_C(0x40000000), UINT32_C(0xffff8000), 0u, 0u}, 8u},
    {VK_FORMAT_R16G16B16A16_SNORM, "rgba16_snorm", NUMERIC_FLOAT,
        {.float32 = {-1.0f, -0.5f, 0.5f, 1.0f}},
        {UINT32_C(0xc0008001), UINT32_C(0x7fff4000), 0u, 0u}, 8u},
    {VK_FORMAT_R16G16B16A16_UINT, "rgba16_uint", NUMERIC_UINT,
        {.uint32 = {0x0123u, 0x4567u, 0x89abu, 0xcdefu}},
        {UINT32_C(0x45670123), UINT32_C(0xcdef89ab), 0u, 0u}, 8u},
    {VK_FORMAT_R16G16B16A16_SINT, "rgba16_sint", NUMERIC_SINT,
        {.int32 = {-1, -32768, 12345, -23456}},
        {UINT32_C(0x8000ffff), UINT32_C(0xa4603039), 0u, 0u}, 8u},
    {VK_FORMAT_R32_UINT, "r32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x89abcdef), 0u, 0u, 1u}},
        {UINT32_C(0x89abcdef), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R32_SINT, "r32_sint", NUMERIC_SINT,
        {.int32 = {-987654321, 0, 0, 1}},
        {UINT32_C(0xc521974f), 0u, 0u, 0u}, 4u},
    {VK_FORMAT_R32G32_UINT, "rg32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 1u}},
        {UINT32_C(0x01234567), UINT32_C(0x89abcdef), 0u, 0u}, 8u},
    {VK_FORMAT_R32G32_SINT, "rg32_sint", NUMERIC_SINT,
        {.int32 = {-1, INT32_MIN, 0, 1}},
        {UINT32_C(0xffffffff), UINT32_C(0x80000000), 0u, 0u}, 8u},
    {VK_FORMAT_R32G32B32A32_UINT, "rgba32_uint", NUMERIC_UINT,
        {.uint32 = {UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531)}},
        {UINT32_C(0x01234567), UINT32_C(0x89abcdef),
            UINT32_C(0x13579bdf), UINT32_C(0xfdb97531)}, 16u},
    {VK_FORMAT_R32G32B32A32_SINT, "rgba32_sint", NUMERIC_SINT,
        {.int32 = {-1, INT32_MIN, 123456789, -987654321}},
        {UINT32_C(0xffffffff), UINT32_C(0x80000000),
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
    const FormatCase *format_case, TestImage *test_image)
{
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical, format_case->format,
        &properties);
    if ((properties.linearTilingFeatures &
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0u)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format_case->format,
        .extent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
    if (result == VK_SUCCESS)
        result = vkMapMemory(device, test_image->memory, 0u,
            requirements.size, 0u, (void **)&test_image->mapped);
    if (result != VK_SUCCESS)
        return result;

    test_image->allocation_size = requirements.size;
    memset(test_image->mapped, 0xa5, requirements.size);
    const VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(device, test_image->image, &subresource,
        &layout);
    test_image->image_offset = layout.offset;
    test_image->row_pitch = layout.rowPitch;

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

static VkResult create_extended_optimal_image(VkPhysicalDevice physical,
    VkDevice device, TestImage *test_image)
{
    const VkFormat view_formats[] = {
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        VK_FORMAT_A8B8G8R8_SNORM_PACK32,
    };
    const VkImageFormatListCreateInfo format_list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2u,
        .pViewFormats = view_formats,
    };
    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &format_list,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        .extent = {480u, 480u, 1u},
        .mipLevels = 1u,
        .arrayLayers = 1u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VkPhysicalDeviceImageFormatInfo2 format_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &format_list,
        .format = image_info.format,
        .type = image_info.imageType,
        .tiling = image_info.tiling,
        .usage = image_info.usage,
        .flags = image_info.flags,
    };
    VkImageFormatProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
    };
    VkResult result = vkGetPhysicalDeviceImageFormatProperties2(physical,
        &format_info, &properties);
    if (result != VK_SUCCESS)
        return result;
    result = vkCreateImage(device, &image_info, NULL, &test_image->image);
    if (result != VK_SUCCESS)
        return result;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, test_image->image, &requirements);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0u; i < 32u; ++i) {
        if (requirements.memoryTypeBits & (1u << i)) {
            memory_type = i;
            break;
        }
    }
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
    const VkImageViewUsageCreateInfo view_usage = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT,
    };
    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &view_usage,
        .image = test_image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_A8B8G8R8_SNORM_PACK32,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
        },
    };
    if (result == VK_SUCCESS)
        result = vkCreateImageView(device, &view_info, NULL,
            &test_image->view);
    return result;
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

static int run_probe(void)
{
    int status = 1;
    VkResult result;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkShaderModule modules[PIPELINE_COUNT] = {VK_NULL_HANDLE};
    VkPipeline pipelines[PIPELINE_COUNT] = {VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    TestImage images[FORMAT_COUNT] = {0};
    TestImage extended_image = {0};

#define VK_TRY(expression) do { \
    result = (expression); \
    if (result != VK_SUCCESS) { \
        printf("format_storage: %s failed (%d)\n", #expression, result); \
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

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical, &features);
    if (!features.shaderStorageImageWriteWithoutFormat) {
        puts("format_storage: shaderStorageImageWriteWithoutFormat missing");
        goto cleanup;
    }
    const VkPhysicalDeviceFeatures requested_features = {
        .shaderStorageImageWriteWithoutFormat = VK_TRUE,
    };
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
        .pEnabledFeatures = &requested_features,
    };
    VK_TRY(vkCreateDevice(physical, &device_info, NULL, &device));

    VkMappedMemoryRange memory_ranges[FORMAT_COUNT];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        result = create_test_image(physical, device, &format_cases[i],
            &images[i]);
        if (result != VK_SUCCESS) {
            printf("format_storage: create %s failed (%d)\n",
                format_cases[i].name, result);
            goto cleanup;
        }
        memory_ranges[i] = (VkMappedMemoryRange) {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = images[i].memory,
            .offset = 0u,
            .size = VK_WHOLE_SIZE,
        };
    }
    VK_TRY(create_extended_optimal_image(physical, device,
        &extended_image));
    VK_TRY(vkFlushMappedMemoryRanges(device, FORMAT_COUNT, memory_ranges));

    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0u,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1u,
        .pBindings = &binding,
    };
    VK_TRY(vkCreateDescriptorSetLayout(device, &set_layout_info, NULL,
        &set_layout));
    const VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0u,
        .size = sizeof(VkClearColorValue),
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
        {vulkan_ps5_format_storage_float_spv,
            sizeof(vulkan_ps5_format_storage_float_spv)},
        {vulkan_ps5_format_storage_uint_spv,
            sizeof(vulkan_ps5_format_storage_uint_spv)},
        {vulkan_ps5_format_storage_sint_spv,
            sizeof(vulkan_ps5_format_storage_sint_spv)},
    };
    for (uint32_t i = 0u; i < PIPELINE_COUNT; ++i)
        VK_TRY(create_pipeline(device, pipeline_layout, &shader_codes[i],
            &modules[i], &pipelines[i]));

    const VkDescriptorPoolSize pool_size = {
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, TOTAL_IMAGE_COUNT,
    };
    const VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = TOTAL_IMAGE_COUNT,
        .poolSizeCount = 1u,
        .pPoolSizes = &pool_size,
    };
    VK_TRY(vkCreateDescriptorPool(device, &pool_info, NULL,
        &descriptor_pool));
    VkDescriptorSetLayout set_layouts[TOTAL_IMAGE_COUNT];
    for (uint32_t i = 0u; i < TOTAL_IMAGE_COUNT; ++i)
        set_layouts[i] = set_layout;
    const VkDescriptorSetAllocateInfo set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = TOTAL_IMAGE_COUNT,
        .pSetLayouts = set_layouts,
    };
    VkDescriptorSet descriptor_sets[TOTAL_IMAGE_COUNT];
    VK_TRY(vkAllocateDescriptorSets(device, &set_allocate_info,
        descriptor_sets));
    VkDescriptorImageInfo descriptor_images[TOTAL_IMAGE_COUNT];
    VkWriteDescriptorSet descriptor_writes[TOTAL_IMAGE_COUNT];
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        descriptor_images[i] = (VkDescriptorImageInfo) {
            .imageView = images[i].view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        descriptor_writes[i] = (VkWriteDescriptorSet) {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets[i],
            .dstBinding = 0u,
            .descriptorCount = 1u,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &descriptor_images[i],
        };
    }
    descriptor_images[FORMAT_COUNT] = (VkDescriptorImageInfo) {
        .imageView = extended_image.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    descriptor_writes[FORMAT_COUNT] = (VkWriteDescriptorSet) {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_sets[FORMAT_COUNT],
        .dstBinding = 0u,
        .descriptorCount = 1u,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &descriptor_images[FORMAT_COUNT],
    };
    vkUpdateDescriptorSets(device, TOTAL_IMAGE_COUNT, descriptor_writes,
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
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command = VK_NULL_HANDLE;
    VK_TRY(vkAllocateCommandBuffers(device, &command_allocate_info, &command));
    const VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_TRY(vkBeginCommandBuffer(command, &begin_info));

    VkImageMemoryBarrier to_shader[TOTAL_IMAGE_COUNT];
    VkImageMemoryBarrier to_host[FORMAT_COUNT];
    const VkImageSubresourceRange range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
    };
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        to_shader[i] = (VkImageMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            /* The first case covers Vulkan's legal broad memory scope.  The
             * compute descriptor reflection must select ShaderWrite at the
             * concrete point of consumption instead of the barrier guessing
             * that GENERAL means storage-image use. */
            .dstAccessMask = i == 0u ? VK_ACCESS_MEMORY_WRITE_BIT :
                VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].image,
            .subresourceRange = range,
        };
        to_host[i] = (VkImageMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i].image,
            .subresourceRange = range,
        };
    }
    to_shader[FORMAT_COUNT] = (VkImageMemoryBarrier) {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = extended_image.image,
        .subresourceRange = range,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, NULL, 0u, NULL,
        TOTAL_IMAGE_COUNT, to_shader);
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelines[format_cases[i].numeric_class]);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipeline_layout, 0u, 1u, &descriptor_sets[i], 0u, NULL);
        vkCmdPushConstants(command, pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(VkClearColorValue),
            &format_cases[i].value);
        vkCmdDispatch(command, 1u, 1u, 1u);
    }
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelines[NUMERIC_FLOAT]);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_layout, 0u, 1u, &descriptor_sets[FORMAT_COUNT], 0u, NULL);
    vkCmdPushConstants(command, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
        0u, sizeof(VkClearColorValue), &format_cases[8].value);
    vkCmdDispatch(command, 1u, 1u, 1u);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 0u, NULL,
        FORMAT_COUNT, to_host);
    VK_TRY(vkEndCommandBuffer(command));

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0u, 0u, &queue);
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
        printf("format_storage: two-second fence wait failed (%d)\n", result);
        goto cleanup;
    }
    VK_TRY(vkInvalidateMappedMemoryRanges(device, FORMAT_COUNT,
        memory_ranges));

#if defined(OPENAGC_PROSPERO)
    uint32_t checked = 0u;
    for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
        for (uint32_t y = 0u; y < IMAGE_HEIGHT; ++y) {
            const uint8_t *row = images[i].mapped + images[i].image_offset +
                y * images[i].row_pitch;
            for (uint32_t x = 0u; x < IMAGE_WIDTH; ++x) {
                const uint8_t *pixel = row + x * format_cases[i].byte_count;
                if (memcmp(pixel, format_cases[i].expected,
                    format_cases[i].byte_count) != 0) {
                    uint32_t actual[4] = {0};
                    memcpy(actual, pixel, format_cases[i].byte_count);
                    printf("format_storage: mismatch format=%s x=%u y=%u "
                        "got=%08x,%08x,%08x,%08x\n",
                        format_cases[i].name, x, y, actual[0], actual[1],
                        actual[2], actual[3]);
                    goto cleanup;
                }
                ++checked;
            }
        }
    }
    printf("format_storage: PASS formats=%u pixels=%u exact-bits\n",
        FORMAT_COUNT, checked);
    puts("format_storage: EXTENDED_OPTIMAL PASS format=a8b8g8r8_unorm "
         "view=a8b8g8r8_snorm extent=480x480 usage=0x1f flags=0x108");
#else
    puts("format_storage: PASS command recording");
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
        for (uint32_t i = 0u; i < FORMAT_COUNT; ++i) {
            if (images[i].view != VK_NULL_HANDLE)
                vkDestroyImageView(device, images[i].view, NULL);
            if (images[i].mapped != NULL)
                vkUnmapMemory(device, images[i].memory);
            if (images[i].image != VK_NULL_HANDLE)
                vkDestroyImage(device, images[i].image, NULL);
            if (images[i].memory != VK_NULL_HANDLE)
                vkFreeMemory(device, images[i].memory, NULL);
        }
        if (extended_image.view != VK_NULL_HANDLE)
            vkDestroyImageView(device, extended_image.view, NULL);
        if (extended_image.image != VK_NULL_HANDLE)
            vkDestroyImage(device, extended_image.image, NULL);
        if (extended_image.memory != VK_NULL_HANDLE)
            vkFreeMemory(device, extended_image.memory, NULL);
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
    puts("format_storage: stage=start");
    const int status = run_probe();
    printf("format_storage: stage=exit status=%d\n", status);
#if defined(OPENAGC_PROSPERO)
    vulkan_ps5_system_service_exit("format_storage");
#endif
    return status;
}
