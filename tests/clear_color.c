#include <vulkan/vulkan.h>

#include "../src/vulkan_ps5_internal.h"

#include <assert.h>
#include <string.h>

static VkDevice create_device(VkInstance instance, VkPhysicalDevice *physical_out)
{
    uint32_t count = 1u;
    assert(vkEnumeratePhysicalDevices(instance, &count, physical_out) ==
        VK_SUCCESS);
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0u,
        .queueCount = 1u,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo create = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1u,
        .pQueueCreateInfos = &queue,
    };
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(*physical_out, &create, NULL, &device) == VK_SUCCESS);
    return device;
}

static void test_clear_pattern_packing(void)
{
    const VkClearColorValue clear = {
        .float32 = {0.5f, 1.0f, -1.0f, 2.0f},
    };
    uint32_t pattern[4];
    uint32_t words;
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x80808080u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8G8_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xff80ff80u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8_SNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x40404040u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8G8_SNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x7f407f40u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8G8B8A8_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xff00ff80u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_B8G8R8A8_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xff80ff00u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xc00ffe00u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_A8B8G8R8_SNORM_PACK32, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x7f807f40u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_A2R10G10B10_UNORM_PACK32, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xe00ffc00u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16_SFLOAT, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x38003800u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x80008000u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xffff8000u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16B16A16_UNORM, &clear, pattern, &words));
    assert(words == 2u && pattern[0] == 0xffff8000u &&
        pattern[1] == 0xffff0000u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16_SNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x40004000u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16_SNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x7fff4000u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16B16A16_SNORM, &clear, pattern, &words));
    assert(words == 2u && pattern[0] == 0x7fff4000u &&
        pattern[1] == 0x7fff8000u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16B16A16_SFLOAT, &clear, pattern, &words));
    assert(words == 2u && pattern[0] == 0x3c003800u &&
        pattern[1] == 0x4000bc00u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_B10G11R11_UFLOAT_PACK32, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x001e0380u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R32G32B32A32_SFLOAT, &clear, pattern, &words));
    assert(words == 4u && memcmp(pattern, clear.float32,
        sizeof(pattern)) == 0);
    const VkClearColorValue uint_clear = {
        .uint32 = {0x1234u, 0xabcdu, 0x5678u, 0x9abcu},
    };
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8_UINT, &uint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x34343434u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8G8_UINT, &uint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xcd34cd34u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_A8B8G8R8_UINT_PACK32, &uint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xbc78cd34u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_A2B10G10R10_UINT_PACK32, &uint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x278f3634u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16B16A16_UINT, &uint_clear, pattern, &words));
    assert(words == 2u && pattern[0] == 0xabcd1234u &&
        pattern[1] == 0x9abc5678u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16_UINT, &uint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x12341234u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16_UINT, &uint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xabcd1234u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R32_UINT, &uint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x1234u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R32G32_UINT, &uint_clear, pattern, &words));
    assert(words == 2u && pattern[0] == 0x1234u &&
        pattern[1] == 0xabcdu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R32G32B32A32_UINT, &uint_clear, pattern, &words));
    assert(words == 4u && memcmp(pattern, uint_clear.uint32,
        sizeof(pattern)) == 0);
    const VkClearColorValue sint_clear = {
        .int32 = {-1, -2, 0x1234, -32768},
    };
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8_SINT, &sint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xffffffffu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R8G8_SINT, &sint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xfefffeffu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_A8B8G8R8_SINT_PACK32, &sint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x0034feffu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16B16A16_SINT, &sint_clear, pattern, &words));
    assert(words == 2u && pattern[0] == 0xfffeffffu &&
        pattern[1] == 0x80001234u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16_SINT, &sint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xffffffffu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16G16_SINT, &sint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xfffeffffu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R32_SINT, &sint_clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xffffffffu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R32G32_SINT, &sint_clear, pattern, &words));
    assert(words == 2u && pattern[0] == 0xffffffffu &&
        pattern[1] == 0xfffffffeu);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R32G32B32A32_SINT, &sint_clear, pattern, &words));
    assert(words == 4u && memcmp(pattern, sint_clear.int32,
        sizeof(pattern)) == 0);
    assert(!vk_ps5_pack_clear_color(
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK, &clear, pattern, &words));
}

static void test_integer_color_image_views(void)
{
    const VkInstanceCreateInfo instance_create = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_create, NULL, &instance) == VK_SUCCESS);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = create_device(instance, &physical);
    const VkFormat formats[] = {
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8_SNORM,
        VK_FORMAT_R8_UINT,
        VK_FORMAT_R8_SINT,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8G8_SNORM,
        VK_FORMAT_R8G8_UINT,
        VK_FORMAT_R8G8_SINT,
        VK_FORMAT_A8B8G8R8_SNORM_PACK32,
        VK_FORMAT_A8B8G8R8_UINT_PACK32,
        VK_FORMAT_A8B8G8R8_SINT_PACK32,
        VK_FORMAT_A2B10G10R10_UINT_PACK32,
        VK_FORMAT_R16_UNORM,
        VK_FORMAT_R16_SNORM,
        VK_FORMAT_R16_UINT,
        VK_FORMAT_R16_SINT,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_R16G16_UINT,
        VK_FORMAT_R16G16_SINT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R16G16B16A16_SNORM,
        VK_FORMAT_R16G16B16A16_UINT,
        VK_FORMAT_R16G16B16A16_SINT,
        VK_FORMAT_R32_UINT,
        VK_FORMAT_R32_SINT,
        VK_FORMAT_R32G32_UINT,
        VK_FORMAT_R32G32_SINT,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_FORMAT_R32G32B32A32_SINT,
    };
    for (uint32_t i = 0u; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        const VkImageCreateInfo image_create = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = formats[i],
            .extent = {8u, 8u, 1u},
            .mipLevels = 1u,
            .arrayLayers = 1u,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VkImage image = VK_NULL_HANDLE;
        assert(vkCreateImage(device, &image_create, NULL, &image) ==
            VK_SUCCESS);
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, image, &requirements);
        const VkMemoryAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = 0u,
        };
        VkDeviceMemory memory = VK_NULL_HANDLE;
        assert(vkAllocateMemory(device, &allocation, NULL, &memory) ==
            VK_SUCCESS);
        assert(vkBindImageMemory(device, image, memory, 0u) == VK_SUCCESS);
        const VkImageViewCreateInfo view_create = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = formats[i],
            .subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u,
            },
        };
        VkImageView view = VK_NULL_HANDLE;
        assert(vkCreateImageView(device, &view_create, NULL, &view) ==
            VK_SUCCESS);
        vkDestroyImageView(device, view, NULL);
        vkDestroyImage(device, image, NULL);
        vkFreeMemory(device, memory, NULL);
    }
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
}

static void test_depth_stencil_pattern_packing(void)
{
    const VkClearDepthStencilValue clear = {0.5f, 0x123u};
    uint32_t pattern[4];
    uint32_t words;
    uint32_t plane;
    assert(vk_ps5_pack_depth_stencil_clear(VK_FORMAT_D16_UNORM,
        VK_IMAGE_ASPECT_DEPTH_BIT, &clear, pattern, &words, &plane));
    assert(words == 1u && plane == 0u && pattern[0] == 0x80008000u);
    assert(vk_ps5_pack_depth_stencil_clear(VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_ASPECT_DEPTH_BIT, &clear, pattern, &words, &plane));
    assert(words == 1u && plane == 0u && pattern[0] == 0x3f000000u);
    assert(vk_ps5_pack_depth_stencil_clear(VK_FORMAT_D16_UNORM_S8_UINT,
        VK_IMAGE_ASPECT_STENCIL_BIT, &clear, pattern, &words, &plane));
    assert(words == 1u && plane == 1u && pattern[0] == 0x23232323u);
    assert(vk_ps5_pack_depth_stencil_clear(VK_FORMAT_S8_UINT,
        VK_IMAGE_ASPECT_STENCIL_BIT, &clear, pattern, &words, &plane));
    assert(words == 1u && plane == 0u && pattern[0] == 0x23232323u);
    assert(!vk_ps5_pack_depth_stencil_clear(VK_FORMAT_D16_UNORM,
        VK_IMAGE_ASPECT_STENCIL_BIT, &clear, pattern, &words, &plane));
    const VkClearDepthStencilValue invalid_depth = {1.01f, 0u};
    assert(!vk_ps5_pack_depth_stencil_clear(VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_ASPECT_DEPTH_BIT, &invalid_depth, pattern, &words, &plane));
}

static void test_multidword_clear_recording(void)
{
    const VkInstanceCreateInfo instance_create = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_create, NULL, &instance) == VK_SUCCESS);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = create_device(instance, &physical);

    const VkImageCreateInfo image_create = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .extent = {64u, 32u, 1u},
        .mipLevels = 3u,
        .arrayLayers = 70u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &image_create, NULL, &image) == VK_SUCCESS);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = 0u,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &allocation, NULL, &memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device, image, memory, 0u) == VK_SUCCESS);

    const VkCommandPoolCreateInfo pool_create = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    assert(vkCreateCommandPool(device, &pool_create, NULL, &pool) ==
        VK_SUCCESS);
    const VkCommandBufferAllocateInfo command_allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command = VK_NULL_HANDLE;
    assert(vkAllocateCommandBuffers(device, &command_allocate, &command) ==
        VK_SUCCESS);
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    assert(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS);
    const VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0u,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, VK_REMAINING_MIP_LEVELS,
            0u, VK_REMAINING_ARRAY_LAYERS,
        },
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL,
        1u, &barrier);
    const VkClearColorValue clear = {
        .float32 = {0.25f, -2.0f, 65504.0f, 1.0f},
    };
    const VkImageSubresourceRange range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 1u, 1u, 0u, 70u,
    };
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_GENERAL,
        &clear, 1u, &range);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_dispatch_count(command) == 1u);

    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
}

static void test_depth_stencil_clear_recording(void)
{
    const VkInstanceCreateInfo instance_create = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_create, NULL, &instance) == VK_SUCCESS);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = create_device(instance, &physical);

    const VkImageCreateInfo image_create = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
        .extent = {64u, 32u, 1u},
        .mipLevels = 1u,
        .arrayLayers = 70u,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &image_create, NULL, &image) == VK_SUCCESS);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    assert((requirements.memoryTypeBits & 0x2u) != 0u);
    const VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = 1u,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &allocation, NULL, &memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device, image, memory, 0u) == VK_SUCCESS);

    const VkCommandPoolCreateInfo pool_create = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0u,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    assert(vkCreateCommandPool(device, &pool_create, NULL, &pool) ==
        VK_SUCCESS);
    const VkCommandBufferAllocateInfo command_allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1u,
    };
    VkCommandBuffer command = VK_NULL_HANDLE;
    assert(vkAllocateCommandBuffers(device, &command_allocate, &command) ==
        VK_SUCCESS);
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    assert(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS);
    const VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0u,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            0u, VK_REMAINING_MIP_LEVELS, 0u, VK_REMAINING_ARRAY_LAYERS,
        },
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL,
        1u, &barrier);
    const VkClearDepthStencilValue clear = {0.25f, 0x5au};
    const VkImageSubresourceRange range = {
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        0u, 1u, 0u, 70u,
    };
    vkCmdClearDepthStencilImage(command, image, VK_IMAGE_LAYOUT_GENERAL,
        &clear, 1u, &range);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    assert(vk_ps5_command_buffer_native_dispatch_count(command) == 2u);

    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
}

int main(void)
{
    test_clear_pattern_packing();
    test_integer_color_image_views();
    test_depth_stencil_pattern_packing();
    test_multidword_clear_recording();
    test_depth_stencil_clear_recording();
    return 0;
}
