#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

static atomic_uint validation_messages;

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *user) {
    (void)types; (void)data; (void)user;
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT))
        atomic_fetch_add(&validation_messages, 1);
    return VK_FALSE;
}

static uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t bits,
                                 VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    assert(0 && "memory type not found");
    return 0;
}

int main(void) {
    atomic_init(&validation_messages, 0);
    const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char *extensions[] = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME,
    };
    VkDebugUtilsMessengerCreateInfoEXT debug_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vulkan-ps5-vvl",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &debug_info,
        .pApplicationInfo = &app,
        .enabledLayerCount = 1,
        .ppEnabledLayerNames = layers,
        .enabledExtensionCount = 3,
        .ppEnabledExtensionNames = extensions,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);

    uint32_t count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &count, &physical) == VK_SUCCESS);
    VkPhysicalDeviceIDProperties id = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
    };
    VkPhysicalDeviceMaintenance3Properties maintenance = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES,
        .pNext = &id,
    };
    VkPhysicalDeviceProperties2 properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &maintenance,
    };
    vkGetPhysicalDeviceProperties2(physical, &properties);
    assert(properties.properties.vendorID == 0x1002);
    assert(maintenance.maxMemoryAllocationSize != 0);

    VkHeadlessSurfaceCreateInfoEXT surface_info = {
        .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    assert(vkCreateHeadlessSurfaceEXT(instance, &surface_info, NULL, &surface) ==
           VK_SUCCESS);
    VkSurfaceCapabilitiesKHR surface_capabilities;
    assert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physical, surface, &surface_capabilities) == VK_SUCCESS);
    uint32_t surface_format_count = 2;
    VkSurfaceFormatKHR surface_formats[2];
    assert(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physical, surface, &surface_format_count, surface_formats) == VK_SUCCESS);
    assert(surface_format_count == 2);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    const VkPhysicalDeviceFeatures enabled_features = {
        .imageCubeArray = VK_TRUE,
        .multiViewport = VK_TRUE,
        .sampleRateShading = VK_TRUE,
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .pEnabledFeatures = &enabled_features,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames =
            (const char *const[]){VK_KHR_SWAPCHAIN_EXTENSION_NAME},
    };
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = surface_capabilities.minImageCount,
        .imageFormat = surface_formats[0].format,
        .imageColorSpace = surface_formats[0].colorSpace,
        .imageExtent = surface_capabilities.currentExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    assert(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &swapchain) ==
           VK_SUCCESS);
    uint32_t swapchain_image_count = 0;
    assert(vkGetSwapchainImagesKHR(device, swapchain,
        &swapchain_image_count, NULL) == VK_SUCCESS);
    assert(swapchain_image_count == 3);

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4096,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer buffer = VK_NULL_HANDLE;
    assert(vkCreateBuffer(device, &buffer_info, NULL, &buffer) == VK_SUCCESS);
    VkMemoryRequirements buffer_requirements;
    vkGetBufferMemoryRequirements(device, buffer, &buffer_requirements);
    VkMemoryAllocateInfo buffer_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = buffer_requirements.size,
        .memoryTypeIndex = find_memory_type(physical, buffer_requirements.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
    };
    VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &buffer_allocation, NULL, &buffer_memory) == VK_SUCCESS);
    assert(vkBindBufferMemory(device, buffer, buffer_memory, 0) == VK_SUCCESS);

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {16, 16, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_4_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &image_info, NULL, &image) == VK_SUCCESS);
    VkMemoryRequirements image_requirements;
    vkGetImageMemoryRequirements(device, image, &image_requirements);
    VkMemoryAllocateInfo image_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = find_memory_type(physical, image_requirements.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &image_allocation, NULL, &image_memory) == VK_SUCCESS);
    assert(vkBindImageMemory(device, image, image_memory, 0) == VK_SUCCESS);
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_info.format,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &view_info, NULL, &view) == VK_SUCCESS);

    VkImageCreateInfo cube_info = image_info;
    cube_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cube_info.extent = (VkExtent3D){8u, 8u, 1u};
    cube_info.arrayLayers = 12u;
    cube_info.samples = VK_SAMPLE_COUNT_1_BIT;
    cube_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage cube_image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &cube_info, NULL, &cube_image) == VK_SUCCESS);
    VkMemoryRequirements cube_requirements;
    vkGetImageMemoryRequirements(device, cube_image, &cube_requirements);
    VkMemoryAllocateInfo cube_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = cube_requirements.size,
        .memoryTypeIndex = find_memory_type(physical,
            cube_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VkDeviceMemory cube_memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &cube_allocation, NULL, &cube_memory) ==
           VK_SUCCESS);
    assert(vkBindImageMemory(device, cube_image, cube_memory, 0u) == VK_SUCCESS);
    VkImageViewCreateInfo cube_view_info = view_info;
    cube_view_info.image = cube_image;
    cube_view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    cube_view_info.subresourceRange.layerCount = 12u;
    VkImageView cube_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &cube_view_info, NULL, &cube_view) ==
           VK_SUCCESS);

    VkImageCreateInfo bc_info = cube_info;
    bc_info.format = VK_FORMAT_BC7_SRGB_BLOCK;
    bc_info.extent = (VkExtent3D){17u, 17u, 1u};
    bc_info.mipLevels = 5u;
    bc_info.arrayLayers = 6u;
    bc_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage bc_image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &bc_info, NULL, &bc_image) == VK_SUCCESS);
    VkMemoryRequirements bc_requirements;
    vkGetImageMemoryRequirements(device, bc_image, &bc_requirements);
    VkMemoryAllocateInfo bc_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bc_requirements.size,
        .memoryTypeIndex = find_memory_type(physical,
            bc_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VkDeviceMemory bc_memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &bc_allocation, NULL, &bc_memory) ==
           VK_SUCCESS);
    assert(vkBindImageMemory(device, bc_image, bc_memory, 0u) == VK_SUCCESS);
    VkImageViewCreateInfo bc_view_info = cube_view_info;
    bc_view_info.image = bc_image;
    bc_view_info.format = bc_info.format;
    bc_view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    bc_view_info.subresourceRange.levelCount = 5u;
    bc_view_info.subresourceRange.layerCount = 6u;
    VkImageView bc_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &bc_view_info, NULL, &bc_view) ==
           VK_SUCCESS);
    bc_view_info.subresourceRange.baseMipLevel = 2u;
    bc_view_info.subresourceRange.levelCount = 2u;
    VkImageView bc_mip_view = VK_NULL_HANDLE;
    assert(vkCreateImageView(device, &bc_view_info, NULL, &bc_mip_view) ==
           VK_SUCCESS);

    VkImageCreateInfo clear_info = image_info;
    clear_info.samples = VK_SAMPLE_COUNT_1_BIT;
    clear_info.extent = (VkExtent3D){16u, 8u, 1u};
    clear_info.mipLevels = 4u;
    clear_info.arrayLayers = 2u;
    clear_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage clear_image = VK_NULL_HANDLE;
    assert(vkCreateImage(device, &clear_info, NULL, &clear_image) == VK_SUCCESS);
    VkMemoryRequirements clear_requirements;
    vkGetImageMemoryRequirements(device, clear_image, &clear_requirements);
    VkMemoryAllocateInfo clear_allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = clear_requirements.size,
        .memoryTypeIndex = find_memory_type(physical,
            clear_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VkDeviceMemory clear_memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &clear_allocation, NULL, &clear_memory) ==
           VK_SUCCESS);
    assert(vkBindImageMemory(device, clear_image, clear_memory, 0u) ==
           VK_SUCCESS);

    VkAttachmentDescription attachment = {
        .format = image_info.format,
        .samples = VK_SAMPLE_COUNT_4_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference color_reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_reference,
    };
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass render_pass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass) == VK_SUCCESS);
    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .pAttachments = &view,
        .width = 16,
        .height = 16,
        .layers = 1,
    };
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    assert(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer) == VK_SUCCESS);

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = 0,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    assert(vkCreateCommandPool(device, &pool_info, NULL, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo command_allocation = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command = VK_NULL_HANDLE;
    assert(vkAllocateCommandBuffers(device, &command_allocation, &command) == VK_SUCCESS);
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    assert(vkBeginCommandBuffer(command, &begin_info) == VK_SUCCESS);
    const VkImageMemoryBarrier clear_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0u,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = clear_image,
        .subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0u, 4u, 0u, 2u,
        },
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL, 1u,
        &clear_barrier);
    const VkClearColorValue clear_value = {
        .float32 = {0.25f, 0.5f, 0.75f, 1.0f},
    };
    const VkImageSubresourceRange clear_range = {
        VK_IMAGE_ASPECT_COLOR_BIT, 1u, 2u, 1u, 1u,
    };
    vkCmdClearColorImage(command, clear_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1u, &clear_range);
    assert(vkEndCommandBuffer(command) == VK_SUCCESS);
    VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence = VK_NULL_HANDLE;
    assert(vkCreateFence(device, &fence_info, NULL, &fence) == VK_SUCCESS);
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    assert(vkQueueSubmit(queue, 1, &submit, fence) == VK_SUCCESS);
    assert(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS);

    vkDestroyFence(device, fence, NULL);
    vkFreeCommandBuffers(device, pool, 1, &command);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyImage(device, clear_image, NULL);
    vkFreeMemory(device, clear_memory, NULL);
    vkDestroyImageView(device, bc_mip_view, NULL);
    vkDestroyImageView(device, bc_view, NULL);
    vkDestroyImage(device, bc_image, NULL);
    vkFreeMemory(device, bc_memory, NULL);
    vkDestroyImageView(device, cube_view, NULL);
    vkDestroyImage(device, cube_image, NULL);
    vkFreeMemory(device, cube_memory, NULL);
    vkDestroyImageView(device, view, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, image_memory, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, buffer_memory, NULL);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    assert(atomic_load(&validation_messages) == 0);
    return 0;
}
