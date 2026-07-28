#include <cstdint>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#if defined(OPENAGC_PROSPERO)
extern "C" int sceSystemServiceGetAppStatus(void *status);
extern "C" int sceSystemServiceKillApp(
    int app_id, int how, int reason, int core_dump);
extern "C" int sceKernelUsleep(unsigned int microseconds);

[[noreturn]] static void prospero_clean_exit()
{
    uint32_t status[0x100 / sizeof(uint32_t)]{};
    const int status_result = sceSystemServiceGetAppStatus(status);
    uint32_t app_id = status[2];
    if (app_id < 0x10u || app_id == UINT32_MAX)
        app_id = status[0];
    if (status_result != 0 || app_id < 0x10u || app_id == UINT32_MAX) {
        std::printf("eden-vma: clean-exit status failure result=0x%x app=0x%x\n",
                    status_result, app_id);
        std::fflush(nullptr);
        for (;;)
            sceKernelUsleep(1000000);
    }

    std::fflush(nullptr);
    const int result = sceSystemServiceKillApp((int)app_id, 0, 0, 0);
    std::printf("eden-vma: clean-exit unexpectedly returned result=0x%x\n",
                result);
    std::fflush(nullptr);
    for (;;)
        sceKernelUsleep(1000000);
}
#endif

struct BufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
    VkMemoryPropertyFlags properties = 0;
};

static constexpr VmaMemoryUsage manual_host_usage =
    VMA_VERSION >= VK_MAKE_VERSION(3, 4, 0) ?
    VMA_MEMORY_USAGE_UNKNOWN : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

static BufferAllocation create_buffer(
    VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
    VmaMemoryUsage memory_usage, VmaAllocationCreateFlags flags,
    VkMemoryPropertyFlags preferred, uint32_t memory_type_bits = 0)
{
    const VkBufferCreateInfo buffer_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        size,
        usage,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
    };
    const VmaAllocationCreateInfo allocation_info = {
        flags | VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        memory_usage,
        0,
        preferred,
        memory_type_bits,
        VK_NULL_HANDLE,
        nullptr,
        0.0f,
    };
    BufferAllocation result;
    assert(vmaCreateBuffer(allocator, &buffer_info, &allocation_info,
                           &result.buffer, &result.allocation,
                           &result.info) == VK_SUCCESS);
    vmaGetAllocationMemoryProperties(
        allocator, result.allocation, &result.properties);
    return result;
}

static void destroy_buffer(VmaAllocator allocator, BufferAllocation &buffer)
{
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

int main()
{
    const VkApplicationInfo application = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "vulkan-ps5-eden-vma",
        1,
        "Vulkan-PS5",
        1,
        VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo instance_info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &application,
        0,
        nullptr,
        0,
        nullptr,
    };
#ifdef VULKAN_PS5_ENABLE_VALIDATION
    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    instance_info.enabledLayerCount = 1;
    instance_info.ppEnabledLayerNames = &validation_layer;
#endif
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_info, nullptr, &instance) == VK_SUCCESS);

    uint32_t physical_count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, &physical) ==
           VK_SUCCESS);
    assert(physical_count == 1 && physical != VK_NULL_HANDLE);

    float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        0,
        1,
        &priority,
    };
    const VkDeviceCreateInfo device_info = {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,
        1,
        &queue_info,
        0,
        nullptr,
        0,
        nullptr,
        nullptr,
    };
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, nullptr, &device) == VK_SUCCESS);

    VkPhysicalDeviceProperties physical_properties{};
    vkGetPhysicalDeviceProperties(physical, &physical_properties);
    const VkDeviceSize preferred_block_size =
        physical_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ?
        64ull * 1024ull * 1024ull : 256ull * 1024ull * 1024ull;
    const VmaVulkanFunctions functions = {
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };
    const VmaAllocatorCreateInfo allocator_info = {
        .flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT,
        .physicalDevice = physical,
        .device = device,
        .preferredLargeHeapBlockSize = preferred_block_size,
        .pVulkanFunctions = &functions,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_1,
    };
    VmaAllocator allocator = VK_NULL_HANDLE;
    assert(vmaCreateAllocator(&allocator_info, &allocator) == VK_SUCCESS);

    auto upload = create_buffer(
        allocator, 4096, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VMA_ALLOCATION_CREATE_MAPPED_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(upload.info.pMappedData != nullptr);
    assert(upload.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    std::memset(upload.info.pMappedData, 0x5a, 4096);
    assert(vmaFlushAllocation(allocator, upload.allocation, 0, 4096) ==
           VK_SUCCESS);

    auto download = create_buffer(
        allocator, 4096, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VMA_ALLOCATION_CREATE_MAPPED_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(download.info.pMappedData != nullptr);
    assert(download.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    assert(vmaInvalidateAllocation(allocator, download.allocation, 0, 4096) ==
           VK_SUCCESS);

    auto stream = create_buffer(
        allocator, 4096,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        VMA_ALLOCATION_CREATE_MAPPED_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    assert(stream.info.pMappedData != nullptr);
    assert(stream.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    assert(stream.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    auto device_local = create_buffer(
        allocator, 65536,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    assert(device_local.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    std::array<BufferAllocation, 3> blocks;
    for (auto &block : blocks) {
        block = create_buffer(
            allocator, 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            VMA_ALLOCATION_CREATE_MAPPED_BIT |
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    bool found_suballocation = false;
    for (size_t i = 0; i < blocks.size(); ++i) {
        for (size_t j = i + 1; j < blocks.size(); ++j) {
            found_suballocation |=
                blocks[i].info.deviceMemory == blocks[j].info.deviceMemory &&
                blocks[i].info.offset != blocks[j].info.offset;
        }
    }
    assert(found_suballocation);

    const VkImageCreateInfo image_info = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8G8B8A8_UNORM,
        {64, 64, 1},
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo image_allocation_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation image_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocated_image_info{};
    assert(vmaCreateImage(allocator, &image_info, &image_allocation_info,
                          &image, &image_allocation,
                          &allocated_image_info) == VK_SUCCESS);

    const VkBufferCreateInfo raw_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        8192,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
    };
    VkBuffer raw_buffer = VK_NULL_HANDLE;
    assert(vkCreateBuffer(device, &raw_info, nullptr, &raw_buffer) == VK_SUCCESS);
    const VmaAllocationCreateInfo raw_allocation_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = manual_host_usage,
        .preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    VmaAllocation raw_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo raw_allocation_result{};
    assert(vmaAllocateMemoryForBuffer(
               allocator, raw_buffer, &raw_allocation_info, &raw_allocation,
               &raw_allocation_result) == VK_SUCCESS);
    assert(vmaBindBufferMemory2(
               allocator, raw_allocation, 0, raw_buffer, nullptr) == VK_SUCCESS);
    assert(raw_allocation_result.pMappedData != nullptr);

    VkMemoryRequirements raw_requirements{};
    vkGetBufferMemoryRequirements(device, raw_buffer, &raw_requirements);
    VmaAllocation requirements_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo requirements_result{};
    VmaAllocationCreateInfo requirements_info = raw_allocation_info;
    requirements_info.memoryTypeBits = raw_requirements.memoryTypeBits;
    assert(vmaAllocateMemory(
               allocator, &raw_requirements, &requirements_info,
               &requirements_allocation, &requirements_result) == VK_SUCCESS);
    assert(requirements_result.pMappedData != nullptr);

    vmaFreeMemory(allocator, requirements_allocation);
    vkDestroyBuffer(device, raw_buffer, nullptr);
    vmaFreeMemory(allocator, raw_allocation);
    vmaDestroyImage(allocator, image, image_allocation);
    for (auto &block : blocks)
        destroy_buffer(allocator, block);
    destroy_buffer(allocator, device_local);
    destroy_buffer(allocator, stream);
    destroy_buffer(allocator, download);
    destroy_buffer(allocator, upload);

    VmaTotalStatistics statistics{};
    vmaCalculateStatistics(allocator, &statistics);
    assert(statistics.total.statistics.allocationCount == 0);
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::printf(
        "eden-vma: PASS VMA=%u.%u.%u dynamic-functions upload download stream device-local image manual-bind suballocation\n",
        VK_VERSION_MAJOR(VMA_VERSION), VK_VERSION_MINOR(VMA_VERSION),
        VK_VERSION_PATCH(VMA_VERSION));
#if defined(OPENAGC_PROSPERO)
    prospero_clean_exit();
#endif
    return 0;
}
