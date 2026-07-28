#include <vulkan/vulkan.h>

#include <stdio.h>
#include <string.h>

#define EDEN_MANDATORY_CORE_FEATURES(X)             \
    X(depthBiasClamp)                               \
    X(depthClamp)                                   \
    X(drawIndirectFirstInstance)                    \
    X(dualSrcBlend)                                 \
    X(fillModeNonSolid)                             \
    X(fragmentStoresAndAtomics)                     \
    X(geometryShader)                               \
    X(imageCubeArray)                               \
    X(independentBlend)                             \
    X(largePoints)                                  \
    X(logicOp)                                      \
    X(multiDrawIndirect)                            \
    X(multiViewport)                                \
    X(occlusionQueryPrecise)                        \
    X(robustBufferAccess)                           \
    X(samplerAnisotropy)                            \
    X(sampleRateShading)                            \
    X(shaderClipDistance)                           \
    X(shaderCullDistance)                           \
    X(shaderImageGatherExtended)                    \
    X(shaderStorageImageWriteWithoutFormat)         \
    X(tessellationShader)                           \
    X(vertexPipelineStoresAndAtomics)               \
    X(wideLines)

static int has_extension(const VkExtensionProperties *properties,
                         uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; ++i)
        if (strcmp(properties[i].extensionName, name) == 0)
            return 1;
    return 0;
}

int main(int argc, char **argv) {
    const int strict = argc == 2 && strcmp(argv[1], "--strict") == 0;
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Vulkan-PS5 Eden profile probe",
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
        return 3;
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    unsigned missing_limits = 0;
#define CHECK_LIMIT(field, minimum)                                                \
    do {                                                                           \
        if (properties.limits.field < (minimum)) {                                 \
            printf("eden-profile: missing limit %s=%u minimum=%u\n", #field,      \
                   properties.limits.field, (unsigned)(minimum));                  \
            ++missing_limits;                                                      \
        }                                                                          \
    } while (0)
    CHECK_LIMIT(maxUniformBufferRange, 65536);
    CHECK_LIMIT(maxViewports, 16);
    CHECK_LIMIT(maxColorAttachments, 8);
    CHECK_LIMIT(maxClipDistances, 8);
#undef CHECK_LIMIT

    uint32_t extension_count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count,
                                             NULL) != VK_SUCCESS ||
        extension_count > 32) {
        vkDestroyInstance(instance, NULL);
        return 4;
    }
    VkExtensionProperties extensions[32];
    uint32_t written_extensions = extension_count;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL,
            &written_extensions, extensions) != VK_SUCCESS) {
        vkDestroyInstance(instance, NULL);
        return 5;
    }
    static const char *const mandatory_extensions[] = {
        VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
        VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
        VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
        VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
    };
    unsigned missing_extensions = 0;
    for (size_t i = 0; i < sizeof(mandatory_extensions) /
                                   sizeof(mandatory_extensions[0]); ++i) {
        if (!has_extension(extensions, written_extensions,
                           mandatory_extensions[i])) {
            printf("eden-profile: missing extension %s\n",
                   mandatory_extensions[i]);
            ++missing_extensions;
        }
    }

    VkPhysicalDeviceVariablePointersFeatures variable = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES,
    };
    VkPhysicalDeviceShaderDrawParametersFeatures draw_parameters = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,
        .pNext = &variable,
    };
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT,
        .pNext = &draw_parameters,
    };
    VkPhysicalDeviceHostQueryResetFeatures host_query_reset = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .pNext = &demote,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &host_query_reset,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features);

    unsigned missing_features = 0;
#define CHECK_CORE_FEATURE(name)                                      \
    do {                                                               \
        if (!features.features.name) {                                 \
            printf("eden-profile: missing feature %s\n", #name);     \
            ++missing_features;                                        \
        }                                                              \
    } while (0);
    EDEN_MANDATORY_CORE_FEATURES(CHECK_CORE_FEATURE)
#undef CHECK_CORE_FEATURE
#define CHECK_CHAIN_FEATURE(value, name)                               \
    do {                                                               \
        if (!(value)) {                                                \
            printf("eden-profile: missing feature %s\n", (name));    \
            ++missing_features;                                        \
        }                                                              \
    } while (0)
    CHECK_CHAIN_FEATURE(host_query_reset.hostQueryReset, "hostQueryReset");
    CHECK_CHAIN_FEATURE(demote.shaderDemoteToHelperInvocation,
                        "shaderDemoteToHelperInvocation");
    CHECK_CHAIN_FEATURE(draw_parameters.shaderDrawParameters,
                        "shaderDrawParameters");
    CHECK_CHAIN_FEATURE(variable.variablePointers, "variablePointers");
    CHECK_CHAIN_FEATURE(variable.variablePointersStorageBuffer,
                        "variablePointersStorageBuffer");
#undef CHECK_CHAIN_FEATURE

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, NULL);
    VkQueueFamilyProperties queue = {0};
    uint32_t written_queues = queue_count ? 1 : 0;
    if (written_queues)
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &written_queues,
                                                 &queue);
    const VkQueueFlags required_queue_flags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    const unsigned missing_queues =
        written_queues != 1 || queue.queueCount == 0 ||
        (queue.queueFlags & required_queue_flags) != required_queue_flags;
    if (missing_queues)
        printf("eden-profile: missing universal graphics/compute/transfer queue\n");

    const unsigned missing = missing_extensions + missing_features +
                             missing_limits + missing_queues;
    printf("eden-profile: extensions=%u features=%u limits=%u queues=%u total=%u\n",
           missing_extensions, missing_features, missing_limits,
           missing_queues, missing);
    vkDestroyInstance(instance, NULL);
    return strict && missing != 0 ? 1 : 0;
}
