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
        VK_FORMAT_R8G8B8A8_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xff00ff80u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_B8G8R8A8_UNORM, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xff80ff00u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0xc00ffe00u);
    assert(vk_ps5_pack_clear_color(
        VK_FORMAT_R16_SFLOAT, &clear, pattern, &words));
    assert(words == 1u && pattern[0] == 0x38003800u);
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
    assert(!vk_ps5_pack_clear_color(
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK, &clear, pattern, &words));
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

int main(void)
{
    test_clear_pattern_packing();
    test_multidword_clear_recording();
    return 0;
}
