#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
#include "vulkan_ps5_robust_vertex_spv.h"
#include "vulkan_ps5_robust_vertex_frag_spv.h"
#define vulkan_ps5_triangle_vert_spv vulkan_ps5_robust_vertex_spv
#define vulkan_ps5_triangle_frag_spv vulkan_ps5_robust_vertex_frag_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "robust_vertex_access"
#elif defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
#include "vulkan_ps5_vertex_pipeline_stores_atomics_vert_spv.h"
#include "vulkan_ps5_vertex_pipeline_stores_atomics_tesc_spv.h"
#include "vulkan_ps5_vertex_pipeline_stores_atomics_tese_spv.h"
#include "vulkan_ps5_vertex_pipeline_stores_atomics_geom_spv.h"
#include "vulkan_ps5_triangle_frag_spv.h"
#define vulkan_ps5_triangle_vert_spv \
    vulkan_ps5_vertex_pipeline_stores_atomics_vert_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "vertex_pipeline_stores_atomics"
#elif defined(VULKAN_PS5_CULL_DISTANCE_PROBE)
#include "vulkan_ps5_cull_distance_vert_spv.h"
#include "vulkan_ps5_triangle_frag_spv.h"
#define vulkan_ps5_triangle_vert_spv vulkan_ps5_cull_distance_vert_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "shader_cull_distance"
#elif defined(VULKAN_PS5_CLIP_DISTANCE_PROBE)
#include "vulkan_ps5_clip_distance_vert_spv.h"
#include "vulkan_ps5_triangle_frag_spv.h"
#define vulkan_ps5_triangle_vert_spv vulkan_ps5_clip_distance_vert_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "shader_clip_distance"
#elif defined(VULKAN_PS5_DEMOTE_PROBE)
#include "vulkan_ps5_demote_frag_spv.h"
#include "vulkan_ps5_demote_vert_spv.h"
#define vulkan_ps5_triangle_frag_spv vulkan_ps5_demote_frag_spv
#define vulkan_ps5_triangle_vert_spv vulkan_ps5_demote_vert_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "shader_demote"
#elif defined(VULKAN_PS5_WIDE_LINES_PROBE)
#include "vulkan_ps5_wide_lines_vert_spv.h"
#include "vulkan_ps5_non_solid_frag_spv.h"
#define vulkan_ps5_triangle_frag_spv vulkan_ps5_non_solid_frag_spv
#define vulkan_ps5_triangle_vert_spv vulkan_ps5_wide_lines_vert_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "wide_lines"
#elif defined(VULKAN_PS5_LARGE_POINTS_PROBE)
#include "vulkan_ps5_large_points_vert_spv.h"
#include "vulkan_ps5_non_solid_frag_spv.h"
#define vulkan_ps5_triangle_frag_spv vulkan_ps5_non_solid_frag_spv
#define vulkan_ps5_triangle_vert_spv vulkan_ps5_large_points_vert_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "large_points"
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
#include "vulkan_ps5_non_solid_frag_spv.h"
#include "vulkan_ps5_non_solid_vert_spv.h"
#define vulkan_ps5_triangle_frag_spv vulkan_ps5_non_solid_frag_spv
#define vulkan_ps5_triangle_vert_spv vulkan_ps5_non_solid_vert_spv
#include "../system_service_exit.h"
#define SAMPLE_LABEL "fill_mode_non_solid"
#else
#include "vulkan_ps5_triangle_frag_spv.h"
#include "vulkan_ps5_triangle_vert_spv.h"
#if defined(VULKAN_PS5_LOGIC_OP_PROBE)
#include "../system_service_exit.h"
#define SAMPLE_LABEL "logic_op"
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
#include "vulkan_ps5_tess_control_spv.h"
#include "vulkan_ps5_tess_evaluation_spv.h"
#define SAMPLE_LABEL "tessellation"
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
#include "vulkan_ps5_geometry_spv.h"
#define SAMPLE_LABEL "geometry"
#else
#define SAMPLE_LABEL "triangle"
#endif
#endif

#define TARGET_WIDTH 256u
#define TARGET_HEIGHT 256u
#define GREEN_RGBA8 0xff00ff00u
#define RED_RGBA8 0xff0000ffu
#define LOGIC_BACKGROUND_RGBA8 0x55aa33ccu
#define LOGIC_XOR_RGBA8 0xaaaaccccu

#if defined(VULKAN_PS5_TESSELLATION_SAMPLE)
typedef struct TessellationHullProbe {
    uint32_t position[3][4];
    uint32_t executed[3];
    uint32_t padding;
    uint32_t tes_executed;
    uint32_t tes_padding[3];
    uint32_t tes_position[3][4];
} TessellationHullProbe;
_Static_assert(sizeof(TessellationHullProbe) == 128u,
    "tessellation probe must match the std430 shader layout");
#endif

#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
typedef struct VertexPipelineStageProbe {
    uint32_t atomic_markers[4];
    uint32_t store_markers[4];
} VertexPipelineStageProbe;
_Static_assert(sizeof(VertexPipelineStageProbe) == 32u,
    "vertex-pipeline probe must match the std430 shader layout");
#endif

#define VK_CHECK(expression) do { \
    VkResult check_result = (expression); \
    if (check_result != VK_SUCCESS) { \
        printf(SAMPLE_LABEL ": %s failed (%d)\n", #expression, check_result); \
        return 1; \
    } \
} while (0)

static uint32_t find_host_visible_memory_type(
    VkPhysicalDevice physical, uint32_t compatible_types)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if ((compatible_types & (1u << i)) != 0 &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
            return i;
    }
    return UINT32_MAX;
}

int main(void)
{
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView image_view;
    VkRenderPass render_pass;
    VkFramebuffer framebuffer;
    VkShaderModule vertex_shader;
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    VkShaderModule tess_control_shader;
    VkShaderModule tess_evaluation_shader;
    VkShaderModule geometry_shader;
    VkBuffer stage_probe_buffer;
    VkDeviceMemory stage_probe_memory;
    VkDescriptorSetLayout stage_probe_set_layout;
    VkDescriptorPool stage_probe_descriptor_pool;
    VkDescriptorSet stage_probe_descriptor_set;
    void *stage_probe_mapped;
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    VkShaderModule tess_control_shader;
    VkShaderModule tess_evaluation_shader;
    VkBuffer hull_probe_buffer;
    VkDeviceMemory hull_probe_memory;
    VkDescriptorSetLayout hull_probe_set_layout;
    VkDescriptorPool hull_probe_descriptor_pool;
    VkDescriptorSet hull_probe_descriptor_set;
    void *hull_probe_mapped;
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    VkShaderModule geometry_shader;
#endif
    VkShaderModule fragment_shader;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
    VkBuffer robust_vertex_buffer;
    VkDeviceMemory robust_vertex_memory;
#endif
#if defined(VULKAN_PS5_WIDE_LINES_PROBE)
    VkPipeline dynamic_line_pipeline;
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
    VkPipeline point_pipeline;
#endif
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkFence fence;
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    VkQueryPool query_pool;
#endif
    void *mapped;

    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t physical_count = 1u;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, &physical));
    if (physical_count != 1u) {
        printf(SAMPLE_LABEL ": expected one physical device\n");
        return 1;
    }
#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.robustBufferAccess) {
        printf("robust_vertex_access: robustBufferAccess is unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .robustBufferAccess = VK_TRUE,
    };
#elif defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.geometryShader ||
        !supported_features.tessellationShader ||
        !supported_features.vertexPipelineStoresAndAtomics) {
        printf("vertex_pipeline_stores_atomics: prerequisite stages are unavailable\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .geometryShader = VK_TRUE,
        .tessellationShader = VK_TRUE,
        .vertexPipelineStoresAndAtomics = VK_TRUE,
    };
#elif defined(VULKAN_PS5_DEMOTE_PROBE)
    VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT demote_features = {
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT,
    };
    VkPhysicalDeviceFeatures2 supported_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &demote_features,
    };
    vkGetPhysicalDeviceFeatures2(physical, &supported_features);
    if (!demote_features.shaderDemoteToHelperInvocation) {
        printf("shader_demote: shaderDemoteToHelperInvocation is not supported\n");
        return 1;
    }
#elif defined(VULKAN_PS5_CULL_DISTANCE_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.shaderCullDistance) {
        printf("shader_cull_distance: shaderCullDistance is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .shaderCullDistance = VK_TRUE,
    };
#elif defined(VULKAN_PS5_CLIP_DISTANCE_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.shaderClipDistance) {
        printf("shader_clip_distance: shaderClipDistance is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .shaderClipDistance = VK_TRUE,
    };
#elif defined(VULKAN_PS5_WIDE_LINES_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.wideLines) {
        printf("wide_lines: wideLines is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .wideLines = VK_TRUE,
    };
#elif defined(VULKAN_PS5_LARGE_POINTS_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.largePoints) {
        printf("large_points: largePoints is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .largePoints = VK_TRUE,
    };
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.fillModeNonSolid) {
        printf("fill_mode_non_solid: fillModeNonSolid is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .fillModeNonSolid = VK_TRUE,
    };
#elif defined(VULKAN_PS5_LOGIC_OP_PROBE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.logicOp) {
        printf("logic_op: logicOp is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .logicOp = VK_TRUE,
    };
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.geometryShader) {
        printf("geometry: geometryShader is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .geometryShader = VK_TRUE,
    };
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.tessellationShader) {
        printf("tessellation: tessellationShader is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .tessellationShader = VK_TRUE,
    };
#elif defined(VULKAN_PS5_QUERY_SAMPLE)
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical, &supported_features);
    if (!supported_features.occlusionQueryPrecise) {
        printf("query: occlusionQueryPrecise is not supported\n");
        return 1;
    }
    const VkPhysicalDeviceFeatures enabled_features = {
        .occlusionQueryPrecise = VK_TRUE,
    };
#endif
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
#if defined(VULKAN_PS5_DEMOTE_PROBE)
    const char *device_extensions[] = {
        VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
    };
    const VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT
        enabled_demote = {
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT,
            .shaderDemoteToHelperInvocation = VK_TRUE,
        };
#elif defined(VULKAN_PS5_QUERY_SAMPLE)
    const char *device_extensions[] = {
        VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
    };
    const VkPhysicalDeviceHostQueryResetFeatures host_reset = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .hostQueryReset = VK_TRUE,
    };
#endif
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
#if defined(VULKAN_PS5_DEMOTE_PROBE)
        .pNext = &enabled_demote,
#elif defined(VULKAN_PS5_QUERY_SAMPLE)
        .pNext = &host_reset,
#endif
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
#if defined(VULKAN_PS5_WIDE_LINES_PROBE) || \
    defined(VULKAN_PS5_ROBUST_VERTEX_PROBE) || \
    defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE) || \
    defined(VULKAN_PS5_CULL_DISTANCE_PROBE) || \
    defined(VULKAN_PS5_CLIP_DISTANCE_PROBE) || \
    defined(VULKAN_PS5_LARGE_POINTS_PROBE) || \
    defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE) || \
    defined(VULKAN_PS5_LOGIC_OP_PROBE) || \
    defined(VULKAN_PS5_GEOMETRY_SAMPLE) || \
    defined(VULKAN_PS5_TESSELLATION_SAMPLE) || \
    defined(VULKAN_PS5_QUERY_SAMPLE)
        .pEnabledFeatures = &enabled_features,
#endif
#if defined(VULKAN_PS5_DEMOTE_PROBE) || defined(VULKAN_PS5_QUERY_SAMPLE)
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
#endif
    };
    VK_CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    const VkQueryPoolCreateInfo query_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = 1,
    };
    VK_CHECK(vkCreateQueryPool(device, &query_info, NULL, &query_pool));
    vkResetQueryPoolEXT(device, query_pool, 0, 1);
#endif

    const VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {TARGET_WIDTH, TARGET_HEIGHT, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    VK_CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    uint32_t memory_type = find_host_visible_memory_type(
        physical, requirements.memoryTypeBits);
    if (memory_type == UINT32_MAX) {
        printf(SAMPLE_LABEL ": no host-visible compatible memory type\n");
        return 1;
    }
    const VkMemoryAllocateInfo memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    VK_CHECK(vkAllocateMemory(device, &memory_info, NULL, &memory));
    VK_CHECK(vkBindImageMemory(device, image, memory, 0));
    VK_CHECK(vkMapMemory(device, memory, 0, requirements.size, 0, &mapped));
#if defined(VULKAN_PS5_LOGIC_OP_PROBE)
    for (size_t word = 0u;
         word < requirements.size / sizeof(uint32_t); ++word)
        ((uint32_t *)mapped)[word] = LOGIC_BACKGROUND_RGBA8;
#else
    memset(mapped, 0, (size_t)requirements.size);
#endif
    const VkMappedMemoryRange mapped_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory,
        .offset = 0,
        .size = requirements.size,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &mapped_range));

#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
    const VkBufferCreateInfo robust_vertex_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 2u * sizeof(uint32_t),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(device, &robust_vertex_buffer_info, NULL,
        &robust_vertex_buffer));
    VkMemoryRequirements robust_vertex_requirements;
    vkGetBufferMemoryRequirements(device, robust_vertex_buffer,
        &robust_vertex_requirements);
    uint32_t robust_vertex_memory_type = find_host_visible_memory_type(
        physical, robust_vertex_requirements.memoryTypeBits);
    if (robust_vertex_memory_type == UINT32_MAX) {
        printf("robust_vertex_access: no host-visible vertex memory type\n");
        return 1;
    }
    const VkMemoryAllocateInfo robust_vertex_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = robust_vertex_requirements.size,
        .memoryTypeIndex = robust_vertex_memory_type,
    };
    VK_CHECK(vkAllocateMemory(device, &robust_vertex_memory_info, NULL,
        &robust_vertex_memory));
    VK_CHECK(vkBindBufferMemory(device, robust_vertex_buffer,
        robust_vertex_memory, 0));
#endif

#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    const VkBufferCreateInfo stage_probe_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(VertexPipelineStageProbe),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(device, &stage_probe_buffer_info, NULL,
        &stage_probe_buffer));
    VkMemoryRequirements stage_probe_requirements;
    vkGetBufferMemoryRequirements(device, stage_probe_buffer,
        &stage_probe_requirements);
    uint32_t stage_probe_memory_type = find_host_visible_memory_type(
        physical, stage_probe_requirements.memoryTypeBits);
    if (stage_probe_memory_type == UINT32_MAX) {
        printf("vertex_pipeline_stores_atomics: no host-visible probe memory type\n");
        return 1;
    }
    const VkMemoryAllocateInfo stage_probe_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stage_probe_requirements.size,
        .memoryTypeIndex = stage_probe_memory_type,
    };
    VK_CHECK(vkAllocateMemory(device, &stage_probe_memory_info, NULL,
        &stage_probe_memory));
    VK_CHECK(vkBindBufferMemory(device, stage_probe_buffer,
        stage_probe_memory, 0));
    VK_CHECK(vkMapMemory(device, stage_probe_memory, 0,
        stage_probe_requirements.size, 0, &stage_probe_mapped));
    memset(stage_probe_mapped, 0, (size_t)stage_probe_requirements.size);
    const VkMappedMemoryRange stage_probe_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = stage_probe_memory,
        .size = stage_probe_requirements.size,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &stage_probe_range));
    const VkDescriptorSetLayoutBinding stage_probe_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
            VK_SHADER_STAGE_GEOMETRY_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo stage_probe_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &stage_probe_binding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &stage_probe_set_layout_info,
        NULL, &stage_probe_set_layout));
    const VkDescriptorPoolSize stage_probe_pool_size = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
    };
    const VkDescriptorPoolCreateInfo stage_probe_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &stage_probe_pool_size,
    };
    VK_CHECK(vkCreateDescriptorPool(device, &stage_probe_pool_info, NULL,
        &stage_probe_descriptor_pool));
    const VkDescriptorSetAllocateInfo stage_probe_set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = stage_probe_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &stage_probe_set_layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(device, &stage_probe_set_allocate_info,
        &stage_probe_descriptor_set));
    const VkDescriptorBufferInfo stage_probe_descriptor_buffer = {
        .buffer = stage_probe_buffer,
        .range = sizeof(VertexPipelineStageProbe),
    };
    const VkWriteDescriptorSet stage_probe_descriptor_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = stage_probe_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &stage_probe_descriptor_buffer,
    };
    vkUpdateDescriptorSets(device, 1, &stage_probe_descriptor_write, 0, NULL);
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    const VkBufferCreateInfo hull_probe_buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(TessellationHullProbe),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(device, &hull_probe_buffer_info, NULL,
        &hull_probe_buffer));
    VkMemoryRequirements hull_probe_requirements;
    vkGetBufferMemoryRequirements(device, hull_probe_buffer,
        &hull_probe_requirements);
    uint32_t hull_probe_memory_type = find_host_visible_memory_type(
        physical, hull_probe_requirements.memoryTypeBits);
    if (hull_probe_memory_type == UINT32_MAX) {
        printf("tessellation: no host-visible hull-probe memory type\n");
        return 1;
    }
    const VkMemoryAllocateInfo hull_probe_memory_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = hull_probe_requirements.size,
        .memoryTypeIndex = hull_probe_memory_type,
    };
    VK_CHECK(vkAllocateMemory(device, &hull_probe_memory_info, NULL,
        &hull_probe_memory));
    VK_CHECK(vkBindBufferMemory(device, hull_probe_buffer,
        hull_probe_memory, 0));
    VK_CHECK(vkMapMemory(device, hull_probe_memory, 0,
        hull_probe_requirements.size, 0, &hull_probe_mapped));
    memset(hull_probe_mapped, 0xcd, (size_t)hull_probe_requirements.size);
    const VkMappedMemoryRange hull_probe_range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = hull_probe_memory,
        .offset = 0,
        .size = hull_probe_requirements.size,
    };
    VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &hull_probe_range));
    const VkDescriptorSetLayoutBinding hull_probe_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo hull_probe_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &hull_probe_binding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(device, &hull_probe_set_layout_info,
        NULL, &hull_probe_set_layout));
    const VkDescriptorPoolSize hull_probe_pool_size = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
    };
    const VkDescriptorPoolCreateInfo hull_probe_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &hull_probe_pool_size,
    };
    VK_CHECK(vkCreateDescriptorPool(device, &hull_probe_pool_info, NULL,
        &hull_probe_descriptor_pool));
    const VkDescriptorSetAllocateInfo hull_probe_set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = hull_probe_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &hull_probe_set_layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(device, &hull_probe_set_allocate_info,
        &hull_probe_descriptor_set));
    const VkDescriptorBufferInfo hull_probe_descriptor_buffer = {
        .buffer = hull_probe_buffer,
        .offset = 0,
        .range = sizeof(TessellationHullProbe),
    };
    const VkWriteDescriptorSet hull_probe_descriptor_write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = hull_probe_descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &hull_probe_descriptor_buffer,
    };
    vkUpdateDescriptorSets(device, 1, &hull_probe_descriptor_write, 0, NULL);
#endif

    const VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &image_view));
    const VkAttachmentDescription attachment = {
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
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
    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass));
    const VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .pAttachments = &image_view,
        .width = TARGET_WIDTH,
        .height = TARGET_HEIGHT,
        .layers = 1,
    };
    VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));

    const VkShaderModuleCreateInfo vertex_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_triangle_vert_spv),
        .pCode = vulkan_ps5_triangle_vert_spv,
    };
    const VkShaderModuleCreateInfo fragment_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_triangle_frag_spv),
        .pCode = vulkan_ps5_triangle_frag_spv,
    };
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    const VkShaderModuleCreateInfo tess_control_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_vertex_pipeline_stores_atomics_tesc_spv),
        .pCode = vulkan_ps5_vertex_pipeline_stores_atomics_tesc_spv,
    };
    const VkShaderModuleCreateInfo tess_evaluation_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_vertex_pipeline_stores_atomics_tese_spv),
        .pCode = vulkan_ps5_vertex_pipeline_stores_atomics_tese_spv,
    };
    const VkShaderModuleCreateInfo geometry_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_vertex_pipeline_stores_atomics_geom_spv),
        .pCode = vulkan_ps5_vertex_pipeline_stores_atomics_geom_spv,
    };
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    const VkShaderModuleCreateInfo tess_control_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_tess_control_spv),
        .pCode = vulkan_ps5_tess_control_spv,
    };
    const VkShaderModuleCreateInfo tess_evaluation_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_tess_evaluation_spv),
        .pCode = vulkan_ps5_tess_evaluation_spv,
    };
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    const VkShaderModuleCreateInfo geometry_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_geometry_spv),
        .pCode = vulkan_ps5_geometry_spv,
    };
#endif
    VK_CHECK(vkCreateShaderModule(
        device, &vertex_shader_info, NULL, &vertex_shader));
    VK_CHECK(vkCreateShaderModule(
        device, &fragment_shader_info, NULL, &fragment_shader));
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    VK_CHECK(vkCreateShaderModule(
        device, &tess_control_shader_info, NULL, &tess_control_shader));
    VK_CHECK(vkCreateShaderModule(
        device, &tess_evaluation_shader_info, NULL,
        &tess_evaluation_shader));
    VK_CHECK(vkCreateShaderModule(
        device, &geometry_shader_info, NULL, &geometry_shader));
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    VK_CHECK(vkCreateShaderModule(
        device, &tess_control_shader_info, NULL, &tess_control_shader));
    VK_CHECK(vkCreateShaderModule(
        device, &tess_evaluation_shader_info, NULL,
        &tess_evaluation_shader));
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    VK_CHECK(vkCreateShaderModule(
        device, &geometry_shader_info, NULL, &geometry_shader));
#endif
    const VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
        .setLayoutCount = 1,
        .pSetLayouts = &stage_probe_set_layout,
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
        .setLayoutCount = 1,
        .pSetLayouts = &hull_probe_set_layout,
#endif
    };
    VK_CHECK(vkCreatePipelineLayout(
        device, &pipeline_layout_info, NULL, &pipeline_layout));
    const VkPipelineShaderStageCreateInfo stages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader,
            .pName = "main",
        },
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
            .module = tess_control_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
            .module = tess_evaluation_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
            .module = geometry_shader,
            .pName = "main",
        },
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
            .module = tess_control_shader,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
            .module = tess_evaluation_shader,
            .pName = "main",
        },
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
            .module = geometry_shader,
            .pName = "main",
        },
#endif
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader,
            .pName = "main",
        },
    };
#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
    const VkVertexInputBindingDescription robust_vertex_binding = {
        .binding = 3,
        .stride = 2u * sizeof(float),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription robust_vertex_attribute = {
        .location = 5,
        .binding = 3,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = 4u * sizeof(float),
    };
#endif
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &robust_vertex_binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &robust_vertex_attribute,
#endif
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE) || \
    defined(VULKAN_PS5_TESSELLATION_SAMPLE)
        .topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
#elif defined(VULKAN_PS5_WIDE_LINES_PROBE)
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
#elif defined(VULKAN_PS5_LARGE_POINTS_PROBE)
        .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
#else
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
#endif
    };
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE) || \
    defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    const VkPipelineTessellationStateCreateInfo tessellation_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .patchControlPoints = 3,
    };
#endif
    const VkViewport viewport = {
        0, 0, TARGET_WIDTH, TARGET_HEIGHT, 0, 1,
    };
    const VkRect2D scissor = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}};
    const VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
#if defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
        .polygonMode = VK_POLYGON_MODE_LINE,
#else
        .polygonMode = VK_POLYGON_MODE_FILL,
#endif
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
#if defined(VULKAN_PS5_WIDE_LINES_PROBE)
        .lineWidth = 8.0f,
#else
        .lineWidth = 1.0f,
#endif
    };
    const VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
#if defined(VULKAN_PS5_LOGIC_OP_PROBE)
        .logicOpEnable = VK_TRUE,
        .logicOp = VK_LOGIC_OP_XOR,
#endif
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };
    const VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = sizeof(stages) / sizeof(stages[0]),
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE) || \
    defined(VULKAN_PS5_TESSELLATION_SAMPLE)
        .pTessellationState = &tessellation_state,
#endif
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pColorBlendState = &blend,
        .layout = pipeline_layout,
        .renderPass = render_pass,
    };
    VK_CHECK(vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
#if defined(VULKAN_PS5_WIDE_LINES_PROBE)
    const VkDynamicState line_width_dynamic_state =
        VK_DYNAMIC_STATE_LINE_WIDTH;
    const VkPipelineDynamicStateCreateInfo line_width_dynamic_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1,
        .pDynamicStates = &line_width_dynamic_state,
    };
    VkGraphicsPipelineCreateInfo dynamic_line_pipeline_info = pipeline_info;
    dynamic_line_pipeline_info.pDynamicState = &line_width_dynamic_info;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &dynamic_line_pipeline_info, NULL, &dynamic_line_pipeline));
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
    VkPipelineRasterizationStateCreateInfo point_rasterization = rasterization;
    point_rasterization.polygonMode = VK_POLYGON_MODE_POINT;
    VkGraphicsPipelineCreateInfo point_pipeline_info = pipeline_info;
    point_pipeline_info.pRasterizationState = &point_rasterization;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
        &point_pipeline_info, NULL, &point_pipeline));
#endif

    const VkCommandPoolCreateInfo command_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VK_CHECK(vkCreateCommandPool(
        device, &command_pool_info, NULL, &command_pool));
    const VkCommandBufferAllocateInfo command_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vkAllocateCommandBuffers(
        device, &command_allocate_info, &command));
    const VkCommandBufferBeginInfo command_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VK_CHECK(vkBeginCommandBuffer(command, &command_begin));
    const VkRenderPassBeginInfo render_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {TARGET_WIDTH, TARGET_HEIGHT}},
    };
#if defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    vkCmdResetQueryPool(command, query_pool, 0, 1);
#endif
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    vkCmdBeginQuery(command, query_pool, 0, VK_QUERY_CONTROL_PRECISE_BIT);
#endif
#if !defined(VULKAN_PS5_QUERY_IDLE_ONLY)
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
    const VkDeviceSize robust_vertex_offset = 0u;
    vkCmdBindVertexBuffers(command, 3, 1, &robust_vertex_buffer,
        &robust_vertex_offset);
#endif
#if defined(VULKAN_PS5_WIDE_LINES_PROBE)
    vkCmdDraw(command, 2, 1, 0, 0);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        dynamic_line_pipeline);
    vkCmdSetLineWidth(command, 16.0f);
    vkCmdDraw(command, 2, 1, 2, 0);
    vkCmdSetLineWidth(command, 32.0f);
    vkCmdDraw(command, 2, 1, 4, 0);
#else
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout, 0, 1, &stage_probe_descriptor_set, 0, NULL);
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout, 0, 1, &hull_probe_descriptor_set, 0, NULL);
#endif
#if defined(VULKAN_PS5_CULL_DISTANCE_PROBE)
    vkCmdDraw(command, 6, 1, 0, 0);
#else
    vkCmdDraw(command, 3, 1, 0, 0);
#endif
#if defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
        point_pipeline);
    vkCmdDraw(command, 3, 1, 0, 1);
#endif
#endif
#endif
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    vkCmdEndQuery(command, query_pool, 0);
#endif
    vkCmdEndRenderPass(command);
    VK_CHECK(vkEndCommandBuffer(command));

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    printf("query: stage submit\n");
#endif
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, fence));
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    printf("query: stage submitted\n");
#endif
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    printf("query: stage fence\n");
#endif
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1, &mapped_range));
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1, &stage_probe_range));
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    VK_CHECK(vkInvalidateMappedMemoryRanges(device, 1, &hull_probe_range));
#endif

    const uint32_t *pixels = mapped;
    uint32_t green_count = 0;
#if defined(VULKAN_PS5_DEMOTE_PROBE) || \
    defined(VULKAN_PS5_WIDE_LINES_PROBE) || \
    defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE) || \
    defined(VULKAN_PS5_LARGE_POINTS_PROBE)
    uint32_t red_count = 0;
#endif
#if defined(VULKAN_PS5_DEMOTE_PROBE)
    uint32_t demoted_count = 0;
    uint32_t red_positions[64];
    uint32_t red_position_count = 0;
#endif
#if defined(VULKAN_PS5_DEMOTE_PROBE) || \
    defined(VULKAN_PS5_WIDE_LINES_PROBE) || \
    defined(VULKAN_PS5_LARGE_POINTS_PROBE) || \
    defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
    uint32_t blue_count = 0;
#endif
#if defined(VULKAN_PS5_LOGIC_OP_PROBE)
    uint32_t background_count = 0;
#endif
    uint32_t unexpected_count = 0;
    for (uint32_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i) {
#if defined(VULKAN_PS5_LOGIC_OP_PROBE)
        if (pixels[i] == LOGIC_XOR_RGBA8)
            ++green_count;
        else if (pixels[i] == LOGIC_BACKGROUND_RGBA8)
            ++background_count;
        else
            ++unexpected_count;
#else
        if (pixels[i] == GREEN_RGBA8)
            ++green_count;
#if defined(VULKAN_PS5_DEMOTE_PROBE) || \
    defined(VULKAN_PS5_WIDE_LINES_PROBE) || \
    defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE) || \
    defined(VULKAN_PS5_LARGE_POINTS_PROBE)
        else if (pixels[i] == RED_RGBA8) {
            ++red_count;
#if defined(VULKAN_PS5_DEMOTE_PROBE)
            if (red_position_count < 64u)
                red_positions[red_position_count++] = i;
#endif
        }
#endif
#if defined(VULKAN_PS5_DEMOTE_PROBE) || \
    defined(VULKAN_PS5_WIDE_LINES_PROBE) || \
    defined(VULKAN_PS5_LARGE_POINTS_PROBE) || \
    defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
        else if (pixels[i] == 0xffff0000u)
            ++blue_count;
#endif
#if defined(VULKAN_PS5_DEMOTE_PROBE)
        else if (pixels[i] == 0u)
            ++demoted_count;
#endif
        else if (pixels[i] != 0u)
            ++unexpected_count;
#endif
    }
    int status = 0;
    uint32_t center = pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
        TARGET_WIDTH / 2u];
#if defined(VULKAN_PS5_CULL_DISTANCE_PROBE)
    uint32_t cull_left = pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
        TARGET_WIDTH / 4u];
    uint32_t cull_right = pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
        (TARGET_WIDTH * 3u) / 4u];
#endif
#if defined(VULKAN_PS5_CLIP_DISTANCE_PROBE)
    uint32_t clip_left = pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
        TARGET_WIDTH / 4u];
    uint32_t clip_right = pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
        (TARGET_WIDTH * 9u) / 16u];
#endif
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    const VertexPipelineStageProbe expected_stage_probe = {
        .atomic_markers = {
            0xa7010001u, 0xa7020002u, 0xa7030003u, 0xa7040004u,
        },
        .store_markers = {
            0x57010001u, 0x57020002u, 0x57030003u, 0x57040004u,
        },
    };
    const VertexPipelineStageProbe *stage_probe = stage_probe_mapped;
    int stage_probe_ok = memcmp(stage_probe, &expected_stage_probe,
        sizeof(expected_stage_probe)) == 0;
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    const TessellationHullProbe expected_hull_probe = {
        .position = {
            {0xbf400000u, 0xbf400000u, 0u, 0x3f800000u},
            {0x3f400000u, 0xbf400000u, 0u, 0x3f800000u},
            {0u, 0x3f400000u, 0u, 0x3f800000u},
        },
        .executed = {0x48530000u, 0x48530001u, 0x48530002u},
        .padding = 0xcdcdcdcdu,
        .tes_executed = 0x54455300u,
        .tes_padding = {0xcdcdcdcdu, 0xcdcdcdcdu, 0xcdcdcdcdu},
        .tes_position = {
            {0xbf400000u, 0xbf400000u, 0u, 0x3f800000u},
            {0x3f400000u, 0xbf400000u, 0u, 0x3f800000u},
            {0u, 0x3f400000u, 0u, 0x3f800000u},
        },
    };
    const TessellationHullProbe *hull_probe = hull_probe_mapped;
    int hull_probe_ok = memcmp(hull_probe->position,
            expected_hull_probe.position, sizeof(hull_probe->position)) == 0 &&
        memcmp(hull_probe->executed, expected_hull_probe.executed,
            sizeof(hull_probe->executed)) == 0 &&
        hull_probe->padding == expected_hull_probe.padding;
    int tes_probe_ok =
        hull_probe->tes_executed == expected_hull_probe.tes_executed &&
        memcmp(hull_probe->tes_padding, expected_hull_probe.tes_padding,
            sizeof(hull_probe->tes_padding)) == 0 &&
        memcmp(hull_probe->tes_position, expected_hull_probe.tes_position,
            sizeof(hull_probe->tes_position)) == 0;
    printf("tessellation: hull probe %s executed=%08x,%08x,%08x\n",
        hull_probe_ok ? "PASS" : "FAIL",
        hull_probe->executed[0], hull_probe->executed[1],
        hull_probe->executed[2]);
    for (uint32_t probe_vertex = 0; probe_vertex < 3u; ++probe_vertex)
        printf("tessellation: hull position%u=%08x,%08x,%08x,%08x\n",
            probe_vertex, hull_probe->position[probe_vertex][0],
            hull_probe->position[probe_vertex][1],
            hull_probe->position[probe_vertex][2],
            hull_probe->position[probe_vertex][3]);
    printf("tessellation: TES probe %s executed=%08x\n",
        tes_probe_ok ? "PASS" : "FAIL", hull_probe->tes_executed);
    for (uint32_t probe_vertex = 0; probe_vertex < 3u; ++probe_vertex)
        printf("tessellation: TES position%u=%08x,%08x,%08x,%08x\n",
            probe_vertex, hull_probe->tes_position[probe_vertex][0],
            hull_probe->tes_position[probe_vertex][1],
            hull_probe->tes_position[probe_vertex][2],
            hull_probe->tes_position[probe_vertex][3]);
#endif
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
    uint64_t query_data[2] = {0, 0};
    VkResult query_result = vkGetQueryPoolResults(device, query_pool, 0, 1,
        sizeof(query_data), query_data, sizeof(query_data),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    printf("query: stage result=%d samples=%llu available=%llu\n",
        query_result, (unsigned long long)query_data[0],
        (unsigned long long)query_data[1]);
#endif
    int image_ok;
#if defined(VULKAN_PS5_QUERY_IDLE_ONLY)
    image_ok = green_count == 0u && unexpected_count == 0u && center == 0u &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#else
#if defined(VULKAN_PS5_LOGIC_OP_PROBE)
    image_ok = green_count >= 16000u && green_count <= 21000u &&
        background_count + green_count == TARGET_WIDTH * TARGET_HEIGHT &&
        unexpected_count == 0u && center == LOGIC_XOR_RGBA8 &&
        pixels[0] == LOGIC_BACKGROUND_RGBA8 &&
        pixels[TARGET_WIDTH - 1u] == LOGIC_BACKGROUND_RGBA8;
#elif defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    image_ok = green_count == 7200u && unexpected_count == 0u &&
        center == GREEN_RGBA8 && pixels[0] == 0u &&
        pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    image_ok = green_count >= 6000u && green_count <= 8500u &&
        unexpected_count == 0u && center == GREEN_RGBA8 &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    image_ok = green_count >= 3500u && green_count <= 6000u &&
        unexpected_count == 0u && center == GREEN_RGBA8 &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
    image_ok = green_count >= 150u && green_count <= 400u &&
        red_count >= 1u && red_count <= 12u && unexpected_count == 0u &&
        pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH + TARGET_WIDTH / 4u] == 0u &&
        pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
            (TARGET_WIDTH * 3u) / 4u] == 0u &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_CULL_DISTANCE_PROBE)
    image_ok = green_count == 4608u && unexpected_count == 0u &&
        cull_left == 0u && cull_right == GREEN_RGBA8 && center == 0u &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_CLIP_DISTANCE_PROBE)
    image_ok = green_count == 9216u && unexpected_count == 0u &&
        clip_left == 0u && clip_right == GREEN_RGBA8 &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_DEMOTE_PROBE)
    image_ok = green_count == 2048u && red_count == 0u &&
        blue_count == 30720u && demoted_count == 32768u &&
        unexpected_count == 0u && center == 0u &&
        pixels[(TARGET_HEIGHT / 2u) * TARGET_WIDTH +
            TARGET_WIDTH / 2u + 1u] == 0xffff0000u;
#elif defined(VULKAN_PS5_WIDE_LINES_PROBE)
    image_ok = green_count == 1024u && red_count == 2048u &&
        blue_count == 4096u && unexpected_count == 0u &&
        center == RED_RGBA8 && pixels[0] == 0u &&
        pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_LARGE_POINTS_PROBE)
    image_ok = green_count == 64u && red_count == 256u &&
        blue_count == 1024u && unexpected_count == 0u &&
        center == RED_RGBA8 && pixels[0] == 0u &&
        pixels[TARGET_WIDTH - 1u] == 0u;
#elif defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
#if !defined(OPENAGC_PROSPERO)
    image_ok = 1;
#else
    image_ok = blue_count >= 16000u && blue_count <= 21000u &&
        green_count == 0u && unexpected_count == 0u &&
        center == 0xffff0000u && pixels[0] == 0u &&
        pixels[TARGET_WIDTH - 1u] == 0u;
#endif
#else
    image_ok = green_count >= 16000u && green_count <= 21000u &&
        unexpected_count == 0u && center == GREEN_RGBA8 &&
        pixels[0] == 0u && pixels[TARGET_WIDTH - 1u] == 0u;
#endif
#endif
    if (!image_ok
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
        || !stage_probe_ok
#endif
#if defined(VULKAN_PS5_TESSELLATION_SAMPLE)
        || !hull_probe_ok || !tes_probe_ok
#endif
#if defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
        || query_result != VK_SUCCESS || query_data[1] != 1u ||
        query_data[0] != green_count
#endif
        ) {
#if defined(VULKAN_PS5_QUERY_IDLE_ONLY)
        printf("query_idle: mismatch result=%d samples=%llu available=%llu green=%u unexpected=%u\n",
            query_result, (unsigned long long)query_data[0],
            (unsigned long long)query_data[1], green_count, unexpected_count);
#elif defined(VULKAN_PS5_QUERY_SAMPLE) && \
    !defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY) && \
    !defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
        printf("query: mismatch result=%d samples=%llu available=%llu green=%u unexpected=%u\n",
            query_result, (unsigned long long)query_data[0],
            (unsigned long long)query_data[1], green_count, unexpected_count);
#elif defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
        printf("vertex_pipeline_stores_atomics: mismatch green=%u unexpected=%u atomic=%08x,%08x,%08x,%08x stores=%08x,%08x,%08x,%08x\n",
            green_count, unexpected_count,
            stage_probe->atomic_markers[0], stage_probe->atomic_markers[1],
            stage_probe->atomic_markers[2], stage_probe->atomic_markers[3],
            stage_probe->store_markers[0], stage_probe->store_markers[1],
            stage_probe->store_markers[2], stage_probe->store_markers[3]);
#elif defined(VULKAN_PS5_LOGIC_OP_PROBE)
        printf("logic_op: mismatch xor=%u background=%u unexpected=%u center=%08x\n",
            green_count, background_count, unexpected_count, center);
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
        printf("fill_mode_non_solid: mismatch line=%u point=%u unexpected=%u center=%08x\n",
            green_count, red_count, unexpected_count, center);
#elif defined(VULKAN_PS5_CULL_DISTANCE_PROBE)
        printf("shader_cull_distance: mismatch green=%u unexpected=%u left=%08x right=%08x center=%08x\n",
            green_count, unexpected_count, cull_left, cull_right, center);
#elif defined(VULKAN_PS5_CLIP_DISTANCE_PROBE)
        printf("shader_clip_distance: mismatch green=%u unexpected=%u left=%08x right=%08x center=%08x\n",
            green_count, unexpected_count, clip_left, clip_right, center);
#elif defined(VULKAN_PS5_DEMOTE_PROBE)
        printf("shader_demote: mismatch green=%u red=%u blue=%u demoted=%u unexpected=%u center=%08x\n",
            green_count, red_count, blue_count, demoted_count,
            unexpected_count, center);
        for (uint32_t i = 0; i < red_position_count; ++i)
            printf("shader_demote: red_pixel x=%u y=%u\n",
                red_positions[i] % TARGET_WIDTH,
                red_positions[i] / TARGET_WIDTH);
#elif defined(VULKAN_PS5_WIDE_LINES_PROBE)
        printf("wide_lines: mismatch width8=%u width16=%u width32=%u unexpected=%u center=%08x\n",
            green_count, red_count, blue_count, unexpected_count, center);
#elif defined(VULKAN_PS5_LARGE_POINTS_PROBE)
        printf("large_points: mismatch size8=%u size16=%u size32=%u unexpected=%u center=%08x\n",
            green_count, red_count, blue_count, unexpected_count, center);
#elif defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
        printf("robust_vertex_access: mismatch blue=%u green=%u unexpected=%u center=%08x\n",
            blue_count, green_count, unexpected_count, center);
#else
        printf(SAMPLE_LABEL ": mismatch green=%u unexpected=%u center=%08x\n",
            green_count, unexpected_count, center);
#endif
        status = 1;
    } else {
#if defined(VULKAN_PS5_QUERY_LIFECYCLE_ONLY)
        printf("query_lifecycle: PASS green=%u\n", green_count);
#elif defined(VULKAN_PS5_QUERY_COMMAND_RESET_ONLY)
        printf("query_reset: PASS green=%u\n", green_count);
#elif defined(VULKAN_PS5_QUERY_IDLE_ONLY)
        printf("query_idle: PASS samples=%llu available=%llu\n",
            (unsigned long long)query_data[0],
            (unsigned long long)query_data[1]);
#elif defined(VULKAN_PS5_QUERY_SAMPLE)
        printf("query: PASS samples=%llu green=%u\n",
            (unsigned long long)query_data[0], green_count);
#elif defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
        printf("vertex_pipeline_stores_atomics: PASS green=%u stages=VS,TCS,TES,GS atomic=4 stores=4\n",
            green_count);
#elif defined(VULKAN_PS5_LOGIC_OP_PROBE)
        printf("logic_op: PASS xor=%u background=%u center=%08x\n",
            green_count, background_count, center);
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
        printf("fill_mode_non_solid: PASS line=%u point=%u center=%08x\n",
            green_count, red_count, center);
#elif defined(VULKAN_PS5_CULL_DISTANCE_PROBE)
        printf("shader_cull_distance: PASS green=%u left=%08x right=%08x\n",
            green_count, cull_left, cull_right);
#elif defined(VULKAN_PS5_CLIP_DISTANCE_PROBE)
        printf("shader_clip_distance: PASS green=%u left=%08x right=%08x\n",
            green_count, clip_left, clip_right);
#elif defined(VULKAN_PS5_DEMOTE_PROBE)
        printf("shader_demote: PASS green=%u blue=%u demoted=%u center=%08x\n",
            green_count, blue_count, demoted_count, center);
#elif defined(VULKAN_PS5_WIDE_LINES_PROBE)
        printf("wide_lines: PASS width8=%u width16=%u width32=%u center=%08x\n",
            green_count, red_count, blue_count, center);
#elif defined(VULKAN_PS5_LARGE_POINTS_PROBE)
        printf("large_points: PASS size8=%u size16=%u size32=%u center=%08x\n",
            green_count, red_count, blue_count, center);
#elif defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
#if !defined(OPENAGC_PROSPERO)
        printf("robust_vertex_access: PASS command recording\n");
#else
        printf("robust_vertex_access: PASS OOB attribute=0 blue=%u\n",
            blue_count);
#endif
#else
        printf(SAMPLE_LABEL ": PASS %u green pixels\n", green_count);
#endif
    }

    vkDestroyFence(device, fence, NULL);
#if defined(VULKAN_PS5_QUERY_SAMPLE)
    vkDestroyQueryPool(device, query_pool, NULL);
#endif
    vkDestroyCommandPool(device, command_pool, NULL);
#if defined(VULKAN_PS5_WIDE_LINES_PROBE)
    vkDestroyPipeline(device, dynamic_line_pipeline, NULL);
#elif defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE)
    vkDestroyPipeline(device, point_pipeline, NULL);
#endif
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
#if defined(VULKAN_PS5_ROBUST_VERTEX_PROBE)
    vkDestroyBuffer(device, robust_vertex_buffer, NULL);
    vkFreeMemory(device, robust_vertex_memory, NULL);
#endif
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    vkDestroyDescriptorPool(device, stage_probe_descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(device, stage_probe_set_layout, NULL);
    vkUnmapMemory(device, stage_probe_memory);
    vkDestroyBuffer(device, stage_probe_buffer, NULL);
    vkFreeMemory(device, stage_probe_memory, NULL);
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    vkDestroyDescriptorPool(device, hull_probe_descriptor_pool, NULL);
    vkDestroyDescriptorSetLayout(device, hull_probe_set_layout, NULL);
    vkUnmapMemory(device, hull_probe_memory);
    vkDestroyBuffer(device, hull_probe_buffer, NULL);
    vkFreeMemory(device, hull_probe_memory, NULL);
#endif
    vkDestroyShaderModule(device, fragment_shader, NULL);
#if defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE)
    vkDestroyShaderModule(device, geometry_shader, NULL);
    vkDestroyShaderModule(device, tess_evaluation_shader, NULL);
    vkDestroyShaderModule(device, tess_control_shader, NULL);
#elif defined(VULKAN_PS5_TESSELLATION_SAMPLE)
    vkDestroyShaderModule(device, tess_evaluation_shader, NULL);
    vkDestroyShaderModule(device, tess_control_shader, NULL);
#elif defined(VULKAN_PS5_GEOMETRY_SAMPLE)
    vkDestroyShaderModule(device, geometry_shader, NULL);
#endif
    vkDestroyShaderModule(device, vertex_shader, NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render_pass, NULL);
    vkDestroyImageView(device, image_view, NULL);
    vkUnmapMemory(device, memory);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
#if defined(OPENAGC_PROSPERO) && \
    (defined(VULKAN_PS5_VERTEX_PIPELINE_STORES_ATOMICS_PROBE) || \
     defined(VULKAN_PS5_ROBUST_VERTEX_PROBE) || \
     defined(VULKAN_PS5_LOGIC_OP_PROBE) || \
     defined(VULKAN_PS5_CULL_DISTANCE_PROBE) || \
     defined(VULKAN_PS5_CLIP_DISTANCE_PROBE) || \
     defined(VULKAN_PS5_DEMOTE_PROBE) || \
     defined(VULKAN_PS5_WIDE_LINES_PROBE) || \
     defined(VULKAN_PS5_FILL_MODE_NON_SOLID_PROBE) || \
     defined(VULKAN_PS5_LARGE_POINTS_PROBE))
    vulkan_ps5_system_service_exit(SAMPLE_LABEL);
#endif
    return status;
}
