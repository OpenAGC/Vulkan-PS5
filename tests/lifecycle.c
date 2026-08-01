#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    uint32_t api_version = 0;
    assert(vkEnumerateInstanceVersion(&api_version) == VK_SUCCESS);
    assert(VK_API_VERSION_MAJOR(api_version) == 1);
    assert(VK_API_VERSION_MINOR(api_version) == 2);

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "lifecycle",
        .apiVersion = VK_API_VERSION_1_2,
    };
    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS);

    uint32_t physical_count = 0;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, NULL) == VK_SUCCESS);
    assert(physical_count == 1);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &physical_count, &physical) == VK_SUCCESS);

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    assert(properties.vendorID == 0x1002);
    assert(strstr(properties.deviceName, "gfx1013") != NULL);
    assert(properties.limits.pointSizeRange[0] == 1.0f);
    assert(properties.limits.pointSizeRange[1] == 64.0f);
    assert(properties.limits.pointSizeGranularity == 0.125f);
    assert(properties.limits.lineWidthRange[0] == 1.0f);
    assert(properties.limits.lineWidthRange[1] == 64.0f);
    assert(properties.limits.lineWidthGranularity == 0.125f);
    assert((properties.limits.framebufferDepthSampleCounts &
            VK_SAMPLE_COUNT_4_BIT) != 0u);
    assert((properties.limits.framebufferStencilSampleCounts &
            VK_SAMPLE_COUNT_4_BIT) != 0u);
    assert((properties.limits.framebufferNoAttachmentsSampleCounts &
            VK_SAMPLE_COUNT_4_BIT) != 0u);
    assert((properties.limits.sampledImageColorSampleCounts &
            VK_SAMPLE_COUNT_4_BIT) != 0u);
    assert((properties.limits.sampledImageDepthSampleCounts &
            VK_SAMPLE_COUNT_4_BIT) != 0u);
    assert((properties.limits.sampledImageStencilSampleCounts &
            VK_SAMPLE_COUNT_4_BIT) != 0u);

    VkPhysicalDeviceSubgroupProperties subgroup = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES,
    };
    VkPhysicalDeviceVertexAttributeDivisorProperties divisor_properties = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES,
        .pNext = &subgroup,
        .maxVertexAttribDivisor = 0,
        .supportsNonZeroFirstInstance = VK_TRUE,
    };
    VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT divisor_properties_ext = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT,
        .pNext = &divisor_properties,
        .maxVertexAttribDivisor = 0,
    };
    VkPhysicalDeviceMaintenance3Properties maintenance = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES,
        .pNext = &divisor_properties_ext,
    };
    VkPhysicalDeviceFloatControlsProperties float_controls = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES,
        .pNext = &maintenance,
    };
    VkPhysicalDeviceDriverProperties driver = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        .pNext = &float_controls,
    };
    VkPhysicalDeviceIDProperties id = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
        .pNext = &driver,
    };
    VkPhysicalDeviceProperties2 properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &id,
    };
    vkGetPhysicalDeviceProperties2(physical, &properties2);
    assert(id.deviceUUID[0] != 0);
    assert(driver.driverID == VK_DRIVER_ID_MESA_RADV);
    assert(strcmp(driver.driverName, "Vulkan-PS5") == 0);
    assert(driver.conformanceVersion.major == 0);
    assert(driver.conformanceVersion.minor == 0);
    assert(driver.conformanceVersion.subminor == 0);
    assert(driver.conformanceVersion.patch == 0);
    assert(float_controls.denormBehaviorIndependence ==
           VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE);
    assert(float_controls.roundingModeIndependence ==
           VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE);
    assert(float_controls.shaderSignedZeroInfNanPreserveFloat32 == VK_FALSE);
    assert(maintenance.maxMemoryAllocationSize == 12ull * 1024 * 1024 * 1024);
    assert(divisor_properties_ext.maxVertexAttribDivisor == UINT32_MAX);
    assert(divisor_properties.maxVertexAttribDivisor == UINT32_MAX);
    assert(divisor_properties.supportsNonZeroFirstInstance == VK_FALSE);
    assert(subgroup.subgroupSize == 32);
    VkPhysicalDeviceTimelineSemaphoreProperties timeline_properties = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES,
    };
    VkPhysicalDeviceProperties2 timeline_properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &timeline_properties,
    };
    vkGetPhysicalDeviceProperties2(physical, &timeline_properties2);
assert(timeline_properties.maxTimelineSemaphoreValueDifference ==
UINT64_MAX);
VkPhysicalDeviceLineRasterizationProperties line_properties = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES,
};
VkPhysicalDeviceProperties2 line_properties2 = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
.pNext = &line_properties,
};
vkGetPhysicalDeviceProperties2(physical, &line_properties2);
assert(line_properties.lineSubPixelPrecisionBits == 8);
    VkPhysicalDeviceCustomBorderColorPropertiesEXT border_properties = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT,
    };
    VkPhysicalDeviceProperties2 border_properties2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &border_properties,
    };
    vkGetPhysicalDeviceProperties2(physical, &border_properties2);
    assert(border_properties.maxCustomBorderColorSamplers == 64u);

    uint32_t extension_count = 0;
    assert(vkEnumerateDeviceExtensionProperties(
        physical, NULL, &extension_count, NULL) == VK_SUCCESS);
    assert(extension_count == 22);
    VkExtensionProperties extensions[22];
    assert(vkEnumerateDeviceExtensionProperties(
        physical, NULL, &extension_count, extensions) == VK_SUCCESS);
    VkBool32 has_host_query_reset = VK_FALSE;
    VkBool32 has_vertex_divisor = VK_FALSE;
    VkBool32 has_swapchain = VK_FALSE;
    VkBool32 has_driver_properties = VK_FALSE;
    VkBool32 has_sampler_mirror_clamp = VK_FALSE;
    VkBool32 has_shader_float_controls = VK_FALSE;
    VkBool32 has_shader_demote = VK_FALSE;
    VkBool32 has_maintenance1 = VK_FALSE;
    VkBool32 has_renderpass2 = VK_FALSE;
    VkBool32 has_descriptor_template = VK_FALSE;
    VkBool32 has_timeline = VK_FALSE;
    VkBool32 has_scalar_layout = VK_FALSE;
    VkBool32 has_dynamic_rendering = VK_FALSE;
    VkBool32 has_custom_border_color = VK_FALSE;
    VkBool32 has_border_color_swizzle = VK_FALSE;
    VkBool32 has_maintenance5 = VK_FALSE;
    VkBool32 has_depth_clip_enable = VK_FALSE;
    for (uint32_t i = 0; i < extension_count; ++i) {
        has_host_query_reset |= strcmp(extensions[i].extensionName,
            VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME) == 0;
        has_vertex_divisor |= strcmp(extensions[i].extensionName,
            VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME) == 0;
        has_swapchain |= strcmp(extensions[i].extensionName,
            VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        has_driver_properties |= strcmp(extensions[i].extensionName,
            VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME) == 0;
        has_sampler_mirror_clamp |= strcmp(extensions[i].extensionName,
            VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME) == 0;
        has_shader_float_controls |= strcmp(extensions[i].extensionName,
            VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME) == 0;
        has_shader_demote |= strcmp(extensions[i].extensionName,
            VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME) == 0;
        has_maintenance1 |= strcmp(extensions[i].extensionName,
            VK_KHR_MAINTENANCE_1_EXTENSION_NAME) == 0;
        has_renderpass2 |= strcmp(extensions[i].extensionName,
            VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) == 0;
        has_descriptor_template |= strcmp(extensions[i].extensionName,
            VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME) == 0;
        has_timeline |= strcmp(extensions[i].extensionName,
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0;
        has_scalar_layout |= strcmp(extensions[i].extensionName,
            VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME) == 0;
        has_dynamic_rendering |= strcmp(extensions[i].extensionName,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0;
        has_custom_border_color |= strcmp(extensions[i].extensionName,
            VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME) == 0;
        has_border_color_swizzle |= strcmp(extensions[i].extensionName,
            VK_EXT_BORDER_COLOR_SWIZZLE_EXTENSION_NAME) == 0;
        has_maintenance5 |= strcmp(extensions[i].extensionName,
            VK_KHR_MAINTENANCE_5_EXTENSION_NAME) == 0;
        has_depth_clip_enable |= strcmp(extensions[i].extensionName,
            VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME) == 0;
    }
    assert(has_host_query_reset && has_vertex_divisor && has_swapchain &&
           has_driver_properties && has_sampler_mirror_clamp &&
           has_shader_float_controls && has_shader_demote &&
           has_maintenance1 && has_renderpass2 && has_descriptor_template);
    assert(has_timeline && has_scalar_layout && has_dynamic_rendering &&
           has_custom_border_color && has_border_color_swizzle);
    assert(has_maintenance5 && has_depth_clip_enable);

    VkPhysicalDeviceVulkan11Features features11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .shaderDrawParameters = VK_TRUE,
    };
    VkPhysicalDeviceVertexAttributeDivisorFeatures divisor_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES,
        .pNext = &features11,
        .vertexAttributeInstanceRateDivisor = VK_TRUE,
        .vertexAttributeInstanceRateZeroDivisor = VK_TRUE,
    };
    VkPhysicalDeviceHostQueryResetFeatures host_query_reset = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
    };
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT,
        .pNext = &divisor_features,
    };
    host_query_reset.pNext = &demote_features;
    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &host_query_reset,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features2);
    assert(features2.features.robustBufferAccess == VK_TRUE);
    assert(features2.features.alphaToOne == VK_TRUE);
    assert(host_query_reset.hostQueryReset == VK_TRUE);
    assert(demote_features.shaderDemoteToHelperInvocation == VK_TRUE);
    assert(divisor_features.vertexAttributeInstanceRateDivisor == VK_TRUE);
    assert(divisor_features.vertexAttributeInstanceRateZeroDivisor == VK_FALSE);
    assert(features11.shaderDrawParameters == VK_TRUE);
    assert(features11.variablePointers == VK_TRUE);
    assert(features11.variablePointersStorageBuffer == VK_TRUE);
    assert(features2.features.depthBiasClamp == VK_TRUE);
    assert(features2.features.depthClamp == VK_TRUE);
    assert(features2.features.drawIndirectFirstInstance == VK_TRUE);
    assert(features2.features.dualSrcBlend == VK_TRUE);
    assert(features2.features.fragmentStoresAndAtomics == VK_TRUE);
    assert(features2.features.fillModeNonSolid == VK_TRUE);
    assert(features2.features.geometryShader == VK_TRUE);
    assert(features2.features.imageCubeArray == VK_TRUE);
    assert(features2.features.multiViewport == VK_TRUE);
    assert(features2.features.independentBlend == VK_TRUE);
    assert(features2.features.largePoints == VK_TRUE);
    assert(features2.features.logicOp == VK_TRUE);
    assert(features2.features.multiDrawIndirect == VK_TRUE);
    assert(features2.features.occlusionQueryPrecise == VK_TRUE);
    assert(features2.features.samplerAnisotropy == VK_TRUE);
    assert(features2.features.shaderClipDistance == VK_TRUE);
    assert(features2.features.shaderCullDistance == VK_TRUE);
    assert(features2.features.shaderImageGatherExtended == VK_TRUE);
    assert(features2.features.tessellationShader == VK_TRUE);
    assert(features2.features.vertexPipelineStoresAndAtomics == VK_TRUE);
    assert(features2.features.wideLines == VK_TRUE);

    VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,
    };
    VkPhysicalDeviceFeatures2 shader_draw_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &shader_draw_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &shader_draw_features2);
    assert(shader_draw_features.shaderDrawParameters == VK_TRUE);

    VkPhysicalDeviceVariablePointersFeatures variable_pointer_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES,
    };
    VkPhysicalDeviceFeatures2 variable_pointer_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &variable_pointer_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &variable_pointer_features2);
    assert(variable_pointer_features.variablePointers == VK_TRUE);
    assert(variable_pointer_features.variablePointersStorageBuffer == VK_TRUE);
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
    };
    VkPhysicalDeviceFeatures2 timeline_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &timeline_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &timeline_features2);
assert(timeline_features.timelineSemaphore == VK_TRUE);
VkPhysicalDeviceScalarBlockLayoutFeatures scalar_features = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
};
VkPhysicalDeviceFeatures2 scalar_features2 = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
.pNext = &scalar_features,
};
vkGetPhysicalDeviceFeatures2(physical, &scalar_features2);
assert(scalar_features.scalarBlockLayout == VK_TRUE);
VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
};
VkPhysicalDeviceFeatures2 dynamic_rendering_features2 = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
.pNext = &dynamic_rendering_features,
};
vkGetPhysicalDeviceFeatures2(physical, &dynamic_rendering_features2);
assert(dynamic_rendering_features.dynamicRendering == VK_TRUE);
    VkPhysicalDeviceCustomBorderColorFeaturesEXT custom_border_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 custom_border_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &custom_border_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &custom_border_features2);
    assert(custom_border_features.customBorderColors == VK_TRUE);
    assert(custom_border_features.customBorderColorWithoutFormat == VK_TRUE);
    VkPhysicalDeviceBorderColorSwizzleFeaturesEXT border_swizzle_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 border_swizzle_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &border_swizzle_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &border_swizzle_features2);
    assert(border_swizzle_features.borderColorSwizzle == VK_FALSE);
    assert(border_swizzle_features.borderColorSwizzleFromImage == VK_TRUE);
    VkPhysicalDeviceMaintenance5Features maintenance5_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES,
    };
    VkPhysicalDeviceFeatures2 maintenance5_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &maintenance5_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &maintenance5_features2);
    assert(maintenance5_features.maintenance5 == VK_TRUE);
    VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 depth_clip_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &depth_clip_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &depth_clip_features2);
    assert(depth_clip_features.depthClipEnable == VK_TRUE);
    VkPhysicalDeviceVulkan12Features features12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features12_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features12,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features12_query);
    assert(features12.samplerMirrorClampToEdge == VK_TRUE);
    assert(features12.scalarBlockLayout == VK_TRUE);
    assert(features12.hostQueryReset == VK_TRUE);
    assert(features12.timelineSemaphore == VK_TRUE);
    assert(features12.drawIndirectCount == VK_FALSE);
    assert(features12.bufferDeviceAddress == VK_FALSE);
VkPhysicalDeviceLineRasterizationFeatures line_features = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES,
};
VkPhysicalDeviceFeatures2 line_features2 = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
.pNext = &line_features,
};
vkGetPhysicalDeviceFeatures2(physical, &line_features2);
assert(line_features.rectangularLines == VK_TRUE);
assert(line_features.bresenhamLines == VK_FALSE);
assert(line_features.smoothLines == VK_FALSE);
assert(line_features.stippledRectangularLines == VK_FALSE);

    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT divisor_features_ext = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT,
        .vertexAttributeInstanceRateDivisor = VK_FALSE,
        .vertexAttributeInstanceRateZeroDivisor = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features2_ext = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &divisor_features_ext,
    };
    vkGetPhysicalDeviceFeatures2(physical, &features2_ext);
    assert(divisor_features_ext.vertexAttributeInstanceRateDivisor == VK_TRUE);
    assert(divisor_features_ext.vertexAttributeInstanceRateZeroDivisor == VK_FALSE);

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical, &features);
    assert(features.robustBufferAccess == VK_TRUE);
    assert(features.alphaToOne == VK_TRUE);
    assert(features.depthBiasClamp == VK_TRUE);
    assert(features.depthClamp == VK_TRUE);
    assert(features.drawIndirectFirstInstance == VK_TRUE);
    assert(features.dualSrcBlend == VK_TRUE);
    assert(features.fragmentStoresAndAtomics == VK_TRUE);
    assert(features.fillModeNonSolid == VK_TRUE);
    assert(features.geometryShader == VK_TRUE);
    assert(features.imageCubeArray == VK_TRUE);
    assert(features.multiViewport == VK_TRUE);
    assert(features.independentBlend == VK_TRUE);
    assert(features.largePoints == VK_TRUE);
    assert(features.logicOp == VK_TRUE);
    assert(features.multiDrawIndirect == VK_TRUE);
    assert(features.occlusionQueryPrecise == VK_TRUE);
    assert(features.samplerAnisotropy == VK_TRUE);
    assert(features.sampleRateShading == VK_TRUE);
    assert(features.shaderClipDistance == VK_TRUE);
    assert(features.shaderCullDistance == VK_TRUE);
    assert(features.shaderImageGatherExtended == VK_TRUE);
    assert(features.shaderStorageImageWriteWithoutFormat == VK_TRUE);
    assert(features.tessellationShader == VK_TRUE);
    assert(features.vertexPipelineStoresAndAtomics == VK_TRUE);
    assert(features.wideLines == VK_TRUE);
    features.depthBiasClamp = VK_FALSE;
    features.depthClamp = VK_FALSE;
    features.drawIndirectFirstInstance = VK_FALSE;
    features.dualSrcBlend = VK_FALSE;
    features.fragmentStoresAndAtomics = VK_FALSE;
    features.fillModeNonSolid = VK_FALSE;
    features.geometryShader = VK_FALSE;
    features.imageCubeArray = VK_FALSE;
    features.multiViewport = VK_FALSE;
    features.independentBlend = VK_FALSE;
    features.largePoints = VK_FALSE;
    features.logicOp = VK_FALSE;
    features.multiDrawIndirect = VK_FALSE;
    features.occlusionQueryPrecise = VK_FALSE;
    features.samplerAnisotropy = VK_FALSE;
    features.sampleRateShading = VK_FALSE;
    features.shaderClipDistance = VK_FALSE;
    features.shaderCullDistance = VK_FALSE;
    features.shaderImageGatherExtended = VK_FALSE;
    features.shaderStorageImageWriteWithoutFormat = VK_FALSE;
    features.tessellationShader = VK_FALSE;
    features.vertexPipelineStoresAndAtomics = VK_FALSE;
    features.wideLines = VK_FALSE;
    features.robustBufferAccess = VK_FALSE;
    features.alphaToOne = VK_FALSE;
    const VkBool32 *feature_bits = (const VkBool32 *)&features;
    for (size_t i = 0; i < sizeof(features) / sizeof(*feature_bits); ++i)
        assert(feature_bits[i] == VK_FALSE);

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    assert(memory_properties.memoryTypeCount == 2);
    assert(memory_properties.memoryHeapCount == 2);
    assert(memory_properties.memoryHeaps[0].size == 4ull * 1024 * 1024 * 1024);
    assert(memory_properties.memoryHeaps[1].size == 12ull * 1024 * 1024 * 1024);

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM,
                                        &format_properties);
    assert(format_properties.optimalTilingFeatures &
           VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    assert((format_properties.optimalTilingFeatures &
        (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT)) ==
        (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT));
    assert(format_properties.linearTilingFeatures &
           VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
    assert(!(format_properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT));
    VkImageFormatProperties image_format_properties;
    assert(vkGetPhysicalDeviceImageFormatProperties(physical,
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, 0u,
        &image_format_properties) == VK_SUCCESS);
    assert(vkGetPhysicalDeviceImageFormatProperties(physical,
        VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, 0u,
        &image_format_properties) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    assert(vkGetPhysicalDeviceImageFormatProperties(physical,
        VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
        VK_IMAGE_CREATE_EXTENDED_USAGE_BIT,
        &image_format_properties) == VK_SUCCESS);
    const VkFormat sampled_msaa_formats[] = {
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D16_UNORM,
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_S8_UINT,
    };
    for (uint32_t i = 0u; i < 4u; ++i) {
        assert(vkGetPhysicalDeviceImageFormatProperties(physical,
            sampled_msaa_formats[i], VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0u,
            &image_format_properties) == VK_SUCCESS);
        assert((image_format_properties.sampleCounts &
                VK_SAMPLE_COUNT_4_BIT) != 0u);
    }
    assert(vkGetPhysicalDeviceImageFormatProperties(physical,
        VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0u,
        &image_format_properties) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    const VkFormat integer_color_formats[] = {
        VK_FORMAT_R16_UINT,
        VK_FORMAT_R16_SINT,
        VK_FORMAT_R16G16_UINT,
        VK_FORMAT_R16G16_SINT,
        VK_FORMAT_R16G16B16A16_UINT,
        VK_FORMAT_R16G16B16A16_SINT,
        VK_FORMAT_R32_UINT,
        VK_FORMAT_R32_SINT,
        VK_FORMAT_R32G32_UINT,
        VK_FORMAT_R32G32_SINT,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_FORMAT_R32G32B32A32_SINT,
    };
    const VkFormatFeatureFlags integer_color_features =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    for (size_t i = 0u;
         i < sizeof(integer_color_formats) /
             sizeof(integer_color_formats[0]); ++i) {
        vkGetPhysicalDeviceFormatProperties(physical,
            integer_color_formats[i], &format_properties);
        assert(format_properties.linearTilingFeatures ==
            integer_color_features);
        assert(format_properties.optimalTilingFeatures ==
            integer_color_features);
        assert(format_properties.bufferFeatures == 0u);
        assert(vkGetPhysicalDeviceImageFormatProperties(physical,
            integer_color_formats[i], VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            0u, &image_format_properties) == VK_SUCCESS);
    }
    const VkFormat normalized_storage_formats[] = {
        VK_FORMAT_R16_UNORM,
        VK_FORMAT_R16_SNORM,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R16G16B16A16_SNORM,
    };
    const VkFormatFeatureFlags normalized_storage_features =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    for (size_t i = 0u;
         i < sizeof(normalized_storage_formats) /
             sizeof(normalized_storage_formats[0]); ++i) {
        vkGetPhysicalDeviceFormatProperties(physical,
            normalized_storage_formats[i], &format_properties);
        assert(format_properties.linearTilingFeatures ==
            normalized_storage_features);
        assert(format_properties.optimalTilingFeatures ==
            normalized_storage_features);
        assert(format_properties.bufferFeatures == 0u);
        assert(vkGetPhysicalDeviceImageFormatProperties(physical,
            normalized_storage_formats[i], VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            0u, &image_format_properties) == VK_SUCCESS);
    }
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
                                        &format_properties);
    assert(format_properties.optimalTilingFeatures == 0);
    const VkFormat unsupported_compressed_formats[] = {
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
    };
    for (size_t i = 0u;
         i < sizeof(unsupported_compressed_formats) /
             sizeof(unsupported_compressed_formats[0]); ++i) {
        vkGetPhysicalDeviceFormatProperties(physical,
            unsupported_compressed_formats[i], &format_properties);
        assert(format_properties.linearTilingFeatures == 0u);
        assert(format_properties.optimalTilingFeatures == 0u);
        assert(format_properties.bufferFeatures == 0u);
        assert(vkGetPhysicalDeviceImageFormatProperties(physical,
            unsupported_compressed_formats[i], VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, 0u,
            &image_format_properties) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    }
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_D24_UNORM_S8_UINT,
                                        &format_properties);
    assert(format_properties.linearTilingFeatures == 0u);
    assert(format_properties.optimalTilingFeatures == 0u);
    assert(format_properties.bufferFeatures == 0u);
    assert(vkGetPhysicalDeviceImageFormatProperties(physical,
        VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0u,
        &image_format_properties) == VK_ERROR_FORMAT_NOT_SUPPORTED);

    const VkFormat bc_formats[] = {
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
        VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
        VK_FORMAT_BC2_UNORM_BLOCK,
        VK_FORMAT_BC2_SRGB_BLOCK,
        VK_FORMAT_BC3_UNORM_BLOCK,
        VK_FORMAT_BC3_SRGB_BLOCK,
        VK_FORMAT_BC4_UNORM_BLOCK,
        VK_FORMAT_BC4_SNORM_BLOCK,
        VK_FORMAT_BC5_UNORM_BLOCK,
        VK_FORMAT_BC5_SNORM_BLOCK,
        VK_FORMAT_BC6H_UFLOAT_BLOCK,
        VK_FORMAT_BC6H_SFLOAT_BLOCK,
        VK_FORMAT_BC7_UNORM_BLOCK,
        VK_FORMAT_BC7_SRGB_BLOCK,
    };
    const VkFormatFeatureFlags bc_features =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT;
    for (size_t i = 0u;
         i < sizeof(bc_formats) / sizeof(bc_formats[0]); ++i) {
        vkGetPhysicalDeviceFormatProperties(physical, bc_formats[i],
                                            &format_properties);
        assert(format_properties.linearTilingFeatures == bc_features);
        assert(format_properties.optimalTilingFeatures == bc_features);
        assert(format_properties.bufferFeatures == 0u);
        assert(vkGetPhysicalDeviceImageFormatProperties(physical,
            bc_formats[i], VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            &image_format_properties) == VK_SUCCESS);
        assert(image_format_properties.maxMipLevels >= 5u);
        assert(image_format_properties.maxArrayLayers >= 6u);
        assert(vkGetPhysicalDeviceImageFormatProperties(physical,
            bc_formats[i], VK_IMAGE_TYPE_3D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT, 0u,
            &image_format_properties) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    }

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
    assert(family_count == 1);
    VkQueueFamilyProperties family;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, &family);
    assert((family.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                                VK_QUEUE_TRANSFER_BIT)) != 0);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
.enabledExtensionCount = 12,
        .ppEnabledExtensionNames =
            (const char *const[]){
                VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
                VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
                VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
                VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
                VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
                VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
                VK_KHR_MAINTENANCE_1_EXTENSION_NAME,
                VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
                VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME,
VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
},
};
VkPhysicalDeviceLineRasterizationFeatures enabled_line = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES,
.rectangularLines = VK_TRUE,
};
VkPhysicalDeviceDepthClipEnableFeaturesEXT enabled_depth_clip = {
.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT,
.pNext = &enabled_line,
.depthClipEnable = VK_TRUE,
};
VkPhysicalDeviceTimelineSemaphoreFeatures enabled_timeline = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
.timelineSemaphore = VK_TRUE,
};
enabled_timeline.pNext = &enabled_depth_clip;
    VkPhysicalDeviceHostQueryResetFeatures enabled_host_query_reset = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .pNext = &enabled_timeline,
        .hostQueryReset = VK_TRUE,
    };
    VkPhysicalDeviceVertexAttributeDivisorFeatures enabled_divisor = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES,
        .vertexAttributeInstanceRateDivisor = VK_TRUE,
    };
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT enabled_demote = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT,
        .pNext = &enabled_host_query_reset,
        .shaderDemoteToHelperInvocation = VK_TRUE,
    };
    enabled_divisor.pNext = &enabled_demote;
    VkPhysicalDeviceFeatures2 enabled_features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enabled_divisor,
        .features = {
            .robustBufferAccess = VK_TRUE,
            .depthBiasClamp = VK_TRUE,
            .depthClamp = VK_TRUE,
            .drawIndirectFirstInstance = VK_TRUE,
            .dualSrcBlend = VK_TRUE,
            .fragmentStoresAndAtomics = VK_TRUE,
            .fillModeNonSolid = VK_TRUE,
            .geometryShader = VK_TRUE,
            .imageCubeArray = VK_TRUE,
            .multiViewport = VK_TRUE,
            .independentBlend = VK_TRUE,
            .largePoints = VK_TRUE,
            .logicOp = VK_TRUE,
            .multiDrawIndirect = VK_TRUE,
            .occlusionQueryPrecise = VK_TRUE,
            .samplerAnisotropy = VK_TRUE,
            .shaderClipDistance = VK_TRUE,
            .shaderCullDistance = VK_TRUE,
            .shaderImageGatherExtended = VK_TRUE,
            .tessellationShader = VK_TRUE,
            .vertexPipelineStoresAndAtomics = VK_TRUE,
            .wideLines = VK_TRUE,
        },
    };
    VkPhysicalDeviceVulkan11Features enabled_features11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &enabled_divisor,
        .shaderDrawParameters = VK_TRUE,
        .variablePointers = VK_TRUE,
        .variablePointersStorageBuffer = VK_TRUE,
    };
    enabled_features2.pNext = &enabled_features11;
    VkDeviceGroupDeviceCreateInfo group_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO,
        .pNext = &enabled_features2,
        .physicalDeviceCount = 1,
        .pPhysicalDevices = &physical,
    };
    device_info.pNext = &group_info;
    VkDeviceCreateInfo unsupported_device_info = device_info;
    unsupported_device_info.pNext = NULL;
    VkDevice unsupported_device = VK_NULL_HANDLE;
    VkPhysicalDeviceVertexAttributeDivisorFeatures unsupported_divisor = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES,
        .vertexAttributeInstanceRateZeroDivisor = VK_TRUE,
    };
    unsupported_device_info.pNext = &unsupported_divisor;
    unsupported_device_info.pEnabledFeatures = NULL;
    assert(vkCreateDevice(physical, &unsupported_device_info, NULL,
                          &unsupported_device) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(unsupported_device == VK_NULL_HANDLE);
    VkPhysicalDeviceShaderDrawParametersFeatures enabled_shader_draw = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,
        .shaderDrawParameters = VK_TRUE,
    };
    VkDeviceCreateInfo shader_draw_device_info = device_info;
    shader_draw_device_info.pNext = &enabled_shader_draw;
    VkDevice shader_draw_device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &shader_draw_device_info, NULL,
                          &shader_draw_device) == VK_SUCCESS);
    assert(shader_draw_device != VK_NULL_HANDLE);
    vkDestroyDevice(shader_draw_device, NULL);
    VkPhysicalDeviceVariablePointersFeatures enabled_variable_pointers = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES,
        .variablePointers = VK_TRUE,
        .variablePointersStorageBuffer = VK_TRUE,
    };
    VkDeviceCreateInfo variable_pointer_device_info = device_info;
    variable_pointer_device_info.pNext = &enabled_variable_pointers;
    VkDevice variable_pointer_device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &variable_pointer_device_info, NULL,
                          &variable_pointer_device) == VK_SUCCESS);
    assert(variable_pointer_device != VK_NULL_HANDLE);
    vkDestroyDevice(variable_pointer_device, NULL);
    VkPhysicalDeviceFeatures legacy_geometry_features = {
        .robustBufferAccess = VK_TRUE,
        .depthBiasClamp = VK_TRUE,
        .depthClamp = VK_TRUE,
        .drawIndirectFirstInstance = VK_TRUE,
        .dualSrcBlend = VK_TRUE,
        .fragmentStoresAndAtomics = VK_TRUE,
        .fillModeNonSolid = VK_TRUE,
        .geometryShader = VK_TRUE,
        .imageCubeArray = VK_TRUE,
        .multiViewport = VK_TRUE,
        .independentBlend = VK_TRUE,
        .largePoints = VK_TRUE,
        .logicOp = VK_TRUE,
        .multiDrawIndirect = VK_TRUE,
        .occlusionQueryPrecise = VK_TRUE,
        .samplerAnisotropy = VK_TRUE,
        .shaderClipDistance = VK_TRUE,
        .shaderCullDistance = VK_TRUE,
        .shaderImageGatherExtended = VK_TRUE,
        .vertexPipelineStoresAndAtomics = VK_TRUE,
        .wideLines = VK_TRUE,
    };
    VkDeviceCreateInfo legacy_geometry_device_info = device_info;
    legacy_geometry_device_info.pNext = NULL;
    legacy_geometry_device_info.pEnabledFeatures = &legacy_geometry_features;
    VkDevice legacy_geometry_device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &legacy_geometry_device_info, NULL,
                          &legacy_geometry_device) == VK_SUCCESS);
    assert(legacy_geometry_device != VK_NULL_HANDLE);
    vkDestroyDevice(legacy_geometry_device, NULL);
    VkDevice device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &device) == VK_SUCCESS);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, 0, 0, &queue);
    assert(queue != VK_NULL_HANDLE);
    assert(vkGetDeviceProcAddr(device, "vkCreateRenderPass2KHR") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCmdBeginRenderPass2KHR") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCmdNextSubpass2KHR") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCmdEndRenderPass2KHR") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCmdBindIndexBuffer2KHR") != NULL);
    assert(vkGetDeviceProcAddr(device,
        "vkGetRenderingAreaGranularityKHR") != NULL);
    assert(vkGetDeviceProcAddr(device,
        "vkGetDeviceImageSubresourceLayoutKHR") != NULL);
    assert(vkGetDeviceProcAddr(device,
        "vkGetImageSubresourceLayout2KHR") != NULL);
    assert(vkGetDeviceProcAddr(device,
        "vkCreateDescriptorUpdateTemplateKHR") != NULL);
    VkDevice peer_device = VK_NULL_HANDLE;
    assert(vkCreateDevice(physical, &device_info, NULL, &peer_device) ==
           VK_SUCCESS);
    VkQueue peer_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(peer_device, 0, 0, &peer_queue);
    assert(peer_queue != VK_NULL_HANDLE && peer_queue != queue);
    vkDestroyDevice(peer_device, NULL);
    assert(vkDeviceWaitIdle(device) == VK_SUCCESS);

    const VkSemaphoreTypeCreateInfo timeline_type = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 2,
    };
    const VkSemaphoreCreateInfo timeline_create = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timeline_type,
    };
    VkSemaphore timeline_semaphore = VK_NULL_HANDLE;
    assert(vkCreateSemaphore(device, &timeline_create, NULL,
                             &timeline_semaphore) == VK_SUCCESS);
    uint64_t timeline_value = 0;
    assert(vkGetSemaphoreCounterValue(device, timeline_semaphore,
                                      &timeline_value) == VK_SUCCESS);
    assert(timeline_value == 2);
    const VkSemaphoreWaitInfo wait_three = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &timeline_semaphore,
        .pValues = (const uint64_t[]){3},
    };
    assert(vkWaitSemaphores(device, &wait_three, 0) == VK_TIMEOUT);
    const VkSemaphoreSignalInfo signal_three = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = timeline_semaphore,
        .value = 3,
    };
    assert(vkSignalSemaphore(device, &signal_three) == VK_SUCCESS);
    const uint64_t wait_value = 3;
    const uint64_t signal_value = 5;
    const VkTimelineSemaphoreSubmitInfo timeline_submit = {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .waitSemaphoreValueCount = 1,
        .pWaitSemaphoreValues = &wait_value,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues = &signal_value,
    };
    const VkPipelineStageFlags timeline_wait_stage =
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const VkSubmitInfo timeline_queue_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = &timeline_submit,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &timeline_semaphore,
        .pWaitDstStageMask = &timeline_wait_stage,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &timeline_semaphore,
    };
    assert(vkQueueSubmit(queue, 1, &timeline_queue_submit,
                         VK_NULL_HANDLE) == VK_SUCCESS);
    assert(vkGetSemaphoreCounterValue(device, timeline_semaphore,
                                      &timeline_value) == VK_SUCCESS);
    assert(timeline_value == 5);
    const VkSemaphoreWaitInfo wait_five = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &timeline_semaphore,
        .pValues = &signal_value,
    };
    assert(vkWaitSemaphores(device, &wait_five, 1000000) == VK_SUCCESS);
    vkDestroySemaphore(device, timeline_semaphore, NULL);

    const VkAttachmentDescription2 attachment2 = {
        .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
        .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkAttachmentReference2 color_reference2 = {
        .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    };
    const VkSubpassDescription2 subpass2 = {
        .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_reference2,
    };
    const VkRenderPassCreateInfo2 render_pass_info2 = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
        .attachmentCount = 1,
        .pAttachments = &attachment2,
        .subpassCount = 1,
        .pSubpasses = &subpass2,
    };
    VkRenderPass render_pass2 = VK_NULL_HANDLE;
    assert(vkCreateRenderPass2(device, &render_pass_info2, NULL,
                               &render_pass2) == VK_SUCCESS);
    assert(render_pass2 != VK_NULL_HANDLE);
    vkDestroyRenderPass(device, render_pass2, NULL);

    const VkQueryPoolCreateInfo query_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = 2,
    };
    VkQueryPool query_pool;
    assert(vkCreateQueryPool(device, &query_info, NULL, &query_pool) == VK_SUCCESS);
    vkResetQueryPoolEXT(device, query_pool, 0, 2);
    uint64_t query_result[] = {UINT64_MAX, UINT64_MAX};
    assert(vkGetQueryPoolResults(device, query_pool, 0, 1,
        sizeof(query_result), query_result, sizeof(query_result),
        VK_QUERY_RESULT_64_BIT |
        VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_NOT_READY);
    assert(query_result[0] == UINT64_MAX && query_result[1] == 0u);
    vkDestroyQueryPool(device, query_pool, NULL);

    VkPipelineCacheCreateInfo cache_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    VkPipelineCache cache = VK_NULL_HANDLE;
    assert(vkCreatePipelineCache(device, &cache_info, NULL, &cache) == VK_SUCCESS);
    size_t cache_size = 0;
    assert(vkGetPipelineCacheData(device, cache, &cache_size, NULL) == VK_SUCCESS);
    assert(cache_size >= sizeof(VkPipelineCacheHeaderVersionOne));
    void *cache_data = malloc(cache_size);
    assert(cache_data != NULL);
    assert(vkGetPipelineCacheData(device, cache, &cache_size, cache_data) == VK_SUCCESS);
    VkPipelineCacheHeaderVersionOne *cache_header = cache_data;
    assert(cache_header->vendorID == 0x1002);
    VkPipelineCacheCreateInfo restored_info = cache_info;
    restored_info.initialDataSize = cache_size;
    restored_info.pInitialData = cache_data;
    VkPipelineCache restored_cache = VK_NULL_HANDLE;
    assert(vkCreatePipelineCache(device, &restored_info, NULL, &restored_cache) == VK_SUCCESS);
    assert(vkMergePipelineCaches(device, cache, 1, &restored_cache) == VK_SUCCESS);
    vkDestroyPipelineCache(device, restored_cache, NULL);
    vkDestroyPipelineCache(device, cache, NULL);
    free(cache_data);

    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    VkDeviceMemory memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &allocation, NULL, &memory) == VK_SUCCESS);
    void *mapped = NULL;
    assert(vkMapMemory(device, memory, 128, 256, 0, &mapped) == VK_SUCCESS);
    memset(mapped, 0x5a, 256);
    vkUnmapMemory(device, memory);
    vkFreeMemory(device, memory, NULL);

    assert(vkGetInstanceProcAddr(instance, "vkCreateDevice") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkAllocateMemory") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkGetDeviceProcAddr") ==
           (PFN_vkVoidFunction)vkGetDeviceProcAddr);
    assert(vkGetDeviceProcAddr(device, "vkCreateImage") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkCreateGraphicsPipelines") != NULL);
    assert(vkGetDeviceProcAddr(device, "vkResetQueryPoolEXT") != NULL);
    assert(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice") == NULL);
    assert(vkGetDeviceProcAddr(VK_NULL_HANDLE, "vkAllocateMemory") == NULL);
    assert(vkDeviceWaitIdle(device) == VK_SUCCESS);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
