#include <vulkan/vulkan.h>

#include <stdio.h>
#include <string.h>

#define ZINK_MESA_REVISION "44e18d3d7783c751fd77aeba01bbff28db97945a"

static int has_extension(const VkExtensionProperties *properties,
                         uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(properties[i].extensionName, name) == 0)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    const int strict = argc == 2 && strcmp(argv[1], "--strict") == 0;
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "zink-profile",
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS)
        return 2;
    uint32_t physical_count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    if (vkEnumeratePhysicalDevices(instance, &physical_count, &physical) !=
            VK_SUCCESS ||
        physical_count != 1) {
        vkDestroyInstance(instance, NULL);
        return 2;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    unsigned api_gaps = properties.apiVersion < VK_API_VERSION_1_2;
    if (api_gaps)
        printf("zink-profile: missing api Vulkan 1.2\n");

    uint32_t extension_count = 0;
    if (vkEnumerateDeviceExtensionProperties(
            physical, NULL, &extension_count, NULL) != VK_SUCCESS ||
        extension_count > 64) {
        vkDestroyInstance(instance, NULL);
        return 2;
    }
    VkExtensionProperties extensions[64];
    uint32_t written = extension_count;
    if (vkEnumerateDeviceExtensionProperties(
            physical, NULL, &written, extensions) != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 2;
    }
    static const char *const required_extensions[] = {
        VK_KHR_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
        VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME,
        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
        VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
        VK_EXT_BORDER_COLOR_SWIZZLE_EXTENSION_NAME,
        VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME,
        VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
        VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
    };
    unsigned extension_gaps = 0;
    for (uint32_t i = 0; i < sizeof(required_extensions) /
                                  sizeof(required_extensions[0]); ++i) {
        if (!has_extension(extensions, written, required_extensions[i])) {
            printf("zink-profile: missing extension %s\n",
                   required_extensions[i]);
            ++extension_gaps;
        }
    }

    VkPhysicalDeviceFeatures core_features;
    vkGetPhysicalDeviceFeatures(physical, &core_features);
    unsigned feature_gaps = 0;
#define REQUIRE_CORE_FEATURE(name) do { \
    if (!core_features.name) { \
        printf("zink-profile: missing feature " #name "\n"); \
        ++feature_gaps; \
    } \
} while (0)
    REQUIRE_CORE_FEATURE(robustBufferAccess);
    REQUIRE_CORE_FEATURE(logicOp);
    REQUIRE_CORE_FEATURE(fillModeNonSolid);
    REQUIRE_CORE_FEATURE(alphaToOne);
    REQUIRE_CORE_FEATURE(shaderClipDistance);
#undef REQUIRE_CORE_FEATURE

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
    };
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
        .pNext = &robustness2,
    };
    VkPhysicalDeviceScalarBlockLayoutFeatures scalar = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
        .pNext = &depth_clip,
    };
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext = &scalar,
    };
    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &timeline,
    };
    VkPhysicalDeviceMaintenance5Features maintenance5 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES,
        .pNext = &dynamic_rendering,
    };
    VkPhysicalDeviceLineRasterizationFeatures line = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES,
        .pNext = &maintenance5,
    };
    VkPhysicalDeviceBorderColorSwizzleFeaturesEXT border_swizzle = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT,
        .pNext = &line,
    };
    VkPhysicalDeviceCustomBorderColorFeaturesEXT custom_border = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
        .pNext = &border_swizzle,
    };
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &custom_border,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features2);
#define REQUIRE_EXTENSION_FEATURE(value, name) do { \
    if (!(value)) { \
        printf("zink-profile: missing feature " name "\n"); \
        ++feature_gaps; \
    } \
} while (0)
    REQUIRE_EXTENSION_FEATURE(custom_border.customBorderColorWithoutFormat,
                              "customBorderColorWithoutFormat");
    REQUIRE_EXTENSION_FEATURE(border_swizzle.borderColorSwizzleFromImage,
                              "borderColorSwizzleFromImage");
    if (properties.limits.strictLines) {
        REQUIRE_EXTENSION_FEATURE(line.rectangularLines || line.bresenhamLines,
                                  "rectangularLines-or-bresenhamLines");
    }
    REQUIRE_EXTENSION_FEATURE(maintenance5.maintenance5, "maintenance5");
    REQUIRE_EXTENSION_FEATURE(dynamic_rendering.dynamicRendering,
                              "dynamicRendering");
    REQUIRE_EXTENSION_FEATURE(timeline.timelineSemaphore,
                              "timelineSemaphore");
    REQUIRE_EXTENSION_FEATURE(scalar.scalarBlockLayout, "scalarBlockLayout");
    REQUIRE_EXTENSION_FEATURE(robustness2.nullDescriptor, "nullDescriptor");
    REQUIRE_EXTENSION_FEATURE(depth_clip.depthClipEnable, "depthClipEnable");
#undef REQUIRE_EXTENSION_FEATURE

    if (robustness2.nullDescriptor && has_extension(
            extensions, written, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) {
        const float priority = 1.0f;
        const VkDeviceQueueCreateInfo queue = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = 0u,
            .queueCount = 1u,
            .pQueuePriorities = &priority,
        };
        VkPhysicalDeviceRobustness2FeaturesEXT requested = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            .nullDescriptor = VK_TRUE,
        };
        VkPhysicalDeviceDepthClipEnableFeaturesEXT requested_depth_clip = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
            .pNext = &requested,
            .depthClipEnable = VK_TRUE,
        };
        const char *const requested_extensions[] = {
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
            VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
        };
        const VkDeviceCreateInfo device_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &requested_depth_clip,
            .queueCreateInfoCount = 1u,
            .pQueueCreateInfos = &queue,
            .enabledExtensionCount = 2u,
            .ppEnabledExtensionNames = requested_extensions,
        };
        VkDevice device = VK_NULL_HANDLE;
        if (vkCreateDevice(physical, &device_info, NULL, &device) !=
                VK_SUCCESS) {
            printf("zink-profile: failed to enable nullDescriptor\n");
            ++feature_gaps;
        } else {
            vkDestroyDevice(device, NULL);
        }
    }

    const unsigned total = api_gaps + extension_gaps + feature_gaps;
    printf("zink-profile: mesa=%s api=%u extensions=%u features=%u total=%u\n",
           ZINK_MESA_REVISION, api_gaps, extension_gaps, feature_gaps, total);
    vkDestroyInstance(instance, NULL);
    return strict && total ? 1 : 0;
}
