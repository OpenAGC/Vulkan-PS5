#include "vulkan_ps5_internal.h"
#include "meta/clear_color_spv.h"
#include <openagc_psbc.h>
#include <agc_cb.h>
#include <agc_graphics.h>
#include <agc_memory.h>
#include <agc_registers.h>
#include <agc_shader.h>
#include <agc_texture.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct VkPs5Fence { atomic_bool signaled; } VkPs5Fence;
typedef struct VkPs5Semaphore {
    VkSemaphoreType type;
    atomic_bool signaled;
    atomic_uint_fast64_t value;
} VkPs5Semaphore;
typedef struct VkPs5Event { atomic_int status; } VkPs5Event;

static VkBool32 timeline_advance(VkPs5Semaphore *semaphore, uint64_t value)
{
    uint_fast64_t current = atomic_load(&semaphore->value);
    while (value > current) {
        if (atomic_compare_exchange_weak(
                &semaphore->value, &current, value))
            return VK_TRUE;
    }
    return VK_FALSE;
}

VkResult vk_ps5_signal_acquire(VkSemaphore semaphore_handle,
                               VkFence fence_handle) {
    if (!semaphore_handle && !fence_handle)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (semaphore_handle) {
        VkPs5Semaphore *semaphore = (VkPs5Semaphore *)semaphore_handle;
        if (semaphore->type != VK_SEMAPHORE_TYPE_BINARY)
            return VK_ERROR_INITIALIZATION_FAILED;
        atomic_store(&semaphore->signaled, true);
    }
    if (fence_handle)
        atomic_store(&((VkPs5Fence *)fence_handle)->signaled, true);
    return VK_SUCCESS;
}

VkResult vk_ps5_consume_semaphores(uint32_t semaphore_count,
                                   const VkSemaphore *semaphores) {
    if (semaphore_count && !semaphores)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < semaphore_count; ++i) {
        VkPs5Semaphore *semaphore = (VkPs5Semaphore *)semaphores[i];
        if (!semaphore || semaphore->type != VK_SEMAPHORE_TYPE_BINARY ||
            !atomic_load(&semaphore->signaled))
            return VK_NOT_READY;
    }
    for (uint32_t i = 0; i < semaphore_count; ++i)
        atomic_store(&((VkPs5Semaphore *)semaphores[i])->signaled, false);
    return VK_SUCCESS;
}

typedef struct VkPs5Buffer {
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkDeviceMemory memory;
    VkDeviceSize memory_offset;
    AgcBuffer native_buffer;
    AgcResourceUsage native_usage;
} VkPs5Buffer;

static AgcBufferUsageFlags native_buffer_usage(VkBufferUsageFlags usage) {
    AgcBufferUsageFlags native = 0u;
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        native |= AGC_BUFFER_USAGE_INDEX_BIT;
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        native |= AGC_BUFFER_USAGE_VERTEX_BIT;
    if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT))
        native |= AGC_BUFFER_USAGE_UNIFORM_BIT;
    if (usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
        native |= AGC_BUFFER_USAGE_STORAGE_BIT;
    if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        native |= AGC_BUFFER_USAGE_INDIRECT_BIT;
    if (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
        native |= AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        native |= AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    return native ? native : AGC_BUFFER_USAGE_STORAGE_BIT;
}

typedef struct VkPs5Image {
    VkDevice device;
    VkImageCreateFlags flags;
    VkImageType type;
    VkFormat format;
    VkExtent3D extent;
    uint32_t mip_levels;
    uint32_t array_layers;
    VkSampleCountFlagBits samples;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkDeviceSize row_pitch;
    VkDeviceSize depth_pitch;
    VkDeviceSize array_pitch;
    VkDeviceSize alignment;
    VkDeviceSize depth_plane_offset;
    VkDeviceSize stencil_plane_offset;
    VkBool32 is_msaa_color_surface;
    VkBool32 is_depth_surface;
    VkDeviceSize size;
    VkDeviceMemory memory;
    VkDeviceSize memory_offset;
    AgcImageDesc native_desc;
    AgcImageLayout native_layout;
    AgcImage native_image;
    AgcBuffer native_clear_buffer;
    AgcResourceUsage native_usage;
} VkPs5Image;

static bool native_image_format(VkFormat format, AgcFormat *native) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: *native = AGC_FORMAT_R8_UNORM; return true;
    case VK_FORMAT_R8G8_UNORM: *native = AGC_FORMAT_RG8_UNORM; return true;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        *native = AGC_FORMAT_RGBA8_UNORM; return true;
    case VK_FORMAT_B8G8R8A8_UNORM:
        *native = AGC_FORMAT_BGRA8_UNORM; return true;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        *native = AGC_FORMAT_RGB10A2_UNORM; return true;
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        *native = AGC_FORMAT_RGBA8_SRGB; return true;
    case VK_FORMAT_B8G8R8A8_SRGB:
        *native = AGC_FORMAT_BGRA8_SRGB; return true;
    case VK_FORMAT_R16_SFLOAT: *native = AGC_FORMAT_R16_FLOAT; return true;
    case VK_FORMAT_R16G16_SFLOAT:
        *native = AGC_FORMAT_RG16_FLOAT; return true;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        *native = AGC_FORMAT_RGBA16_FLOAT; return true;
    case VK_FORMAT_R32_SFLOAT: *native = AGC_FORMAT_R32_FLOAT; return true;
    case VK_FORMAT_R32G32_SFLOAT:
        *native = AGC_FORMAT_RG32_FLOAT; return true;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        *native = AGC_FORMAT_RGBA32_FLOAT; return true;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        *native = AGC_FORMAT_R11G11B10_FLOAT; return true;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        *native = AGC_FORMAT_BC1_UNORM; return true;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        *native = AGC_FORMAT_BC1_SRGB; return true;
    case VK_FORMAT_BC2_UNORM_BLOCK:
        *native = AGC_FORMAT_BC2_UNORM; return true;
    case VK_FORMAT_BC2_SRGB_BLOCK:
        *native = AGC_FORMAT_BC2_SRGB; return true;
    case VK_FORMAT_BC3_UNORM_BLOCK:
        *native = AGC_FORMAT_BC3_UNORM; return true;
    case VK_FORMAT_BC3_SRGB_BLOCK:
        *native = AGC_FORMAT_BC3_SRGB; return true;
    case VK_FORMAT_BC4_UNORM_BLOCK:
        *native = AGC_FORMAT_BC4_UNORM; return true;
    case VK_FORMAT_BC4_SNORM_BLOCK:
        *native = AGC_FORMAT_BC4_SNORM; return true;
    case VK_FORMAT_BC5_UNORM_BLOCK:
        *native = AGC_FORMAT_BC5_UNORM; return true;
    case VK_FORMAT_BC5_SNORM_BLOCK:
        *native = AGC_FORMAT_BC5_SNORM; return true;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        *native = AGC_FORMAT_BC6_UFLOAT; return true;
    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        *native = AGC_FORMAT_BC6_SFLOAT; return true;
    case VK_FORMAT_BC7_UNORM_BLOCK:
        *native = AGC_FORMAT_BC7_UNORM; return true;
    case VK_FORMAT_BC7_SRGB_BLOCK:
        *native = AGC_FORMAT_BC7_SRGB; return true;
    case VK_FORMAT_D16_UNORM: *native = AGC_FORMAT_D16_UNORM; return true;
    case VK_FORMAT_D32_SFLOAT: *native = AGC_FORMAT_D32_FLOAT; return true;
    case VK_FORMAT_S8_UINT: *native = AGC_FORMAT_S8_UINT; return true;
    case VK_FORMAT_D16_UNORM_S8_UINT:
        *native = AGC_FORMAT_D16_UNORM_S8_UINT; return true;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        *native = AGC_FORMAT_D32_FLOAT_S8_UINT; return true;
    default: return false;
    }
}

static AgcImageUsageFlags native_image_usage(VkImageUsageFlags usage,
                                              VkImageCreateFlags flags,
                                              bool depth) {
    AgcImageUsageFlags native = 0u;
    if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
        native |= AGC_IMAGE_USAGE_SAMPLED_BIT;
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
        native |= AGC_IMAGE_USAGE_STORAGE_BIT;
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        native |= AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    if (depth)
        native |= AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT;
    if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        native |= AGC_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        native |= AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
        native |= AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT;
    return native;
}

static bool native_image_is_depth(VkFormat format) {
    return format == VK_FORMAT_D16_UNORM ||
        format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_S8_UINT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static bool native_image_is_bc(VkFormat format) {
    AgcFormat native_format;
    if (!native_image_format(format, &native_format))
        return false;
    return native_format >= AGC_FORMAT_BC1_UNORM &&
        native_format <= AGC_FORMAT_BC7_SRGB;
}

static bool native_image_is_rgba8_clearable(VkFormat format)
{
    return format == VK_FORMAT_R8G8B8A8_UNORM ||
        format == VK_FORMAT_R8G8B8A8_SRGB ||
        format == VK_FORMAT_A8B8G8R8_UNORM_PACK32 ||
        format == VK_FORMAT_A8B8G8R8_SRGB_PACK32 ||
        format == VK_FORMAT_B8G8R8A8_UNORM ||
        format == VK_FORMAT_B8G8R8A8_SRGB;
}

static VkResult initialize_native_image_layout(VkDevice device,
                                                VkPs5Image *image) {
    AgcFormat format;
    AgcImageSubresourceLayout subresource =
        AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    if (!native_image_format(image->format, &format))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    image->native_desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    image->native_desc.width = image->extent.width;
    image->native_desc.height = image->extent.height;
    image->native_desc.depth = image->extent.depth;
    image->native_desc.mip_levels = image->mip_levels;
    image->native_desc.array_layers = image->array_layers;
    image->native_desc.format = format;
    image->native_desc.sample_count = image->samples;
    image->native_desc.tiling = image->tiling == VK_IMAGE_TILING_LINEAR ?
        AGC_IMAGE_TILING_LINEAR : AGC_IMAGE_TILING_OPTIMAL;
    image->native_desc.usage = native_image_usage(image->usage, image->flags,
        native_image_is_depth(image->format));
    if (image->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
        image->native_desc.flags |= AGC_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if (!image->native_desc.usage)
        image->native_desc.usage = AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    image->native_layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    int32_t result = agcGetImageLayout(vk_ps5_native_device(device),
        &image->native_desc, &image->native_layout);
    if (result == AGC_OK)
        result = agcGetImageSubresourceLayout(vk_ps5_native_device(device),
            &image->native_desc, 0u, 0u, 0u, &subresource);
    if (result != AGC_OK)
        return result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_FORMAT_NOT_SUPPORTED;
    image->size = image->native_layout.allocation_size;
    image->alignment = image->native_layout.alignment;
    image->row_pitch = subresource.row_pitch;
    image->depth_pitch = subresource.slice_pitch;
    image->array_pitch = subresource.size;
    if (native_image_is_depth(image->format) &&
        image->format != VK_FORMAT_S8_UINT)
        image->depth_plane_offset = subresource.offset;
    if (image->format == VK_FORMAT_S8_UINT)
        image->stencil_plane_offset = subresource.offset;
    if (image->format == VK_FORMAT_D16_UNORM_S8_UINT ||
        image->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        subresource = (AgcImageSubresourceLayout)
            AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        result = agcGetImageSubresourceLayout(vk_ps5_native_device(device),
            &image->native_desc, 0u, 0u, 1u, &subresource);
        if (result != AGC_OK)
            return result == AGC_ERROR_OUT_OF_MEMORY ?
                VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_FORMAT_NOT_SUPPORTED;
        image->stencil_plane_offset = subresource.offset;
    }
    return VK_SUCCESS;
}

VkResult vk_ps5_enable_image_scanout(VkImage image_handle)
{
    VkPs5Image *image = (VkPs5Image *)image_handle;
    AgcImageSubresourceLayout subresource =
        AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    if (!image || image->memory || image->native_image ||
        image->type != VK_IMAGE_TYPE_2D ||
        (image->format != VK_FORMAT_B8G8R8A8_SRGB &&
         image->format != VK_FORMAT_B8G8R8A8_UNORM) ||
        image->extent.width != 1920u || image->extent.height != 1080u ||
        image->extent.depth != 1u || image->mip_levels != 1u ||
        image->array_layers != 1u || image->samples != VK_SAMPLE_COUNT_1_BIT ||
        image->tiling != VK_IMAGE_TILING_LINEAR)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    /* VideoOut's qualified storage encoding is BGRA8_SRGB. A mutable Vulkan
     * swapchain may expose an UNORM base/view over the same compatible bytes,
     * but must not change the native scanout encoding. */
    image->native_desc.format = AGC_FORMAT_BGRA8_SRGB;
    image->native_desc.usage |= AGC_IMAGE_USAGE_SCANOUT_BIT;
    int32_t result = agcGetImageLayout(vk_ps5_native_device(image->device),
        &image->native_desc, &image->native_layout);
    if (result == AGC_OK)
        result = agcGetImageSubresourceLayout(
            vk_ps5_native_device(image->device), &image->native_desc,
            0u, 0u, 0u, &subresource);
    if (result != AGC_OK)
        return result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_FORMAT_NOT_SUPPORTED;
    image->size = image->native_layout.allocation_size;
    image->alignment = image->native_layout.alignment;
    image->row_pitch = subresource.row_pitch;
    image->depth_pitch = subresource.slice_pitch;
    image->array_pitch = subresource.size;
    return VK_SUCCESS;
}

AgcImage vk_ps5_native_image(VkImage image_handle)
{
    VkPs5Image *image = (VkPs5Image *)image_handle;
    return image ? image->native_image : NULL;
}

void vk_ps5_set_image_native_usage(VkImage image_handle,
                                   AgcResourceUsage usage)
{
    VkPs5Image *image = (VkPs5Image *)image_handle;
    if (image)
        image->native_usage = usage;
}

typedef struct VkPs5ImageView {
    VkImage image;
    VkImageViewType view_type;
    VkFormat format;
    VkComponentMapping components;
    uint32_t base_mip_level;
    uint32_t mip_level_count;
    uint32_t base_array_layer;
    uint32_t layer_count;
    AgcImageView native_view;
} VkPs5ImageView;

static VkResult ensure_native_image_view(VkPs5ImageView *view) {
    VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
    AgcFormat native_format;
    if (!view || !image || !image->native_image || view->native_view)
        return VK_SUCCESS;
    if (!native_image_format(view->format, &native_format))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    AgcImageViewDesc desc = AGC_IMAGE_VIEW_DESC_INIT;
    desc.image = image->native_image;
    desc.format = native_format;
    desc.base_mip_level = view->base_mip_level;
    desc.mip_level_count = view->mip_level_count;
    desc.base_array_layer = view->base_array_layer;
    desc.array_layer_count = view->layer_count;
    switch (view->view_type) {
    case VK_IMAGE_VIEW_TYPE_2D: desc.view_type = AGC_IMAGE_VIEW_TYPE_2D; break;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
        desc.view_type = AGC_IMAGE_VIEW_TYPE_2D_ARRAY; break;
    case VK_IMAGE_VIEW_TYPE_CUBE: desc.view_type = AGC_IMAGE_VIEW_TYPE_CUBE; break;
    case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
        desc.view_type = AGC_IMAGE_VIEW_TYPE_CUBE_ARRAY; break;
    default: return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    desc.swizzle_r = view->components.r;
    desc.swizzle_g = view->components.g;
    desc.swizzle_b = view->components.b;
    desc.swizzle_a = view->components.a;
    int32_t result = agcCreateImageView(vk_ps5_native_device(image->device),
        &desc, &view->native_view);
    return result == AGC_OK ? VK_SUCCESS :
        result == AGC_ERROR_OUT_OF_MEMORY ? VK_ERROR_OUT_OF_DEVICE_MEMORY :
        VK_ERROR_INITIALIZATION_FAILED;
}
typedef struct VkPs5BufferView { VkBuffer buffer; VkFormat format; } VkPs5BufferView;
typedef struct VkPs5Opaque { uint32_t kind; } VkPs5Opaque;
typedef struct VkPs5Sampler {
    uint32_t custom_border_color_value[4];
    AgcSampler native_sampler;
} VkPs5Sampler;

typedef struct VkPs5DescriptorUpdateTemplate {
    VkDescriptorUpdateTemplateType type;
    VkDescriptorSetLayout set_layout;
    uint32_t entry_count;
    VkDescriptorUpdateTemplateEntry entries[];
} VkPs5DescriptorUpdateTemplate;

#define VK_PS5_MAX_RENDER_ATTACHMENTS (AGC_GFX1013_MAX_COLOR_TARGETS + 1u)
#define VK_PS5_MAX_SUBPASSES 8u
#define VK_PS5_MAX_VERTEX_BINDINGS 32u
#define VK_PS5_MAX_VIEWPORTS AGC_GFX1013_MAX_VIEWPORTS
#define VK_PS5_VERTEX_TABLE_SIZE 0x4000u
#define VK_PS5_VERTEX_TABLE_SLICE \
    (VK_PS5_MAX_VERTEX_BINDINGS * sizeof(AgcGfx1013BufferDescriptor))
#define VK_PS5_INDIRECT_DESCRIPTOR_TABLE_SLICE 256u
#define VK_PS5_MAX_NATIVE_RESOURCE_STATES 128u

typedef struct VkPs5ShaderModule {
    size_t code_size;
    uint32_t code[];
} VkPs5ShaderModule;

typedef struct VkPs5PipelineCache {
    size_t data_size;
    uint8_t data[];
} VkPs5PipelineCache;

typedef struct VkPs5DescriptorSetLayout {
    uint32_t binding_count;
    VkDescriptorSetLayoutBinding bindings[];
} VkPs5DescriptorSetLayout;

typedef struct VkPs5PipelineLayout {
    uint32_t binding_count;
    uint32_t push_constant_size;
    OpenAgcPsbcDescriptorBinding bindings[];
} VkPs5PipelineLayout;

typedef struct VkPs5RenderPass {
    uint32_t attachment_count;
    uint32_t subpass_count;
    VkAttachmentDescription attachments[VK_PS5_MAX_RENDER_ATTACHMENTS];
    struct {
        uint32_t color_attachment_count;
        uint32_t color_attachments[AGC_GFX1013_MAX_COLOR_TARGETS];
        uint32_t depth_stencil_attachment;
        VkImageLayout depth_stencil_layout;
        VkSampleCountFlagBits samples;
    } subpasses[VK_PS5_MAX_SUBPASSES];
} VkPs5RenderPass;

typedef struct VkPs5Framebuffer {
    VkPs5RenderPass *render_pass;
    uint32_t attachment_count;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    VkPs5ImageView *attachments[VK_PS5_MAX_RENDER_ATTACHMENTS];
} VkPs5Framebuffer;

typedef struct VkPs5RuntimeShader {
    AgcShader native_shader;
} VkPs5RuntimeShader;

typedef struct VkPs5Pipeline {
    AgcComputePipeline native_compute_pipeline;
    AgcGraphicsPipeline native_graphics_pipeline;
    uint32_t stage_count;
    VkPipelineBindPoint bind_point;
    uint32_t primitive_type;
    OpenAgcPsbcStage stage_types[3];
    OpenAgcPsbcOutput stages[3];
    VkPs5RuntimeShader runtime[3];
    AgcGfx1013ViewportArrayState viewport_state;
    VkBool32 viewport_dynamic;
    VkBool32 scissor_dynamic;
    uint32_t vertex_binding_mask;
    uint32_t vertex_strides[VK_PS5_MAX_VERTEX_BINDINGS];
    VkBool32 robust_buffer_access;
    uint32_t vertex_attribute_mask;
    uint32_t vertex_attribute_bindings[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
    uint32_t vertex_attribute_offsets[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
    uint32_t vertex_attribute_sizes[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
    AgcGfx1013ColorBlendState color_blend;
    AgcGfx1013SampleState sample_state;
    AgcGfx1013PolygonMode polygon_mode;
    AgcGfx1013PrimitiveSizeState primitive_size;
    VkBool32 line_width_dynamic;
    VkBool32 blend_constants_dynamic;
    VkBool32 stencil_reference_dynamic;
    AgcGfx1013DepthBiasState depth_bias;
    VkBool32 depth_bias_enable;
    VkBool32 depth_bias_dynamic;
    VkBool32 depth_clamp_enable;
    AgcRasterizationStateFlags native_rasterization_flags;
    VkBool32 rasterizer_discard_enable;
    AgcCullModeFlags cull_mode;
    AgcFrontFace front_face;
    VkBool32 primitive_restart_enable;
    AgcGfx1013DepthStencilState depth_stencil;
    VkBool32 has_depth_stencil;
    VkBool32 dynamic_rendering;
    uint32_t dynamic_color_attachment_count;
    VkFormat dynamic_color_formats[AGC_GFX1013_MAX_COLOR_TARGETS];
    VkFormat dynamic_depth_format;
    VkFormat dynamic_stencil_format;
} VkPs5Pipeline;

VkBool32 vk_ps5_pipeline_has_native_shaders(VkPipeline pipeline_handle)
{
    const VkPs5Pipeline *pipeline = (const VkPs5Pipeline *)pipeline_handle;
    if (!pipeline || pipeline->stage_count == 0u)
        return VK_FALSE;
    for (uint32_t i = 0u; i < pipeline->stage_count; ++i) {
        if (!pipeline->runtime[i].native_shader)
            return VK_FALSE;
    }
    return VK_TRUE;
}

VkBool32 vk_ps5_pipeline_has_native_compute_pipeline(
    VkPipeline pipeline_handle)
{
    const VkPs5Pipeline *pipeline = (const VkPs5Pipeline *)pipeline_handle;
    return pipeline && pipeline->native_compute_pipeline ? VK_TRUE : VK_FALSE;
}

VkBool32 vk_ps5_pipeline_has_native_graphics_pipeline(
    VkPipeline pipeline_handle)
{
    const VkPs5Pipeline *pipeline = (const VkPs5Pipeline *)pipeline_handle;
    return pipeline && pipeline->native_graphics_pipeline ? VK_TRUE : VK_FALSE;
}

AgcRasterizationStateFlags vk_ps5_pipeline_native_rasterization_flags(
    VkPipeline pipeline_handle)
{
    const VkPs5Pipeline *pipeline = (const VkPs5Pipeline *)pipeline_handle;
    return pipeline ? pipeline->native_rasterization_flags : 0u;
}

typedef struct VkPs5QueryPool {
    VkQueryType type;
    uint32_t count;
    uint64_t record_size;
    AgcMemory native_memory;
    AgcBuffer native_buffer;
    void *data;
} VkPs5QueryPool;

typedef struct VkPs5DescriptorSet VkPs5DescriptorSet;
typedef struct VkPs5DescriptorValue {
    VkDescriptorType type;
    VkDescriptorBufferInfo buffer;
    VkDescriptorImageInfo image;
    VkBool32 valid;
} VkPs5DescriptorValue;

typedef struct VkPs5DescriptorPool {
    VkDevice device;
    uint32_t max_sets;
    uint32_t allocated_sets;
    VkPs5DescriptorSet *sets;
} VkPs5DescriptorPool;

struct VkPs5DescriptorSet {
    VkPs5DescriptorPool *pool;
    VkPs5DescriptorSet *next;
    VkPs5DescriptorSetLayout *layout;
    uint32_t descriptor_count;
    VkPs5DescriptorValue values[];
};

typedef struct VkPs5CommandPool {
    VkDevice device;
    uint32_t queue_family_index;
    struct VkPs5CommandBuffer *buffers;
} VkPs5CommandPool;

typedef enum VkPs5CommandState {
    VK_PS5_COMMAND_INITIAL,
    VK_PS5_COMMAND_RECORDING,
    VK_PS5_COMMAND_EXECUTABLE,
    VK_PS5_COMMAND_PENDING,
} VkPs5CommandState;

typedef struct VkPs5NativeBufferState {
    VkPs5Buffer *buffer;
    AgcResourceUsage usage;
} VkPs5NativeBufferState;

typedef struct VkPs5NativeImageState {
    VkPs5Image *image;
    AgcResourceUsage usage;
} VkPs5NativeImageState;

typedef struct VkPs5CommandBuffer {
    VK_LOADER_DATA loader_data;
    VkDevice device;
    VkCommandPool pool;
    VkCommandBufferLevel level;
    VkPs5CommandState state;
    VkResult record_error;
    const char *debug_last_command;
    AgcCommandBuffer native_graphics_command_buffer;
    AgcCommandBuffer native_compute_command_buffer;
    AgcComputePipeline native_bound_compute;
    AgcGraphicsPipeline native_bound_graphics;
    VkBool32 compute_defaults_emitted;
    VkPs5Pipeline *bound_compute;
    VkPs5Pipeline *bound_graphics;
    VkPs5DescriptorSet *compute_sets[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    VkPs5DescriptorSet *graphics_sets[OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    VkPs5RenderPass *active_render_pass;
    VkPs5Framebuffer *active_framebuffer;
    VkBool32 active_dynamic_rendering;
    VkPs5RenderPass dynamic_render_pass;
    VkPs5Framebuffer dynamic_framebuffer;
    uint32_t active_subpass;
    float dynamic_line_width;
    VkBool32 dynamic_line_width_set;
    AgcGfx1013DepthBiasState dynamic_depth_bias;
    VkBool32 dynamic_depth_bias_set;
    float dynamic_blend_constants[4];
    VkBool32 dynamic_blend_constants_set;
    uint32_t dynamic_stencil_reference_front;
    uint32_t dynamic_stencil_reference_back;
    VkBool32 dynamic_stencil_reference_set;
    AgcGfx1013Viewport dynamic_viewports[VK_PS5_MAX_VIEWPORTS];
    AgcGfx1013ScissorState dynamic_scissors[VK_PS5_MAX_VIEWPORTS];
    uint32_t dynamic_viewport_mask;
    uint32_t dynamic_scissor_mask;
    VkPs5QueryPool *active_query_pool;
    uint32_t active_query;
    VkPs5Buffer *index_buffer;
    VkDeviceSize index_offset;
    VkDeviceSize index_size;
    VkIndexType index_type;
    VkPs5Buffer *vertex_buffers[VK_PS5_MAX_VERTEX_BINDINGS];
    VkDeviceSize vertex_offsets[VK_PS5_MAX_VERTEX_BINDINGS];
    uint8_t push_constant_data[kAgcShaderStageCount][256u];
    uint64_t push_constant_masks[kAgcShaderStageCount];
    VkPs5NativeBufferState
        native_buffer_states[VK_PS5_MAX_NATIVE_RESOURCE_STATES];
    uint32_t native_buffer_state_count;
    VkPs5NativeImageState
        native_image_states[VK_PS5_MAX_NATIVE_RESOURCE_STATES];
    uint32_t native_image_state_count;
    uint32_t native_descriptor_bind_count;
    AgcGraphicsPipeline native_descriptor_graphics_pipeline;
    VkPs5DescriptorSet *native_descriptor_graphics_sets[
        OPENAGC_PSBC_MAX_DESCRIPTOR_SETS];
    AgcGraphicsPipeline native_vertex_graphics_pipeline;
    VkPs5Buffer *native_vertex_buffers[VK_PS5_MAX_VERTEX_BINDINGS];
    VkDeviceSize native_vertex_offsets[VK_PS5_MAX_VERTEX_BINDINGS];
    VkPs5RenderPass *native_attachments_render_pass;
    VkPs5Framebuffer *native_attachments_framebuffer;
    uint32_t native_attachments_subpass;
    uint32_t native_dispatch_count;
    uint32_t native_draw_count;
    VkBool32 native_stream_complete;
    VkBool32 requires_native_stream;
    struct VkPs5CommandBuffer *next;
} VkPs5CommandBuffer;

static void debug_note_command(VkPs5CommandBuffer *command, const char *name)
{
    if (command && command->state == VK_PS5_COMMAND_RECORDING &&
        command->record_error == VK_SUCCESS)
        command->debug_last_command = name;
}

static void native_commit_resource_states(VkPs5CommandBuffer *command);

static VkBool32 native_require_complete_stream(VkPs5CommandBuffer *command)
{
    if (!command->native_stream_complete) {
        if (command->record_error == VK_SUCCESS)
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return VK_FALSE;
    }
    command->requires_native_stream = VK_TRUE;
    return VK_TRUE;
}

static void native_mark_stream_incomplete(VkPs5CommandBuffer *command)
{
    command->native_stream_complete = VK_FALSE;
    if (command->requires_native_stream &&
        command->record_error == VK_SUCCESS)
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}

VkBool32 vk_ps5_command_buffer_has_native(
    VkCommandBuffer command_buffer)
{
    const VkPs5CommandBuffer *command =
        (const VkPs5CommandBuffer *)command_buffer;
    return command && command->native_graphics_command_buffer &&
        command->native_compute_command_buffer ? VK_TRUE : VK_FALSE;
}

uint32_t vk_ps5_command_buffer_native_state(
    VkCommandBuffer command_buffer)
{
    const VkPs5CommandBuffer *command =
        (const VkPs5CommandBuffer *)command_buffer;
    AgcCommandBufferState graphics_state = AGC_COMMAND_BUFFER_STATE_INITIAL;
    AgcCommandBufferState compute_state = AGC_COMMAND_BUFFER_STATE_INITIAL;
    if (!command || !command->native_graphics_command_buffer ||
        !command->native_compute_command_buffer ||
        agcGetCommandBufferState(command->native_graphics_command_buffer,
            &graphics_state) != AGC_OK ||
        agcGetCommandBufferState(command->native_compute_command_buffer,
            &compute_state) != AGC_OK || graphics_state != compute_state)
        return UINT32_MAX;
    return (uint32_t)graphics_state;
}

uint32_t vk_ps5_command_buffer_native_dispatch_count(
    VkCommandBuffer command_buffer)
{
    const VkPs5CommandBuffer *command =
        (const VkPs5CommandBuffer *)command_buffer;
    return command ? command->native_dispatch_count : 0u;
}

uint32_t vk_ps5_command_buffer_native_draw_count(
    VkCommandBuffer command_buffer)
{
    const VkPs5CommandBuffer *command =
        (const VkPs5CommandBuffer *)command_buffer;
    return command ? command->native_draw_count : 0u;
}

VkBool32 vk_ps5_command_buffer_native_stream_complete(
    VkCommandBuffer command_buffer)
{
    const VkPs5CommandBuffer *command =
        (const VkPs5CommandBuffer *)command_buffer;
    return command ? command->native_stream_complete : VK_FALSE;
}

VkResult vk_ps5_command_buffer_record_error(VkCommandBuffer command_buffer)
{
    const VkPs5CommandBuffer *command =
        (const VkPs5CommandBuffer *)command_buffer;
    return command ? command->record_error : VK_ERROR_INITIALIZATION_FAILED;
}

VkBool32 vk_ps5_command_buffer_push_constant_word(
    VkCommandBuffer command_buffer, uint32_t stage, uint32_t offset,
    uint32_t *value)
{
    const VkPs5CommandBuffer *command =
        (const VkPs5CommandBuffer *)command_buffer;
    const uint32_t word = offset / sizeof(uint32_t);
    if (!command || !value || stage >= kAgcShaderStageCount ||
        (offset & 3u) != 0u || offset > 256u - sizeof(uint32_t) ||
        (command->push_constant_masks[stage] &
         (UINT64_C(1) << word)) == 0u)
        return VK_FALSE;
    memcpy(value, command->push_constant_data[stage] + offset,
        sizeof(*value));
    return VK_TRUE;
}

static void *alloc_object(VkDevice device, const VkAllocationCallbacks *allocator,
                          size_t size, size_t alignment) {
    void *object = vk_ps5_device_alloc(device, allocator, size, alignment,
                                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (object) memset(object, 0, size);
    return object;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
                                 VkLayerProperties *pProperties) {
    (void)physicalDevice; (void)pProperties;
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    *pPropertyCount = 0;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
              VkFence fence_handle) {
    if (!queue || (submitCount && !pSubmits)) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < submitCount; ++i) {
        if (pSubmits[i].sType != VK_STRUCTURE_TYPE_SUBMIT_INFO)
            return VK_ERROR_INITIALIZATION_FAILED;
        if ((pSubmits[i].waitSemaphoreCount &&
             (!pSubmits[i].pWaitSemaphores ||
              !pSubmits[i].pWaitDstStageMask)) ||
            (pSubmits[i].signalSemaphoreCount &&
             !pSubmits[i].pSignalSemaphores) ||
            (pSubmits[i].commandBufferCount &&
             !pSubmits[i].pCommandBuffers))
            return VK_ERROR_INITIALIZATION_FAILED;
        const VkTimelineSemaphoreSubmitInfo *timeline = NULL;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)pSubmits[i].pNext;
             next; next = next->pNext) {
            if (next->sType ==
                VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO)
                timeline = (const VkTimelineSemaphoreSubmitInfo *)next;
        }
        if (timeline &&
            (timeline->waitSemaphoreValueCount !=
                 pSubmits[i].waitSemaphoreCount ||
             timeline->signalSemaphoreValueCount !=
                 pSubmits[i].signalSemaphoreCount ||
             (timeline->waitSemaphoreValueCount &&
              !timeline->pWaitSemaphoreValues) ||
             (timeline->signalSemaphoreValueCount &&
              !timeline->pSignalSemaphoreValues)))
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0; j < pSubmits[i].waitSemaphoreCount; ++j) {
            VkPs5Semaphore *semaphore = (VkPs5Semaphore *)pSubmits[i].pWaitSemaphores[j];
            const uint64_t value = timeline ?
                timeline->pWaitSemaphoreValues[j] : 0u;
            if (!semaphore ||
                (semaphore->type == VK_SEMAPHORE_TYPE_BINARY ?
                    !atomic_load(&semaphore->signaled) :
                    atomic_load(&semaphore->value) < value))
                return VK_NOT_READY;
        }
        for (uint32_t j = 0; j < pSubmits[i].signalSemaphoreCount; ++j) {
            VkPs5Semaphore *semaphore =
                (VkPs5Semaphore *)pSubmits[i].pSignalSemaphores[j];
            const uint64_t value = timeline ?
                timeline->pSignalSemaphoreValues[j] : 0u;
            if (!semaphore ||
                (semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE &&
                 (!timeline || value <= atomic_load(&semaphore->value))))
                return VK_ERROR_INITIALIZATION_FAILED;
        }
        for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; ++j) {
            const VkPs5CommandBuffer *command =
                (const VkPs5CommandBuffer *)pSubmits[i].pCommandBuffers[j];
            if (!command || command->state != VK_PS5_COMMAND_EXECUTABLE)
                return VK_ERROR_INITIALIZATION_FAILED;
        }
        for (uint32_t j = 0; j < pSubmits[i].waitSemaphoreCount; ++j) {
            VkPs5Semaphore *semaphore =
                (VkPs5Semaphore *)pSubmits[i].pWaitSemaphores[j];
            if (semaphore->type == VK_SEMAPHORE_TYPE_BINARY)
                atomic_store(&semaphore->signaled, false);
        }
        for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; ++j) {
            VkPs5CommandBuffer *command =
                (VkPs5CommandBuffer *)pSubmits[i].pCommandBuffers[j];
            command->state = VK_PS5_COMMAND_PENDING;
            VkResult result;
            if (!command->native_stream_complete) {
                command->state = VK_PS5_COMMAND_EXECUTABLE;
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            AgcCommandBuffer native_command =
                command->native_graphics_command_buffer;
            result = vk_ps5_queue_submit_native(
                queue, 1u, &native_command);
            if (result == VK_SUCCESS)
                native_commit_resource_states(command);
            command->state = VK_PS5_COMMAND_EXECUTABLE;
            if (result != VK_SUCCESS) return result;
        }
        for (uint32_t j = 0; j < pSubmits[i].signalSemaphoreCount; ++j) {
            VkPs5Semaphore *semaphore = (VkPs5Semaphore *)pSubmits[i].pSignalSemaphores[j];
            if (semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE)
                (void)timeline_advance(
                    semaphore, timeline->pSignalSemaphoreValues[j]);
            else
                atomic_store(&semaphore->signaled, true);
        }
    }
    if (fence_handle) atomic_store(&((VkPs5Fence *)fence_handle)->signaled, true);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                  const VkBindSparseInfo *pBindInfo, VkFence fence) {
    (void)queue; (void)bindInfoCount; (void)pBindInfo; (void)fence;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    if (!device || !pCreateInfo || !pFence ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_FENCE_CREATE_INFO ||
        (pCreateInfo->flags & ~VK_FENCE_CREATE_SIGNALED_BIT))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Fence *fence = alloc_object(device, pAllocator, sizeof(*fence), _Alignof(VkPs5Fence));
    if (!fence) return VK_ERROR_OUT_OF_HOST_MEMORY;
    atomic_init(&fence->signaled,
                (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0);
    *pFence = (VkFence)fence;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator) {
    if (fence) vk_ps5_device_free(device, pAllocator, (void *)fence);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    (void)device;
    if (fenceCount && !pFences) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < fenceCount; ++i) {
        if (!pFences[i]) return VK_ERROR_INITIALIZATION_FAILED;
        atomic_store(&((VkPs5Fence *)pFences[i])->signaled, false);
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetFenceStatus(VkDevice device, VkFence fence) {
    (void)device;
    if (!fence) return VK_ERROR_INITIALIZATION_FAILED;
    return atomic_load(&((VkPs5Fence *)fence)->signaled) ? VK_SUCCESS : VK_NOT_READY;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences,
                VkBool32 waitAll, uint64_t timeout) {
    (void)device; (void)timeout;
    if (!fenceCount || !pFences) return VK_ERROR_INITIALIZATION_FAILED;
    VkBool32 any = VK_FALSE;
    VkBool32 all = VK_TRUE;
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkBool32 signaled = pFences[i] &&
            atomic_load(&((VkPs5Fence *)pFences[i])->signaled);
        any |= signaled;
        all &= signaled;
    }
    return (waitAll ? all : any) ? VK_SUCCESS : VK_TIMEOUT;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
    if (!device || !pCreateInfo || !pSemaphore ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO ||
        pCreateInfo->flags)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkSemaphoreType type = VK_SEMAPHORE_TYPE_BINARY;
    uint64_t initial_value = 0u;
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
            const VkSemaphoreTypeCreateInfo *type_info =
                (const VkSemaphoreTypeCreateInfo *)next;
            if (type_info->semaphoreType != VK_SEMAPHORE_TYPE_BINARY &&
                type_info->semaphoreType != VK_SEMAPHORE_TYPE_TIMELINE)
                return VK_ERROR_INITIALIZATION_FAILED;
            type = type_info->semaphoreType;
            initial_value = type_info->initialValue;
        }
    }
    if (type == VK_SEMAPHORE_TYPE_BINARY && initial_value)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Semaphore *semaphore = alloc_object(device, pAllocator, sizeof(*semaphore),
                                              _Alignof(VkPs5Semaphore));
    if (!semaphore) return VK_ERROR_OUT_OF_HOST_MEMORY;
    semaphore->type = type;
    atomic_init(&semaphore->signaled, false);
    atomic_init(&semaphore->value, initial_value);
    *pSemaphore = (VkSemaphore)semaphore;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                   const VkAllocationCallbacks *pAllocator) {
    if (semaphore) vk_ps5_device_free(device, pAllocator, (void *)semaphore);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore_handle,
                           uint64_t *pValue)
{
    (void)device;
    VkPs5Semaphore *semaphore = (VkPs5Semaphore *)semaphore_handle;
    if (!semaphore || !pValue ||
        semaphore->type != VK_SEMAPHORE_TYPE_TIMELINE)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pValue = atomic_load(&semaphore->value);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkSignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo)
{
    (void)device;
    if (!pSignalInfo ||
        pSignalInfo->sType != VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO ||
        pSignalInfo->pNext)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Semaphore *semaphore =
        (VkPs5Semaphore *)pSignalInfo->semaphore;
    if (!semaphore || semaphore->type != VK_SEMAPHORE_TYPE_TIMELINE ||
        !timeline_advance(semaphore, pSignalInfo->value))
        return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo *pWaitInfo,
                 uint64_t timeout)
{
    (void)device;
    if (!pWaitInfo ||
        pWaitInfo->sType != VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO ||
        pWaitInfo->pNext ||
        (pWaitInfo->flags & ~VK_SEMAPHORE_WAIT_ANY_BIT) ||
        !pWaitInfo->semaphoreCount || !pWaitInfo->pSemaphores ||
        !pWaitInfo->pValues)
        return VK_ERROR_INITIALIZATION_FAILED;
    const uint64_t start = monotonic_nanoseconds();
    for (;;) {
        VkBool32 any = VK_FALSE;
        VkBool32 all = VK_TRUE;
        for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
            const VkPs5Semaphore *semaphore =
                (const VkPs5Semaphore *)pWaitInfo->pSemaphores[i];
            if (!semaphore || semaphore->type != VK_SEMAPHORE_TYPE_TIMELINE)
                return VK_ERROR_INITIALIZATION_FAILED;
            const VkBool32 reached =
                atomic_load(&semaphore->value) >= pWaitInfo->pValues[i];
            any |= reached;
            all &= reached;
        }
        if ((pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT) ? any : all)
            return VK_SUCCESS;
        if (!timeout) return VK_TIMEOUT;
        const uint64_t now = monotonic_nanoseconds();
        if (timeout != UINT64_MAX &&
            (now < start || now - start >= timeout))
            return VK_TIMEOUT;
        const struct timespec sleep_time = {0, 1000000};
        nanosleep(&sleep_time, NULL);
    }
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkEvent *pEvent) {
    if (!device || !pCreateInfo || !pEvent ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_EVENT_CREATE_INFO || pCreateInfo->flags)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Event *event = alloc_object(device, pAllocator, sizeof(*event), _Alignof(VkPs5Event));
    if (!event) return VK_ERROR_OUT_OF_HOST_MEMORY;
    atomic_init(&event->status, VK_EVENT_RESET);
    *pEvent = (VkEvent)event;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator) {
    if (event) vk_ps5_device_free(device, pAllocator, (void *)event);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetEventStatus(VkDevice device, VkEvent event) {
    (void)device;
    return event ? (VkResult)atomic_load(&((VkPs5Event *)event)->status) :
        VK_ERROR_INITIALIZATION_FAILED;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkSetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    atomic_store(&((VkPs5Event *)event)->status, VK_EVENT_SET);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    atomic_store(&((VkPs5Event *)event)->status, VK_EVENT_RESET);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
    if (!device || !pCreateInfo || !pBuffer ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO ||
        !pCreateInfo->size || pCreateInfo->sharingMode > VK_SHARING_MODE_CONCURRENT ||
        (pCreateInfo->flags & (VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                               VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT |
                               VK_BUFFER_CREATE_SPARSE_ALIASED_BIT |
                               VK_BUFFER_CREATE_PROTECTED_BIT)))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext)
        if (next->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO &&
            ((const VkExternalMemoryBufferCreateInfo *)next)->handleTypes != 0)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPs5Buffer *buffer = alloc_object(device, pAllocator, sizeof(*buffer),
                                       _Alignof(VkPs5Buffer));
    if (!buffer) return VK_ERROR_OUT_OF_HOST_MEMORY;
    buffer->size = pCreateInfo->size;
    buffer->usage = pCreateInfo->usage;
    *pBuffer = (VkBuffer)buffer;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator) {
    if (buffer) {
        VkPs5Buffer *native = (VkPs5Buffer *)buffer;
        if (native->native_buffer)
            vk_ps5_destroy_or_defer_native(device, VK_PS5_NATIVE_BUFFER,
                native->native_buffer);
        vk_ps5_device_free(device, pAllocator, (void *)buffer);
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer_handle,
                              VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!buffer_handle || !pMemoryRequirements) return;
    VkPs5Buffer *buffer = (VkPs5Buffer *)buffer_handle;
    pMemoryRequirements->alignment = 0x10000u;
    pMemoryRequirements->size = (buffer->size + 0xffffu) &
        ~(VkDeviceSize)0xffffu;
    pMemoryRequirements->memoryTypeBits = 0x3;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindBufferMemory(VkDevice device, VkBuffer buffer_handle, VkDeviceMemory memory,
                   VkDeviceSize memoryOffset) {
    VkPs5Buffer *buffer = (VkPs5Buffer *)buffer_handle;
    if (!buffer || !memory || buffer->memory ||
        memoryOffset % 0x10000u != 0 ||
        memoryOffset > vk_ps5_memory_size(memory) ||
        buffer->size > vk_ps5_memory_size(memory) - memoryOffset)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    AgcBufferDesc desc = AGC_BUFFER_DESC_INIT;
    desc.size = buffer->size;
    desc.usage = native_buffer_usage(buffer->usage);
    if (vk_ps5_memory_type_index(memory) == 0u)
        desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT |
            AGC_BUFFER_CREATE_READBACK_BIT;
    int32_t result = agcCreatePlacedBuffer(vk_ps5_native_device(device),
        &desc, vk_ps5_native_memory(memory), memoryOffset,
        &buffer->native_buffer);
    if (result != AGC_OK)
        return result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    buffer->memory = memory;
    buffer->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkDeviceAddress VKAPI_CALL
vkGetBufferDeviceAddress(VkDevice device,
                         const VkBufferDeviceAddressInfo *pInfo) {
    (void)device; (void)pInfo;
    return 0u;
}

VK_PS5_EXPORT VKAPI_ATTR uint64_t VKAPI_CALL
vkGetBufferOpaqueCaptureAddress(VkDevice device,
                                const VkBufferDeviceAddressInfo *pInfo) {
    (void)device; (void)pInfo;
    return 0u;
}

VK_PS5_EXPORT VKAPI_ATTR uint64_t VKAPI_CALL
vkGetDeviceMemoryOpaqueCaptureAddress(
    VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo) {
    (void)device; (void)pInfo;
    return 0u;
}

static uint32_t format_bytes(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_S8_UINT: return 1;
    case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_D16_UNORM: return 2;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT: return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    default: return 0;
    }
}

static bool color_target_format(
    VkFormat format, AgcGfx1013ColorTargetFormat *target_format) {
    if (!target_format) return false;
    switch (format) {
    case VK_FORMAT_R8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_R8_UNORM; break;
    case VK_FORMAT_R8G8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_RG8_UNORM; break;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA8_UNORM; break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_BGRA8_UNORM; break;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        *target_format = AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM; break;
    case VK_FORMAT_R16_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_R16_FLOAT; break;
    case VK_FORMAT_R16G16_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RG16_FLOAT; break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT; break;
    case VK_FORMAT_R32_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_R32_FLOAT; break;
    case VK_FORMAT_R32G32_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RG32_FLOAT; break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT; break;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        *target_format = AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT; break;
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA8_SRGB; break;
    case VK_FORMAT_B8G8R8A8_SRGB:
        *target_format = AGC_GFX1013_RT_FORMAT_BGRA8_SRGB; break;
    default:
        return false;
    }
    return true;
}

static bool depth_surface_format(
    VkFormat format, AgcGfx1013DepthSurfaceFormat *depth_format)
{
    if (!depth_format) return false;
    switch (format) {
    case VK_FORMAT_D16_UNORM:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM; break;
    case VK_FORMAT_D32_SFLOAT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT; break;
    case VK_FORMAT_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_S8_UINT; break;
    case VK_FORMAT_D16_UNORM_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT; break;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT; break;
    default:
        return false;
    }
    return true;
}

static AgcGfx1013StencilOp stencil_operation(VkStencilOp operation)
{
    switch (operation) {
    case VK_STENCIL_OP_KEEP: return AGC_GFX1013_STENCIL_KEEP;
    case VK_STENCIL_OP_ZERO: return AGC_GFX1013_STENCIL_ZERO;
    case VK_STENCIL_OP_REPLACE: return AGC_GFX1013_STENCIL_REPLACE;
    case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
        return AGC_GFX1013_STENCIL_INCREMENT_CLAMP;
    case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
        return AGC_GFX1013_STENCIL_DECREMENT_CLAMP;
    case VK_STENCIL_OP_INVERT: return AGC_GFX1013_STENCIL_INVERT;
    case VK_STENCIL_OP_INCREMENT_AND_WRAP:
        return AGC_GFX1013_STENCIL_INCREMENT_WRAP;
    case VK_STENCIL_OP_DECREMENT_AND_WRAP:
        return AGC_GFX1013_STENCIL_DECREMENT_WRAP;
    default: return (AgcGfx1013StencilOp)-1;
    }
}

static bool stencil_face_state(
    const VkStencilOpState *source, AgcGfx1013StencilFaceState *dest)
{
    if (!source || !dest || source->compareOp > VK_COMPARE_OP_ALWAYS)
        return false;
    dest->compare_operation = (AgcGfx1013CompareOp)source->compareOp;
    dest->fail_operation = stencil_operation(source->failOp);
    dest->depth_fail_operation = stencil_operation(source->depthFailOp);
    dest->pass_operation = stencil_operation(source->passOp);
    if ((int)dest->fail_operation < 0 ||
        (int)dest->depth_fail_operation < 0 ||
        (int)dest->pass_operation < 0)
        return false;
    dest->reference = source->reference;
    dest->compare_mask = source->compareMask;
    dest->write_mask = source->writeMask;
    return true;
}

static bool blend_factor(
    VkBlendFactor source, AgcGfx1013BlendFactor *destination)
{
    if (!destination) return false;
    switch (source) {
    case VK_BLEND_FACTOR_ZERO:
        *destination = AGC_GFX1013_BLEND_ZERO; break;
    case VK_BLEND_FACTOR_ONE:
        *destination = AGC_GFX1013_BLEND_ONE; break;
    case VK_BLEND_FACTOR_SRC_COLOR:
        *destination = AGC_GFX1013_BLEND_SRC_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC_COLOR; break;
    case VK_BLEND_FACTOR_DST_COLOR:
        *destination = AGC_GFX1013_BLEND_DST_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_DST_COLOR; break;
    case VK_BLEND_FACTOR_SRC_ALPHA:
        *destination = AGC_GFX1013_BLEND_SRC_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC_ALPHA; break;
    case VK_BLEND_FACTOR_DST_ALPHA:
        *destination = AGC_GFX1013_BLEND_DST_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_DST_ALPHA; break;
    case VK_BLEND_FACTOR_CONSTANT_COLOR:
        *destination = AGC_GFX1013_BLEND_CONSTANT_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_COLOR; break;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA:
        *destination = AGC_GFX1013_BLEND_CONSTANT_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_ALPHA; break;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
        *destination = AGC_GFX1013_BLEND_SRC_ALPHA_SATURATE; break;
    case VK_BLEND_FACTOR_SRC1_COLOR:
        *destination = AGC_GFX1013_BLEND_SRC1_COLOR; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC1_COLOR; break;
    case VK_BLEND_FACTOR_SRC1_ALPHA:
        *destination = AGC_GFX1013_BLEND_SRC1_ALPHA; break;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
        *destination = AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA; break;
    default:
        return false;
    }
    return true;
}

static bool blend_operation(VkBlendOp source, AgcGfx1013BlendOp *destination)
{
    if (!destination) return false;
    switch (source) {
    case VK_BLEND_OP_ADD:
        *destination = AGC_GFX1013_BLEND_OP_ADD; break;
    case VK_BLEND_OP_SUBTRACT:
        *destination = AGC_GFX1013_BLEND_OP_SUBTRACT; break;
    case VK_BLEND_OP_REVERSE_SUBTRACT:
        *destination = AGC_GFX1013_BLEND_OP_REVERSE_SUBTRACT; break;
    case VK_BLEND_OP_MIN:
        *destination = AGC_GFX1013_BLEND_OP_MIN; break;
    case VK_BLEND_OP_MAX:
        *destination = AGC_GFX1013_BLEND_OP_MAX; break;
    default:
        return false;
    }
    return true;
}

static bool logic_operation(VkLogicOp source, AgcGfx1013LogicOp *destination)
{
    if (!destination)
        return false;
    switch (source) {
    case VK_LOGIC_OP_CLEAR: *destination = AGC_GFX1013_LOGIC_CLEAR; break;
    case VK_LOGIC_OP_AND: *destination = AGC_GFX1013_LOGIC_AND; break;
    case VK_LOGIC_OP_AND_REVERSE:
        *destination = AGC_GFX1013_LOGIC_AND_REVERSE; break;
    case VK_LOGIC_OP_COPY: *destination = AGC_GFX1013_LOGIC_COPY; break;
    case VK_LOGIC_OP_AND_INVERTED:
        *destination = AGC_GFX1013_LOGIC_AND_INVERTED; break;
    case VK_LOGIC_OP_NO_OP: *destination = AGC_GFX1013_LOGIC_NO_OP; break;
    case VK_LOGIC_OP_XOR: *destination = AGC_GFX1013_LOGIC_XOR; break;
    case VK_LOGIC_OP_OR: *destination = AGC_GFX1013_LOGIC_OR; break;
    case VK_LOGIC_OP_NOR: *destination = AGC_GFX1013_LOGIC_NOR; break;
    case VK_LOGIC_OP_EQUIVALENT:
        *destination = AGC_GFX1013_LOGIC_EQUIVALENT; break;
    case VK_LOGIC_OP_INVERT: *destination = AGC_GFX1013_LOGIC_INVERT; break;
    case VK_LOGIC_OP_OR_REVERSE:
        *destination = AGC_GFX1013_LOGIC_OR_REVERSE; break;
    case VK_LOGIC_OP_COPY_INVERTED:
        *destination = AGC_GFX1013_LOGIC_COPY_INVERTED; break;
    case VK_LOGIC_OP_OR_INVERTED:
        *destination = AGC_GFX1013_LOGIC_OR_INVERTED; break;
    case VK_LOGIC_OP_NAND: *destination = AGC_GFX1013_LOGIC_NAND; break;
    case VK_LOGIC_OP_SET: *destination = AGC_GFX1013_LOGIC_SET; break;
    default: return false;
    }
    return true;
}

static bool polygon_mode(
    VkPolygonMode source, AgcGfx1013PolygonMode *destination)
{
    if (!destination)
        return false;
    switch (source) {
    case VK_POLYGON_MODE_FILL:
        *destination = AGC_GFX1013_POLYGON_MODE_FILL;
        break;
    case VK_POLYGON_MODE_LINE:
        *destination = AGC_GFX1013_POLYGON_MODE_LINE;
        break;
    case VK_POLYGON_MODE_POINT:
        *destination = AGC_GFX1013_POLYGON_MODE_POINT;
        break;
    default:
        return false;
    }
    return true;
}

static bool primitive_topology(
    VkPrimitiveTopology source, AgcGfx1013PrimitiveTopology *destination)
{
    if (!destination)
        return false;
    switch (source) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_POINT_LIST;
        break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_LINE_LIST;
        break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
        *destination = AGC_GFX1013_TOPOLOGY_LINE_STRIP;
        break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_TRIANGLE_LIST;
        break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
        *destination = AGC_GFX1013_TOPOLOGY_TRIANGLE_STRIP;
        break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
        *destination = AGC_GFX1013_TOPOLOGY_TRIANGLE_FAN;
        break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
        *destination = AGC_GFX1013_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        break;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
        *destination = AGC_GFX1013_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
        *destination = AGC_GFX1013_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
        *destination = AGC_GFX1013_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
        break;
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
        *destination = AGC_GFX1013_TOPOLOGY_PATCH_LIST;
        break;
    default:
        return false;
    }
    return true;
}

static bool translate_viewport(
    const VkViewport *source, AgcGfx1013Viewport *destination)
{
    if (!source || !destination ||
        !(source->width > 0.0f && source->width <= 16384.0f) ||
        !(source->height > 0.0f && source->height <= 16384.0f) ||
        !(source->x >= -32768.0f &&
          source->x + source->width <= 32767.0f) ||
        !(source->y >= -32768.0f &&
          source->y + source->height <= 32767.0f) ||
        !(source->minDepth >= 0.0f && source->minDepth <= 1.0f) ||
        !(source->maxDepth >= 0.0f && source->maxDepth <= 1.0f) ||
        source->minDepth > source->maxDepth)
        return false;
    *destination = (AgcGfx1013Viewport){
        .x = source->x,
        .y = source->y,
        .width = source->width,
        .height = source->height,
        .min_depth = source->minDepth,
        .max_depth = source->maxDepth,
    };
    return true;
}

static bool translate_scissor(
    const VkRect2D *source, AgcGfx1013ScissorState *destination)
{
    if (!source || !destination || source->offset.x < 0 ||
        source->offset.y < 0 || !source->extent.width ||
        !source->extent.height || (uint32_t)source->offset.x > 0x7fffu ||
        source->extent.width > 0x7fffu - (uint32_t)source->offset.x ||
        (uint32_t)source->offset.y > 0x7fffu ||
        source->extent.height > 0x7fffu - (uint32_t)source->offset.y)
        return false;
    destination->left = (uint32_t)source->offset.x;
    destination->top = (uint32_t)source->offset.y;
    destination->right = destination->left + source->extent.width;
    destination->bottom = destination->top + source->extent.height;
    return true;
}

static VkResult resolve_viewport_state(
    const VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline,
    AgcGfx1013ViewportArrayState *destination)
{
    if (!command || !pipeline || !destination ||
        !command->active_framebuffer || !pipeline->viewport_state.count)
        return VK_ERROR_INITIALIZATION_FAILED;
    *destination = pipeline->viewport_state;
    const uint32_t required_mask =
        (1u << pipeline->viewport_state.count) - 1u;
    if (pipeline->viewport_dynamic) {
        if ((command->dynamic_viewport_mask & required_mask) != required_mask)
            return VK_ERROR_INITIALIZATION_FAILED;
        memcpy(destination->viewports, command->dynamic_viewports,
            pipeline->viewport_state.count * sizeof(destination->viewports[0]));
    }
    if (pipeline->scissor_dynamic) {
        if ((command->dynamic_scissor_mask & required_mask) != required_mask)
            return VK_ERROR_INITIALIZATION_FAILED;
        memcpy(destination->scissors, command->dynamic_scissors,
            pipeline->viewport_state.count * sizeof(destination->scissors[0]));
    }
    for (uint32_t i = 0u; i < destination->count; ++i) {
        if (destination->scissors[i].right >
                command->active_framebuffer->width ||
            destination->scissors[i].bottom >
                command->active_framebuffer->height)
            return VK_ERROR_INITIALIZATION_FAILED;
    }
    return VK_SUCCESS;
}

static bool color_blend_state(
    const VkPipelineColorBlendStateCreateInfo *source,
    uint32_t target_count, AgcGfx1013ColorBlendState *destination,
    bool *dual_source_blend)
{
    const VkColorComponentFlags component_mask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (!source || !destination || !dual_source_blend || !target_count ||
        target_count > AGC_GFX1013_MAX_COLOR_TARGETS ||
        source->logicOpEnable > VK_TRUE ||
        source->attachmentCount != target_count || !source->pAttachments)
        return false;
    memset(destination, 0, sizeof(*destination));
    *dual_source_blend = false;
    destination->target_count = target_count;
    destination->logic_enable = source->logicOpEnable;
    if (source->logicOpEnable) {
        if (!logic_operation(source->logicOp,
                &destination->logic_operation))
            return false;
    } else {
        destination->logic_operation = AGC_GFX1013_LOGIC_COPY;
    }
    memcpy(destination->constants, source->blendConstants,
        sizeof(destination->constants));
    for (uint32_t i = 0u; i < target_count; ++i) {
        const VkPipelineColorBlendAttachmentState *attachment =
            &source->pAttachments[i];
        AgcGfx1013ColorBlendTargetState *target =
            &destination->targets[i];
        if (attachment->colorWriteMask & ~component_mask)
            return false;
        target->enable = source->logicOpEnable ? VK_FALSE :
            attachment->blendEnable;
        target->write_mask = attachment->colorWriteMask;
        if (!target->enable) {
            target->color_source = AGC_GFX1013_BLEND_ONE;
            target->color_destination = AGC_GFX1013_BLEND_ZERO;
            target->color_operation = AGC_GFX1013_BLEND_OP_ADD;
            target->alpha_source = AGC_GFX1013_BLEND_ONE;
            target->alpha_destination = AGC_GFX1013_BLEND_ZERO;
            target->alpha_operation = AGC_GFX1013_BLEND_OP_ADD;
            continue;
        }
        const VkBlendFactor factors[] = {
            attachment->srcColorBlendFactor,
            attachment->dstColorBlendFactor,
            attachment->srcAlphaBlendFactor,
            attachment->dstAlphaBlendFactor,
        };
        for (uint32_t j = 0; j < sizeof(factors) / sizeof(factors[0]); ++j) {
            if (factors[j] >= VK_BLEND_FACTOR_SRC1_COLOR &&
                factors[j] <= VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA) {
                if (i != 0u)
                    return false;
                *dual_source_blend = true;
            }
        }
        if (!blend_factor(attachment->srcColorBlendFactor,
                &target->color_source) ||
            !blend_factor(attachment->dstColorBlendFactor,
                &target->color_destination) ||
            !blend_operation(attachment->colorBlendOp,
                &target->color_operation) ||
            !blend_factor(attachment->srcAlphaBlendFactor,
                &target->alpha_source) ||
            !blend_factor(attachment->dstAlphaBlendFactor,
                &target->alpha_destination) ||
            !blend_operation(attachment->alphaBlendOp,
                &target->alpha_operation))
            return false;
        target->separate_alpha =
            target->alpha_source != target->color_source ||
            target->alpha_destination != target->color_destination ||
            target->alpha_operation != target->color_operation;
    }
    return true;
}

static bool layout_resource_usage(
    VkImageLayout layout, AgcGfx1013ResourceUsage *usage) {
    if (!usage) return false;
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        *usage = AGC_GFX1013_RESOURCE_USAGE_UNDEFINED; break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
    case VK_IMAGE_LAYOUT_GENERAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_HOST_READ; break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ; break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_COPY_SOURCE; break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        *usage = AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION; break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        *usage = AGC_GFX1013_RESOURCE_USAGE_PRESENT; break;
    default:
        return false;
    }
    return true;
}

static bool mutable_color_format_pair(VkFormat image_format,
                                      VkFormat view_format)
{
    const bool bgra =
        (image_format == VK_FORMAT_B8G8R8A8_SRGB ||
         image_format == VK_FORMAT_B8G8R8A8_UNORM) &&
        (view_format == VK_FORMAT_B8G8R8A8_SRGB ||
         view_format == VK_FORMAT_B8G8R8A8_UNORM);
    const bool rgba =
        (image_format == VK_FORMAT_R8G8B8A8_SRGB ||
         image_format == VK_FORMAT_R8G8B8A8_UNORM ||
         image_format == VK_FORMAT_A8B8G8R8_SRGB_PACK32 ||
         image_format == VK_FORMAT_A8B8G8R8_UNORM_PACK32) &&
        (view_format == VK_FORMAT_R8G8B8A8_SRGB ||
         view_format == VK_FORMAT_R8G8B8A8_UNORM ||
         view_format == VK_FORMAT_A8B8G8R8_SRGB_PACK32 ||
         view_format == VK_FORMAT_A8B8G8R8_UNORM_PACK32);
    return bgra || rgba;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    if (!device || !pCreateInfo || !pImage ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO ||
        !pCreateInfo->extent.width || !pCreateInfo->extent.height ||
        !pCreateInfo->extent.depth || !pCreateInfo->mipLevels ||
        pCreateInfo->mipLevels > 15u ||
        !pCreateInfo->arrayLayers ||
        (pCreateInfo->samples != VK_SAMPLE_COUNT_1_BIT &&
         pCreateInfo->samples != VK_SAMPLE_COUNT_4_BIT) ||
        !native_image_format(pCreateInfo->format, &(AgcFormat){0}) ||
        ((pCreateInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
         (pCreateInfo->format != VK_FORMAT_R8G8B8A8_UNORM ||
          pCreateInfo->tiling != VK_IMAGE_TILING_LINEAR)) ||
        (pCreateInfo->flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                               VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
                               VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
                               VK_IMAGE_CREATE_PROTECTED_BIT))) {
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    const VkImageFormatListCreateInfo *format_list = NULL;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO &&
            ((const VkExternalMemoryImageCreateInfo *)next)->handleTypes != 0)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (next->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)
            format_list = (const VkImageFormatListCreateInfo *)next;
    }
    if (format_list) {
        if (!format_list->viewFormatCount || !format_list->pViewFormats ||
            !(pCreateInfo->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        bool includes_image_format = false;
        for (uint32_t i = 0; i < format_list->viewFormatCount; ++i) {
            if (!mutable_color_format_pair(
                    pCreateInfo->format, format_list->pViewFormats[i])) {
                return VK_ERROR_FORMAT_NOT_SUPPORTED;
            }
            includes_image_format |=
                format_list->pViewFormats[i] == pCreateInfo->format;
        }
        if (!includes_image_format) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    if (pCreateInfo->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
        if (pCreateInfo->imageType != VK_IMAGE_TYPE_2D ||
            pCreateInfo->extent.width != pCreateInfo->extent.height ||
            pCreateInfo->extent.depth != 1u ||
            pCreateInfo->arrayLayers < 6u ||
            pCreateInfo->arrayLayers > 0x2000u ||
            pCreateInfo->samples != VK_SAMPLE_COUNT_1_BIT ||
            !(pCreateInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT) ||
            (!native_image_is_bc(pCreateInfo->format) &&
             pCreateInfo->format != VK_FORMAT_R8G8B8A8_UNORM &&
             pCreateInfo->format != VK_FORMAT_B8G8R8A8_UNORM))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    VkPs5Image *image = alloc_object(device, pAllocator, sizeof(*image),
                                     _Alignof(VkPs5Image));
    if (!image) return VK_ERROR_OUT_OF_HOST_MEMORY;
    image->device = device;
    image->flags = pCreateInfo->flags;
    image->type = pCreateInfo->imageType;
    image->format = pCreateInfo->format;
    image->extent = pCreateInfo->extent;
    image->mip_levels = pCreateInfo->mipLevels;
    image->array_layers = pCreateInfo->arrayLayers;
    image->samples = pCreateInfo->samples;
    image->tiling = pCreateInfo->tiling;
    image->usage = pCreateInfo->usage;
    image->alignment = 256u;
    AgcGfx1013DepthSurfaceFormat depth_format;
    bool is_depth_format = depth_surface_format(
        pCreateInfo->format, &depth_format);
    if (pCreateInfo->samples == VK_SAMPLE_COUNT_4_BIT) {
        AgcGfx1013ColorTargetFormat target_format;
        if (pCreateInfo->imageType != VK_IMAGE_TYPE_2D ||
            pCreateInfo->extent.depth != 1u ||
            pCreateInfo->arrayLayers != 1u ||
            pCreateInfo->tiling != VK_IMAGE_TILING_OPTIMAL ||
            pCreateInfo->format != VK_FORMAT_R8G8B8A8_UNORM ||
            !(pCreateInfo->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            (pCreateInfo->usage & ~(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)) ||
            is_depth_format ||
            !color_target_format(pCreateInfo->format, &target_format)) {
            vk_ps5_device_free(device, pAllocator, image);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
        image->is_msaa_color_surface = VK_TRUE;
        VkResult native_result = initialize_native_image_layout(device, image);
        if (native_result != VK_SUCCESS) {
            vk_ps5_device_free(device, pAllocator, image);
            return native_result;
        }
        *pImage = (VkImage)image;
        return VK_SUCCESS;
    }
    if (pCreateInfo->tiling == VK_IMAGE_TILING_OPTIMAL && is_depth_format) {
        if (pCreateInfo->imageType != VK_IMAGE_TYPE_2D ||
            pCreateInfo->extent.depth != 1u) {
            vk_ps5_device_free(device, pAllocator, image);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
        image->is_depth_surface = VK_TRUE;
        VkResult native_result = initialize_native_image_layout(device, image);
        if (native_result != VK_SUCCESS) {
            vk_ps5_device_free(device, pAllocator, image);
            return native_result;
        }
        *pImage = (VkImage)image;
        return VK_SUCCESS;
    }
    if (pCreateInfo->tiling != VK_IMAGE_TILING_LINEAR &&
        pCreateInfo->tiling != VK_IMAGE_TILING_OPTIMAL) {
        vk_ps5_device_free(device, pAllocator, image);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    VkResult native_result = initialize_native_image_layout(device, image);
    if (native_result != VK_SUCCESS) {
        vk_ps5_device_free(device, pAllocator, image);
        return native_result;
    }
    *pImage = (VkImage)image;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator) {
    if (image) {
        VkPs5Image *native = (VkPs5Image *)image;
        if (native->native_clear_buffer)
            vk_ps5_destroy_or_defer_native(device, VK_PS5_NATIVE_BUFFER,
                native->native_clear_buffer);
        if (native->native_image)
            vk_ps5_destroy_or_defer_native(device, VK_PS5_NATIVE_IMAGE,
                native->native_image);
        vk_ps5_device_free(device, pAllocator, (void *)image);
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageMemoryRequirements(VkDevice device, VkImage image_handle,
                             VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!image_handle || !pMemoryRequirements) return;
    VkPs5Image *image = (VkPs5Image *)image_handle;
    pMemoryRequirements->size = image->size;
    pMemoryRequirements->alignment = image->alignment;
    pMemoryRequirements->memoryTypeBits =
        (image->is_depth_surface || image->is_msaa_color_surface) ? 0x2 : 0x3;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSparseMemoryRequirements(VkDevice device, VkImage image,
                                   uint32_t *pSparseMemoryRequirementCount,
                                   VkSparseImageMemoryRequirements *pSparseMemoryRequirements) {
    (void)device; (void)image; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory(VkDevice device, VkImage image_handle, VkDeviceMemory memory,
                  VkDeviceSize memoryOffset) {
    VkPs5Image *image = (VkPs5Image *)image_handle;
    if (!image || !memory || image->memory ||
        memoryOffset % image->alignment != 0 ||
        memoryOffset > vk_ps5_memory_size(memory) ||
        image->size > vk_ps5_memory_size(memory) - memoryOffset)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    int32_t result = agcCreatePlacedImage(vk_ps5_native_device(device),
        &image->native_desc, vk_ps5_native_memory(memory), memoryOffset,
        &image->native_image);
    if (result != AGC_OK)
        return result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    if ((image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
        ((image->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
         format_bytes(image->format) != 0u &&
         !native_image_is_depth(image->format))) {
        AgcBufferDesc alias_desc = AGC_BUFFER_DESC_INIT;
        alias_desc.size = image->size;
        alias_desc.usage = AGC_BUFFER_USAGE_TRANSFER_SRC_BIT |
            AGC_BUFFER_USAGE_TRANSFER_DST_BIT |
            AGC_BUFFER_USAGE_STORAGE_BIT;
        if (vk_ps5_memory_type_index(memory) == 0u)
            alias_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
        result = agcCreatePlacedBuffer(vk_ps5_native_device(device),
            &alias_desc, vk_ps5_native_memory(memory), memoryOffset,
            &image->native_clear_buffer);
        if (result != AGC_OK) {
            (void)agcDestroyImage(image->native_image);
            image->native_image = NULL;
            return result == AGC_ERROR_OUT_OF_MEMORY ?
                VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    image->memory = memory;
    image->memory_offset = memoryOffset;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSubresourceLayout(VkDevice device, VkImage image_handle,
                            const VkImageSubresource *pSubresource,
                            VkSubresourceLayout *pLayout) {
    VkPs5Image *image = (VkPs5Image *)image_handle;
    if (!image || !pSubresource || !pLayout) return;
    memset(pLayout, 0, sizeof(*pLayout));
    if (pSubresource->mipLevel >= image->mip_levels ||
        pSubresource->arrayLayer >= image->array_layers)
        return;
    uint32_t plane = 0u;
    if (native_image_is_depth(image->format)) {
        if (pSubresource->aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
            if (image->format != VK_FORMAT_S8_UINT &&
                image->format != VK_FORMAT_D16_UNORM_S8_UINT &&
                image->format != VK_FORMAT_D32_SFLOAT_S8_UINT)
                return;
            plane = image->format == VK_FORMAT_S8_UINT ? 0u : 1u;
        } else if (pSubresource->aspectMask != VK_IMAGE_ASPECT_DEPTH_BIT) {
            return;
        }
    } else if (pSubresource->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) {
        return;
    }
    AgcImageSubresourceLayout native_layout =
        AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    if (agcGetImageSubresourceLayout(vk_ps5_native_device(device),
            &image->native_desc, pSubresource->mipLevel,
            pSubresource->arrayLayer, plane, &native_layout) != AGC_OK)
        return;
    pLayout->offset = native_layout.offset;
    pLayout->size = native_layout.size;
    pLayout->rowPitch = native_layout.row_pitch;
    pLayout->depthPitch = native_layout.slice_pitch;
    pLayout->arrayPitch = native_layout.size;
    if (image->array_layers > 1u) {
        AgcImageSubresourceLayout adjacent =
            AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        uint32_t adjacent_layer = pSubresource->arrayLayer + 1u <
            image->array_layers ? pSubresource->arrayLayer + 1u :
            pSubresource->arrayLayer - 1u;
        if (agcGetImageSubresourceLayout(vk_ps5_native_device(device),
                &image->native_desc, pSubresource->mipLevel,
                adjacent_layer, plane, &adjacent) == AGC_OK) {
            pLayout->arrayPitch = adjacent.offset > native_layout.offset ?
                adjacent.offset - native_layout.offset :
                native_layout.offset - adjacent.offset;
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSubresourceLayout2(VkDevice device, VkImage image,
    const VkImageSubresource2 *pSubresource, VkSubresourceLayout2 *pLayout)
{
    if (!pSubresource || !pLayout ||
        pSubresource->sType != VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2 ||
        pLayout->sType != VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2)
        return;
    vkGetImageSubresourceLayout(device, image,
        &pSubresource->imageSubresource, &pLayout->subresourceLayout);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceImageSubresourceLayout(VkDevice device,
    const VkDeviceImageSubresourceInfo *pInfo, VkSubresourceLayout2 *pLayout)
{
    if (!device || !pInfo || !pLayout ||
        pInfo->sType != VK_STRUCTURE_TYPE_DEVICE_IMAGE_SUBRESOURCE_INFO ||
        !pInfo->pCreateInfo || !pInfo->pSubresource ||
        pLayout->sType != VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2)
        return;
    memset(&pLayout->subresourceLayout, 0,
        sizeof(pLayout->subresourceLayout));
    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(device, pInfo->pCreateInfo, NULL, &image) != VK_SUCCESS)
        return;
    vkGetImageSubresourceLayout2(
        device, image, pInfo->pSubresource, pLayout);
    vkDestroyImage(device, image, NULL);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                            VkDeviceSize *pCommittedMemoryInBytes) {
    (void)device;
    if (pCommittedMemoryInBytes) *pCommittedMemoryInBytes = vk_ps5_memory_size(memory);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
    if (!device || !pCreateInfo || !pCommandPool ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO ||
        pCreateInfo->queueFamilyIndex != 0 ||
        (pCreateInfo->flags & VK_COMMAND_POOL_CREATE_PROTECTED_BIT))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5CommandPool *pool = alloc_object(device, pAllocator, sizeof(*pool),
                                          _Alignof(VkPs5CommandPool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->device = device;
    pool->queue_family_index = pCreateInfo->queueFamilyIndex;
    *pCommandPool = (VkCommandPool)pool;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                     const VkAllocationCallbacks *pAllocator) {
    VkPs5CommandPool *pool = (VkPs5CommandPool *)commandPool;
    if (!pool) return;
    while (pool->buffers) {
        VkPs5CommandBuffer *command = pool->buffers;
        pool->buffers = command->next;
        (void)agcResetCommandBuffer(
            command->native_compute_command_buffer);
        (void)agcResetCommandBuffer(
            command->native_graphics_command_buffer);
        (void)agcDestroyCommandBuffer(
            command->native_compute_command_buffer);
        (void)agcDestroyCommandBuffer(
            command->native_graphics_command_buffer);
        vk_ps5_device_free(device, NULL, command);
    }
    vk_ps5_collect_deferred_native(device);
    vk_ps5_device_free(device, pAllocator, pool);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                   VkCommandPoolResetFlags flags) {
    (void)device; (void)flags;
    VkPs5CommandPool *pool = (VkPs5CommandPool *)commandPool;
    if (!pool) return VK_ERROR_INITIALIZATION_FAILED;
    for (VkPs5CommandBuffer *command = pool->buffers; command; command = command->next) {
        if (agcResetCommandBuffer(
                command->native_graphics_command_buffer) != AGC_OK ||
            agcResetCommandBuffer(
                command->native_compute_command_buffer) != AGC_OK)
            return VK_ERROR_DEVICE_LOST;
        command->state = VK_PS5_COMMAND_INITIAL;
        command->record_error = VK_SUCCESS;
        command->compute_defaults_emitted = VK_FALSE;
        command->bound_compute = NULL;
        command->bound_graphics = NULL;
        command->native_bound_compute = NULL;
        command->native_bound_graphics = NULL;
        command->native_bound_graphics = NULL;
        command->active_render_pass = NULL;
        command->active_framebuffer = NULL;
        command->active_dynamic_rendering = VK_FALSE;
        command->active_query_pool = NULL;
        command->index_buffer = NULL;
        memset(command->vertex_buffers, 0, sizeof(command->vertex_buffers));
command->native_buffer_state_count = 0u;
command->native_image_state_count = 0u;
command->native_descriptor_bind_count = 0u;
command->native_descriptor_graphics_pipeline = NULL;
memset(command->native_descriptor_graphics_sets, 0,
       sizeof(command->native_descriptor_graphics_sets));
command->native_vertex_graphics_pipeline = NULL;
memset(command->native_vertex_buffers, 0,
       sizeof(command->native_vertex_buffers));
memset(command->native_vertex_offsets, 0,
       sizeof(command->native_vertex_offsets));
command->native_attachments_render_pass = NULL;
command->native_attachments_framebuffer = NULL;
command->native_attachments_subpass = 0u;
command->native_dispatch_count = 0u;
command->native_draw_count = 0u;
command->native_stream_complete = VK_TRUE;
command->requires_native_stream = VK_FALSE;
    }
    vk_ps5_collect_deferred_native(device);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkTrimCommandPool(VkDevice device, VkCommandPool commandPool,
                  VkCommandPoolTrimFlags flags) {
    (void)device; (void)commandPool; (void)flags;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                         VkCommandBuffer *pCommandBuffers) {
    if (!device || !pAllocateInfo || !pCommandBuffers ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO ||
        !pAllocateInfo->commandPool || !pAllocateInfo->commandBufferCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5CommandPool *pool = (VkPs5CommandPool *)pAllocateInfo->commandPool;
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i)
        pCommandBuffers[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i) {
        VkPs5CommandBuffer *command = alloc_object(device, NULL, sizeof(*command),
                                                    _Alignof(VkPs5CommandBuffer));
        if (!command) {
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i, pCommandBuffers);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        command->device = device;
        command->pool = pAllocateInfo->commandPool;
        command->level = pAllocateInfo->level;
        command->state = VK_PS5_COMMAND_INITIAL;
        AgcCommandBufferDesc native_desc = AGC_COMMAND_BUFFER_DESC_INIT;
        native_desc.queue_type = kAgcQueueGraphics;
        native_desc.capacity_dwords = VK_PS5_DCB_SIZE / sizeof(uint32_t);
        int32_t native_result = agcCreateCommandBuffer(
            vk_ps5_native_device(device), &native_desc,
            &command->native_graphics_command_buffer);
        if (native_result != AGC_OK) {
            vk_ps5_device_free(device, NULL, command);
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i,
                                 pCommandBuffers);
            return native_result == AGC_ERROR_OUT_OF_MEMORY ?
                VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
        }
        native_desc.queue_type = kAgcQueueCompute;
        native_result = agcCreateCommandBuffer(
            vk_ps5_native_device(device), &native_desc,
            &command->native_compute_command_buffer);
        if (native_result != AGC_OK) {
            (void)agcDestroyCommandBuffer(
                command->native_graphics_command_buffer);
            vk_ps5_device_free(device, NULL, command);
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i,
                                 pCommandBuffers);
            return native_result == AGC_ERROR_OUT_OF_MEMORY ?
                VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
        }
        VkResult result = vk_ps5_set_device_loader_data(device, command);
        if (result != VK_SUCCESS) {
            (void)agcDestroyCommandBuffer(
                command->native_compute_command_buffer);
            (void)agcDestroyCommandBuffer(
                command->native_graphics_command_buffer);
            vk_ps5_device_free(device, NULL, command);
            vkFreeCommandBuffers(device, pAllocateInfo->commandPool, i, pCommandBuffers);
            return result;
        }
        command->next = pool->buffers;
        pool->buffers = command;
        pCommandBuffers[i] = (VkCommandBuffer)command;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                     uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    VkPs5CommandPool *pool = (VkPs5CommandPool *)commandPool;
    if (!pCommandBuffers) return;
    for (uint32_t i = 0; i < commandBufferCount; ++i) {
        VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)pCommandBuffers[i];
        if (!command || !pool) continue;
        VkPs5CommandBuffer **link = &pool->buffers;
        while (*link && *link != command) link = &(*link)->next;
        if (*link) {
            *link = command->next;
            (void)agcResetCommandBuffer(
                command->native_compute_command_buffer);
            (void)agcResetCommandBuffer(
                command->native_graphics_command_buffer);
            (void)agcDestroyCommandBuffer(
                command->native_compute_command_buffer);
            (void)agcDestroyCommandBuffer(
                command->native_graphics_command_buffer);
            vk_ps5_device_free(device, NULL, command);
        }
    }
    vk_ps5_collect_deferred_native(device);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                     const VkCommandBufferBeginInfo *pBeginInfo) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || !pBeginInfo ||
        pBeginInfo->sType != VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO ||
        command->state == VK_PS5_COMMAND_RECORDING || command->state == VK_PS5_COMMAND_PENDING)
        return VK_ERROR_INITIALIZATION_FAILED;
    int32_t native_result = agcBeginCommandBuffer(
        command->native_graphics_command_buffer);
    if (native_result != AGC_OK)
        return native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    native_result = agcBeginCommandBuffer(
        command->native_compute_command_buffer);
    if (native_result != AGC_OK) {
        (void)agcResetCommandBuffer(
            command->native_graphics_command_buffer);
        return native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    }
    command->state = VK_PS5_COMMAND_RECORDING;
    command->record_error = VK_SUCCESS;
    command->debug_last_command = "vkBeginCommandBuffer";
    command->compute_defaults_emitted = VK_FALSE;
    command->bound_compute = NULL;
    command->bound_graphics = NULL;
    command->native_bound_compute = NULL;
    command->native_bound_graphics = NULL;
    command->active_render_pass = NULL;
    command->active_framebuffer = NULL;
    command->active_dynamic_rendering = VK_FALSE;
    command->active_query_pool = NULL;
    command->index_buffer = NULL;
    command->dynamic_line_width_set = VK_FALSE;
    command->dynamic_depth_bias_set = VK_FALSE;
    command->dynamic_blend_constants_set = VK_FALSE;
    command->dynamic_stencil_reference_set = VK_FALSE;
    command->dynamic_viewport_mask = 0u;
    command->dynamic_scissor_mask = 0u;
    memset(command->vertex_buffers, 0, sizeof(command->vertex_buffers));
    memset(command->push_constant_masks, 0,
           sizeof(command->push_constant_masks));
command->native_buffer_state_count = 0u;
command->native_image_state_count = 0u;
command->native_descriptor_bind_count = 0u;
command->native_descriptor_graphics_pipeline = NULL;
memset(command->native_descriptor_graphics_sets, 0,
       sizeof(command->native_descriptor_graphics_sets));
command->native_vertex_graphics_pipeline = NULL;
memset(command->native_vertex_buffers, 0,
       sizeof(command->native_vertex_buffers));
memset(command->native_vertex_offsets, 0,
       sizeof(command->native_vertex_offsets));
command->native_attachments_render_pass = NULL;
command->native_attachments_framebuffer = NULL;
command->native_attachments_subpass = 0u;
command->native_dispatch_count = 0u;
command->native_draw_count = 0u;
command->native_stream_complete = VK_TRUE;
command->requires_native_stream = VK_FALSE;
    memset(command->compute_sets, 0, sizeof(command->compute_sets));
    memset(command->graphics_sets, 0, sizeof(command->graphics_sets));
    vk_ps5_collect_deferred_native(command->device);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (command->record_error != VK_SUCCESS || command->active_render_pass ||
        command->active_query_pool ||
        (command->requires_native_stream &&
         !command->native_stream_complete)) {
        VkResult result = command->record_error != VK_SUCCESS ?
            command->record_error :
            (command->requires_native_stream &&
             !command->native_stream_complete ?
                VK_ERROR_FEATURE_NOT_PRESENT :
                VK_ERROR_INITIALIZATION_FAILED);
        fprintf(stderr,
            "vulkan-ps5: vkEndCommandBuffer rejected record_error=%d "
            "render_pass=%u query=%u requires_native=%u native_complete=%u "
            "draws=%u dispatches=%u\n",
            command->record_error, command->active_render_pass != NULL,
            command->active_query_pool != NULL,
            command->requires_native_stream,
            command->native_stream_complete,
            command->native_draw_count,
            command->native_dispatch_count);
        fprintf(stderr, "vulkan-ps5: last valid command entry: %s\n",
            command->debug_last_command ? command->debug_last_command :
                "unknown");
        command->state = VK_PS5_COMMAND_INITIAL;
        (void)agcResetCommandBuffer(
            command->native_compute_command_buffer);
        (void)agcResetCommandBuffer(
            command->native_graphics_command_buffer);
        return result;
    }
    int32_t native_result = agcEndCommandBuffer(
        command->native_graphics_command_buffer);
    if (native_result == AGC_OK)
        native_result = agcEndCommandBuffer(
            command->native_compute_command_buffer);
    if (native_result != AGC_OK) {
        command->state = VK_PS5_COMMAND_INITIAL;
        (void)agcResetCommandBuffer(
            command->native_compute_command_buffer);
        (void)agcResetCommandBuffer(
            command->native_graphics_command_buffer);
        return native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
    }
    command->state = VK_PS5_COMMAND_EXECUTABLE;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    (void)flags;
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || command->state == VK_PS5_COMMAND_PENDING)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (agcResetCommandBuffer(command->native_graphics_command_buffer) !=
            AGC_OK ||
        agcResetCommandBuffer(command->native_compute_command_buffer) !=
            AGC_OK)
        return VK_ERROR_DEVICE_LOST;
    command->state = VK_PS5_COMMAND_INITIAL;
    command->record_error = VK_SUCCESS;
    command->debug_last_command = NULL;
    command->compute_defaults_emitted = VK_FALSE;
    command->bound_compute = NULL;
    command->bound_graphics = NULL;
    command->native_bound_compute = NULL;
    command->native_bound_graphics = NULL;
    command->active_render_pass = NULL;
    command->active_framebuffer = NULL;
    command->active_query_pool = NULL;
    command->index_buffer = NULL;
    command->dynamic_line_width_set = VK_FALSE;
    command->dynamic_depth_bias_set = VK_FALSE;
    command->dynamic_blend_constants_set = VK_FALSE;
    command->dynamic_stencil_reference_set = VK_FALSE;
    command->dynamic_viewport_mask = 0u;
    command->dynamic_scissor_mask = 0u;
    memset(command->vertex_buffers, 0, sizeof(command->vertex_buffers));
    memset(command->push_constant_masks, 0,
           sizeof(command->push_constant_masks));
command->native_buffer_state_count = 0u;
command->native_image_state_count = 0u;
command->native_descriptor_bind_count = 0u;
command->native_descriptor_graphics_pipeline = NULL;
memset(command->native_descriptor_graphics_sets, 0,
       sizeof(command->native_descriptor_graphics_sets));
command->native_vertex_graphics_pipeline = NULL;
memset(command->native_vertex_buffers, 0,
       sizeof(command->native_vertex_buffers));
memset(command->native_vertex_offsets, 0,
       sizeof(command->native_vertex_offsets));
command->native_attachments_render_pass = NULL;
command->native_attachments_framebuffer = NULL;
command->native_attachments_subpass = 0u;
command->native_dispatch_count = 0u;
command->native_draw_count = 0u;
command->native_stream_complete = VK_TRUE;
command->requires_native_stream = VK_FALSE;
    memset(command->compute_sets, 0, sizeof(command->compute_sets));
    memset(command->graphics_sets, 0, sizeof(command->graphics_sets));
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
    if (!device || !pCreateInfo || !pView ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO ||
        !pCreateInfo->image ||
        !native_image_format(pCreateInfo->format, &(AgcFormat){0}))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkPs5Image *image = (VkPs5Image *)pCreateInfo->image;
    VkImageAspectFlags valid_aspects = image->is_depth_surface ?
        (image->format == VK_FORMAT_S8_UINT ? VK_IMAGE_ASPECT_STENCIL_BIT :
         image->format == VK_FORMAT_D16_UNORM_S8_UINT ||
         image->format == VK_FORMAT_D32_SFLOAT_S8_UINT ?
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT :
            VK_IMAGE_ASPECT_DEPTH_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
    if (pCreateInfo->subresourceRange.baseMipLevel >= image->mip_levels ||
        pCreateInfo->subresourceRange.baseArrayLayer >= image->array_layers)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    uint32_t level_count = pCreateInfo->subresourceRange.levelCount ==
        VK_REMAINING_MIP_LEVELS ?
        image->mip_levels - pCreateInfo->subresourceRange.baseMipLevel :
        pCreateInfo->subresourceRange.levelCount;
    uint32_t layer_count = pCreateInfo->subresourceRange.layerCount ==
        VK_REMAINING_ARRAY_LAYERS ?
        image->array_layers - pCreateInfo->subresourceRange.baseArrayLayer :
        pCreateInfo->subresourceRange.layerCount;
    if ((pCreateInfo->format != image->format &&
         (!(image->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) ||
          !mutable_color_format_pair(image->format, pCreateInfo->format))) ||
        !pCreateInfo->subresourceRange.aspectMask ||
        (pCreateInfo->subresourceRange.aspectMask & ~valid_aspects) ||
        !level_count || !layer_count ||
        level_count > image->mip_levels -
            pCreateInfo->subresourceRange.baseMipLevel ||
        layer_count > image->array_layers -
            pCreateInfo->subresourceRange.baseArrayLayer)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    switch (pCreateInfo->viewType) {
    case VK_IMAGE_VIEW_TYPE_2D:
        if (layer_count != 1u) return VK_ERROR_FEATURE_NOT_PRESENT;
        break;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
        if (image->type != VK_IMAGE_TYPE_2D || image->is_depth_surface)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        break;
    case VK_IMAGE_VIEW_TYPE_CUBE:
        if (!(image->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) ||
            image->is_depth_surface ||
            image->extent.width != image->extent.height || layer_count != 6u)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        break;
    case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
        if (!(image->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) ||
            image->is_depth_surface ||
            image->extent.width != image->extent.height ||
            layer_count < 6u || layer_count % 6u != 0u)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        break;
    default:
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    VkPs5ImageView *view = alloc_object(device, pAllocator, sizeof(*view),
                                        _Alignof(VkPs5ImageView));
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->image = pCreateInfo->image;
    view->view_type = pCreateInfo->viewType;
    view->format = pCreateInfo->format;
    view->components = pCreateInfo->components;
    view->base_mip_level = pCreateInfo->subresourceRange.baseMipLevel;
    view->mip_level_count = level_count;
    view->base_array_layer = pCreateInfo->subresourceRange.baseArrayLayer;
    view->layer_count = layer_count;
    VkResult native_result = ensure_native_image_view(view);
    if (native_result != VK_SUCCESS) {
        vk_ps5_device_free(device, pAllocator, view);
        return native_result;
    }
    *pView = (VkImageView)view;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyImageView(VkDevice device, VkImageView imageView,
                   const VkAllocationCallbacks *pAllocator) {
    if (imageView) {
        VkPs5ImageView *view = (VkPs5ImageView *)imageView;
        if (view->native_view)
            vk_ps5_destroy_or_defer_native(device,
                VK_PS5_NATIVE_IMAGE_VIEW, view->native_view);
        vk_ps5_device_free(device, pAllocator, (void *)imageView);
    }
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkBufferView *pView) {
    if (!device || !pCreateInfo || !pView ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO ||
        !pCreateInfo->buffer || !format_bytes(pCreateInfo->format))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkPs5Buffer *buffer = (VkPs5Buffer *)pCreateInfo->buffer;
    if (pCreateInfo->offset > buffer->size) return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceSize range = pCreateInfo->range == VK_WHOLE_SIZE ?
        buffer->size - pCreateInfo->offset : pCreateInfo->range;
    if (range > buffer->size - pCreateInfo->offset)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5BufferView *view = alloc_object(device, pAllocator, sizeof(*view),
                                         _Alignof(VkPs5BufferView));
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->buffer = pCreateInfo->buffer;
    view->format = pCreateInfo->format;
    *pView = (VkBufferView)view;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                    const VkAllocationCallbacks *pAllocator) {
    if (bufferView) vk_ps5_device_free(device, pAllocator, (void *)bufferView);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                    const VkBindBufferMemoryInfo *pBindInfos) {
    if (bindInfoCount && !pBindInfos) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        if (pBindInfos[i].sType != VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)pBindInfos[i].pNext;
             next; next = next->pNext) {
            if (next->sType == VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO) {
                const VkBindBufferMemoryDeviceGroupInfo *group =
                    (const VkBindBufferMemoryDeviceGroupInfo *)next;
                if (group->deviceIndexCount > 1 ||
                    (group->deviceIndexCount == 1 &&
                     (!group->pDeviceIndices || group->pDeviceIndices[0] != 0)))
                    return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
        VkResult result = vkBindBufferMemory(device, pBindInfos[i].buffer,
                                             pBindInfos[i].memory,
                                             pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                   const VkBindImageMemoryInfo *pBindInfos) {
    if (bindInfoCount && !pBindInfos) return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        if (pBindInfos[i].sType != VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)pBindInfos[i].pNext;
             next; next = next->pNext)
            if (next->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            else if (next->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO) {
                const VkBindImageMemoryDeviceGroupInfo *group =
                    (const VkBindImageMemoryDeviceGroupInfo *)next;
                if (group->splitInstanceBindRegionCount != 0 ||
                    group->deviceIndexCount > 1 ||
                    (group->deviceIndexCount == 1 &&
                     (!group->pDeviceIndices || group->pDeviceIndices[0] != 0)))
                    return VK_ERROR_INITIALIZATION_FAILED;
            }
        VkResult result = vkBindImageMemory(device, pBindInfos[i].image,
                                            pBindInfos[i].memory,
                                            pBindInfos[i].memoryOffset);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

static void fill_dedicated_requirements(void *pNext) {
    for (VkBaseOutStructure *next = (VkBaseOutStructure *)pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
            VkMemoryDedicatedRequirements *dedicated =
                (VkMemoryDedicatedRequirements *)next;
            dedicated->prefersDedicatedAllocation = VK_FALSE;
            dedicated->requiresDedicatedAllocation = VK_FALSE;
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements2(VkDevice device,
                               const VkBufferMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pInfo || !pMemoryRequirements) return;
    vkGetBufferMemoryRequirements(device, pInfo->buffer,
                                  &pMemoryRequirements->memoryRequirements);
    fill_dedicated_requirements(pMemoryRequirements->pNext);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageMemoryRequirements2(VkDevice device,
                              const VkImageMemoryRequirementsInfo2 *pInfo,
                              VkMemoryRequirements2 *pMemoryRequirements) {
    if (!pInfo || !pMemoryRequirements) return;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO) {
            memset(&pMemoryRequirements->memoryRequirements, 0,
                   sizeof(pMemoryRequirements->memoryRequirements));
            return;
        }
    }
    vkGetImageMemoryRequirements(device, pInfo->image,
                                 &pMemoryRequirements->memoryRequirements);
    fill_dedicated_requirements(pMemoryRequirements->pNext);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSparseMemoryRequirements2(
    VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements) {
    (void)device; (void)pInfo; (void)pSparseMemoryRequirements;
    if (pSparseMemoryRequirementCount) *pSparseMemoryRequirementCount = 0;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool) {
    if (!device || !pCreateInfo || !pQueryPool ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO ||
        !pCreateInfo->queryCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pCreateInfo->queryType != VK_QUERY_TYPE_OCCLUSION)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    AgcOcclusionQueryLayout layout = AGC_OCCLUSION_QUERY_LAYOUT_INIT;
    if (agcGetOcclusionQueryLayout(vk_ps5_native_device(device),
            &layout) != AGC_OK || !layout.record_size ||
        layout.record_size > SIZE_MAX ||
        (size_t)pCreateInfo->queryCount > SIZE_MAX / layout.record_size)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5QueryPool *pool = alloc_object(device, pAllocator, sizeof(*pool),
                                        _Alignof(VkPs5QueryPool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = pCreateInfo->queryType;
    pool->count = pCreateInfo->queryCount;
    pool->record_size = layout.record_size;
    size_t size = (size_t)pool->count * (size_t)pool->record_size;
    AgcMemoryDesc memory_desc = AGC_MEMORY_DESC_INIT;
    memory_desc.size = size;
    memory_desc.heap = AGC_MEMORY_HEAP_FLEXIBLE;
    memory_desc.alignment = 256u;
    if (agcCreateMemory(vk_ps5_native_device(device), &memory_desc,
            &pool->native_memory) != AGC_OK ||
        agcMapMemory(pool->native_memory, 0u, size, &pool->data) != AGC_OK) {
        if (pool->native_memory)
            (void)agcDestroyMemory(pool->native_memory);
        vk_ps5_device_free(device, pAllocator, pool);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    buffer_desc.size = size;
    buffer_desc.usage = AGC_BUFFER_USAGE_QUERY_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT |
        AGC_BUFFER_CREATE_READBACK_BIT;
    if (agcCreatePlacedBuffer(vk_ps5_native_device(device), &buffer_desc,
            pool->native_memory, 0u, &pool->native_buffer) != AGC_OK ||
        agcResetOcclusionQueryResults(pool->native_buffer, 0u,
            pool->count) != AGC_OK) {
        if (pool->native_buffer)
            (void)agcDestroyBuffer(pool->native_buffer);
        (void)agcUnmapMemory(pool->native_memory);
        (void)agcDestroyMemory(pool->native_memory);
        vk_ps5_device_free(device, pAllocator, pool);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    *pQueryPool = (VkQueryPool)pool;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                   const VkAllocationCallbacks *pAllocator) {
    if (queryPool) {
        VkPs5QueryPool *pool = (VkPs5QueryPool *)queryPool;
        (void)agcDestroyBuffer(pool->native_buffer);
        (void)agcUnmapMemory(pool->native_memory);
        (void)agcDestroyMemory(pool->native_memory);
        vk_ps5_device_free(device, pAllocator, pool);
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkResetQueryPoolEXT(VkDevice device, VkQueryPool queryPool_handle,
                    uint32_t firstQuery, uint32_t queryCount) {
    (void)device;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)queryPool_handle;
    if (!pool || firstQuery > pool->count ||
        queryCount > pool->count - firstQuery)
        return;
    if (!queryCount) return;
    (void)agcResetOcclusionQueryResults(pool->native_buffer,
        (uint64_t)firstQuery * pool->record_size, queryCount);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkResetQueryPool(VkDevice device, VkQueryPool queryPool,
                 uint32_t firstQuery, uint32_t queryCount) {
    vkResetQueryPoolEXT(device, queryPool, firstQuery, queryCount);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool_handle, uint32_t firstQuery,
                      uint32_t queryCount, size_t dataSize, void *pData,
                      VkDeviceSize stride, VkQueryResultFlags flags) {
    (void)device;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)queryPool_handle;
    if (!pool || !pData || firstQuery > pool->count || queryCount > pool->count - firstQuery)
        return VK_ERROR_INITIALIZATION_FAILED;
    size_t value_size = (flags & VK_QUERY_RESULT_64_BIT) ? sizeof(uint64_t) : sizeof(uint32_t);
    size_t entry_size = value_size +
        ((flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? value_size : 0);
    if (stride < entry_size || (queryCount && dataSize <
        (size_t)(queryCount - 1) * (size_t)stride + entry_size))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < queryCount; ++i) {
        uint64_t slot = (uint64_t)(firstQuery + i) * pool->record_size;
        AgcOcclusionQueryResult native = AGC_OCCLUSION_QUERY_RESULT_INIT;
        int32_t native_result = agcGetOcclusionQueryResult(
            pool->native_buffer, slot,
            (flags & VK_QUERY_RESULT_WAIT_BIT) ? UINT64_C(5000000000) : 0u,
            &native);
        if (native_result != AGC_OK && native_result != AGC_ERROR_BUSY &&
            native_result != AGC_ERROR_TIMEOUT)
            return VK_ERROR_DEVICE_LOST;
        VkBool32 available = native.available ? VK_TRUE : VK_FALSE;
        uint64_t value = native.value;
        if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
            result = VK_NOT_READY;
        uint8_t *dst = (uint8_t *)pData + (size_t)i * (size_t)stride;
        if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
            if (flags & VK_QUERY_RESULT_64_BIT)
                memcpy(dst, &value, sizeof(value));
            else {
                uint32_t value32 = (uint32_t)value;
                memcpy(dst, &value32, sizeof(value32));
            }
        }
        if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
            if (flags & VK_QUERY_RESULT_64_BIT) {
                uint64_t availability = available;
                memcpy(dst + value_size, &availability, sizeof(availability));
            } else {
                uint32_t availability = available;
                memcpy(dst + value_size, &availability, sizeof(availability));
            }
        }
        if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
            result = VK_NOT_READY;
    }
    return result;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
    if (!device || !pCreateInfo || !pShaderModule ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO ||
        pCreateInfo->codeSize < sizeof(uint32_t) || pCreateInfo->codeSize % 4 ||
        !pCreateInfo->pCode || pCreateInfo->pCode[0] != 0x07230203u)
        return VK_ERROR_INITIALIZATION_FAILED;
    size_t size = sizeof(VkPs5ShaderModule) + pCreateInfo->codeSize;
    VkPs5ShaderModule *module = alloc_object(device, pAllocator, size,
                                              _Alignof(VkPs5ShaderModule));
    if (!module) return VK_ERROR_OUT_OF_HOST_MEMORY;
    module->code_size = pCreateInfo->codeSize;
    memcpy(module->code, pCreateInfo->pCode, pCreateInfo->codeSize);
    *pShaderModule = (VkShaderModule)module;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                      const VkAllocationCallbacks *pAllocator) {
    if (shaderModule) vk_ps5_device_free(device, pAllocator, (void *)shaderModule);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkPipelineCache *pPipelineCache) {
    if (!device || !pCreateInfo || !pPipelineCache ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO ||
        (pCreateInfo->initialDataSize && !pCreateInfo->pInitialData))
        return VK_ERROR_INITIALIZATION_FAILED;
    static const uint8_t cache_uuid[] = "VulkanPS5-gfx1013";
    size_t initial_size = 0;
    if (pCreateInfo->initialDataSize >= sizeof(VkPipelineCacheHeaderVersionOne)) {
        const VkPipelineCacheHeaderVersionOne *header = pCreateInfo->pInitialData;
        if (header->headerSize == sizeof(*header) &&
            header->headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
            header->vendorID == 0x1002u && header->deviceID == 0x163fu &&
            memcmp(header->pipelineCacheUUID, cache_uuid, VK_UUID_SIZE) == 0)
            initial_size = pCreateInfo->initialDataSize;
    }
    size_t data_size = initial_size ? initial_size : sizeof(VkPipelineCacheHeaderVersionOne);
    size_t size = sizeof(VkPs5PipelineCache) + data_size;
    VkPs5PipelineCache *cache = alloc_object(device, pAllocator, size,
                                              _Alignof(VkPs5PipelineCache));
    if (!cache) return VK_ERROR_OUT_OF_HOST_MEMORY;
    cache->data_size = data_size;
    if (initial_size) {
        memcpy(cache->data, pCreateInfo->pInitialData, initial_size);
    } else {
        VkPipelineCacheHeaderVersionOne header = {
            .headerSize = sizeof(header),
            .headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE,
            .vendorID = 0x1002u,
            .deviceID = 0x163fu,
        };
        memcpy(header.pipelineCacheUUID, cache_uuid, VK_UUID_SIZE);
        memcpy(cache->data, &header, sizeof(header));
    }
    *pPipelineCache = (VkPipelineCache)cache;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                       const VkAllocationCallbacks *pAllocator) {
    if (pipelineCache) vk_ps5_device_free(device, pAllocator, (void *)pipelineCache);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache_handle,
                       size_t *pDataSize, void *pData) {
    (void)device;
    VkPs5PipelineCache *cache = (VkPs5PipelineCache *)pipelineCache_handle;
    if (!cache || !pDataSize) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pData) {
        *pDataSize = cache->data_size;
        return VK_SUCCESS;
    }
    size_t written = *pDataSize < cache->data_size ? *pDataSize : cache->data_size;
    if (written) memcpy(pData, cache->data, written);
    *pDataSize = written;
    return written < cache->data_size ? VK_INCOMPLETE : VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                      uint32_t srcCacheCount, const VkPipelineCache *pSrcCaches) {
    (void)device; (void)dstCache;
    return srcCacheCount && !pSrcCaches ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS;
}

static bool psbc_descriptor_type(VkDescriptorType source,
                                 OpenAgcPsbcDescriptorType *dest) {
    switch (source) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_SAMPLER; return true;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER; return true;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        *dest = OPENAGC_PSBC_DESCRIPTOR_SAMPLED_IMAGE; return true;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        *dest = OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE; return true;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_UNIFORM_TEXEL_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        *dest = OPENAGC_PSBC_DESCRIPTOR_STORAGE_TEXEL_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        *dest = OPENAGC_PSBC_DESCRIPTOR_UNIFORM_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        *dest = OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER; return true;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        *dest = OPENAGC_PSBC_DESCRIPTOR_INPUT_ATTACHMENT; return true;
    default:
        return false;
    }
}

static bool psbc_vertex_format(VkFormat source, OpenAgcPsbcVertexFormat *dest) {
    switch (source) {
    case VK_FORMAT_R32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32_SFLOAT; return true;
    case VK_FORMAT_R32G32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32G32_SFLOAT; return true;
    case VK_FORMAT_R32G32B32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32_SFLOAT; return true;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32A32_SFLOAT; return true;
    case VK_FORMAT_R8G8B8A8_UNORM:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R8G8B8A8_UNORM; return true;
    case VK_FORMAT_R16G16_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R16G16_SFLOAT; return true;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        *dest = OPENAGC_PSBC_VERTEX_FORMAT_R16G16B16A16_SFLOAT; return true;
    default:
        return false;
    }
}

static uint32_t psbc_vertex_format_size(OpenAgcPsbcVertexFormat format) {
    switch (format) {
    case OPENAGC_PSBC_VERTEX_FORMAT_R32_SFLOAT:
    case OPENAGC_PSBC_VERTEX_FORMAT_R8G8B8A8_UNORM:
    case OPENAGC_PSBC_VERTEX_FORMAT_R16G16_SFLOAT:
        return 4u;
    case OPENAGC_PSBC_VERTEX_FORMAT_R32G32_SFLOAT:
    case OPENAGC_PSBC_VERTEX_FORMAT_R16G16B16A16_SFLOAT:
        return 8u;
    case OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32_SFLOAT:
        return 12u;
    case OPENAGC_PSBC_VERTEX_FORMAT_R32G32B32A32_SFLOAT:
        return 16u;
    default:
        return 0u;
    }
}

static VkResult psbc_result(OpenAgcPsbcResult result) {
    switch (result) {
    case OPENAGC_PSBC_SUCCESS: return VK_SUCCESS;
    case OPENAGC_PSBC_ERROR_OUT_OF_MEMORY: return VK_ERROR_OUT_OF_HOST_MEMORY;
    case OPENAGC_PSBC_ERROR_UNSUPPORTED_STAGE:
    case OPENAGC_PSBC_ERROR_UNSUPPORTED_PIPELINE:
        return VK_ERROR_FEATURE_NOT_PRESENT;
    case OPENAGC_PSBC_ERROR_INVALID_SPIRV:
    case OPENAGC_PSBC_ERROR_NIR:
    case OPENAGC_PSBC_ERROR_ACO:
        return VK_ERROR_INVALID_SHADER_NV;
    default:
        return VK_ERROR_INITIALIZATION_FAILED;
    }
}

static bool native_blend_factor(VkBlendFactor source, AgcBlendFactor *dest)
{
    switch (source) {
    case VK_BLEND_FACTOR_ZERO: *dest = AGC_BLEND_FACTOR_ZERO; return true;
    case VK_BLEND_FACTOR_ONE: *dest = AGC_BLEND_FACTOR_ONE; return true;
    case VK_BLEND_FACTOR_SRC_COLOR:
        *dest = AGC_BLEND_FACTOR_SRC_COLOR; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; return true;
    case VK_BLEND_FACTOR_DST_COLOR:
        *dest = AGC_BLEND_FACTOR_DST_COLOR; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_DST_COLOR; return true;
    case VK_BLEND_FACTOR_SRC_ALPHA:
        *dest = AGC_BLEND_FACTOR_SRC_ALPHA; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; return true;
    case VK_BLEND_FACTOR_DST_ALPHA:
        *dest = AGC_BLEND_FACTOR_DST_ALPHA; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; return true;
    case VK_BLEND_FACTOR_CONSTANT_COLOR:
        *dest = AGC_BLEND_FACTOR_CONSTANT_COLOR; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR; return true;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA:
        *dest = AGC_BLEND_FACTOR_CONSTANT_ALPHA; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA; return true;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
        *dest = AGC_BLEND_FACTOR_SRC_ALPHA_SATURATE; return true;
    case VK_BLEND_FACTOR_SRC1_COLOR:
        *dest = AGC_BLEND_FACTOR_SRC1_COLOR; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; return true;
    case VK_BLEND_FACTOR_SRC1_ALPHA:
        *dest = AGC_BLEND_FACTOR_SRC1_ALPHA; return true;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
        *dest = AGC_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA; return true;
    default: return false;
    }
}

static bool native_stencil_face(const VkStencilOpState *source,
                                AgcStencilFaceState *dest)
{
    if (!source || source->failOp > VK_STENCIL_OP_DECREMENT_AND_WRAP ||
        source->passOp > VK_STENCIL_OP_DECREMENT_AND_WRAP ||
        source->depthFailOp > VK_STENCIL_OP_DECREMENT_AND_WRAP ||
        source->compareOp > VK_COMPARE_OP_ALWAYS)
        return false;
    *dest = (AgcStencilFaceState)AGC_STENCIL_FACE_STATE_INIT;
    dest->fail_operation = (AgcStencilOperation)source->failOp;
    dest->pass_operation = (AgcStencilOperation)source->passOp;
    dest->depth_fail_operation = (AgcStencilOperation)source->depthFailOp;
    dest->compare_operation = (AgcCompareOperation)source->compareOp;
    dest->compare_mask = source->compareMask;
    dest->write_mask = source->writeMask;
    dest->reference = source->reference;
    return true;
}

typedef struct PsbcSpecialization {
    OpenAgcPsbcSpecializationConstant constants[
        OPENAGC_PSBC_MAX_SPECIALIZATION_CONSTANTS];
    uint32_t count;
    const void *data;
    size_t data_size;
} PsbcSpecialization;

static VkResult translate_specialization(const VkSpecializationInfo *source,
                                         PsbcSpecialization *dest) {
    memset(dest, 0, sizeof(*dest));
    if (!source) return VK_SUCCESS;
    if (source->mapEntryCount > OPENAGC_PSBC_MAX_SPECIALIZATION_CONSTANTS ||
        (source->mapEntryCount && !source->pMapEntries) ||
        (source->dataSize && !source->pData))
        return VK_ERROR_INITIALIZATION_FAILED;
    dest->count = source->mapEntryCount;
    dest->data = source->pData;
    dest->data_size = source->dataSize;
    for (uint32_t i = 0; i < dest->count; ++i) {
        dest->constants[i].constant_id = source->pMapEntries[i].constantID;
        dest->constants[i].offset = source->pMapEntries[i].offset;
        dest->constants[i].size = source->pMapEntries[i].size;
    }
    return VK_SUCCESS;
}

static VkResult compile_stage(const VkPipelineShaderStageCreateInfo *stage,
                              OpenAgcPsbcStage psbc_stage,
                              const VkPipelineShaderStageCreateInfo *pre_stage,
                              OpenAgcPsbcStage psbc_pre_stage,
                              const VkPipelineShaderStageCreateInfo *interface_stage,
                              OpenAgcPsbcStage psbc_interface_stage,
                              const OpenAgcPsbcPipelineContext *context,
                              OpenAgcPsbcOutput *output) {
    const uint32_t runtime_api_version = openagcPsbcGetApiVersion();
    if (runtime_api_version != OPENAGC_PSBC_API_VERSION) {
        fprintf(stderr,
                "vulkan-ps5: PSBC API mismatch headers=%u runtime=%u\n",
                OPENAGC_PSBC_API_VERSION, runtime_api_version);
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    if (!stage || stage->sType != VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ||
        !stage->module || !stage->pName)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (pre_stage &&
        (pre_stage->sType != VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ||
         !pre_stage->module || !pre_stage->pName))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (interface_stage &&
        (interface_stage->sType !=
             VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO ||
         !interface_stage->module || !interface_stage->pName))
        return VK_ERROR_INITIALIZATION_FAILED;
    const VkPs5ShaderModule *module = (const VkPs5ShaderModule *)stage->module;
    const VkPs5ShaderModule *pre_module = pre_stage ?
        (const VkPs5ShaderModule *)pre_stage->module : NULL;
    const VkPs5ShaderModule *interface_module = interface_stage ?
        (const VkPs5ShaderModule *)interface_stage->module : NULL;
    PsbcSpecialization specialization, pre_specialization,
                       interface_specialization;
    VkResult result = translate_specialization(stage->pSpecializationInfo,
                                                &specialization);
    if (result != VK_SUCCESS) return result;
    result = translate_specialization(pre_stage ? pre_stage->pSpecializationInfo : NULL,
                                      &pre_specialization);
    if (result != VK_SUCCESS) return result;
    result = translate_specialization(
        interface_stage ? interface_stage->pSpecializationInfo : NULL,
        &interface_specialization);
    if (result != VK_SUCCESS) return result;
    const OpenAgcPsbcCompileInfo info = {
        .api_version = OPENAGC_PSBC_API_VERSION,
        .stage = psbc_stage,
        .spirv = module->code,
        .spirv_size = module->code_size,
        .entry_point = stage->pName,
        .pre_stage = psbc_pre_stage,
        .pre_spirv = pre_module ? pre_module->code : NULL,
        .pre_spirv_size = pre_module ? pre_module->code_size : 0,
        .pre_entry_point = pre_stage ? pre_stage->pName : NULL,
        .pre_specialization_constants = pre_specialization.count ?
            pre_specialization.constants : NULL,
        .pre_specialization_constant_count = pre_specialization.count,
        .pre_specialization_data = pre_specialization.data,
        .pre_specialization_data_size = pre_specialization.data_size,
        .interface_stage = psbc_interface_stage,
        .interface_spirv = interface_module ? interface_module->code : NULL,
        .interface_spirv_size = interface_module ?
            interface_module->code_size : 0,
        .interface_entry_point = interface_stage ? interface_stage->pName : NULL,
        .interface_specialization_constants = interface_specialization.count ?
            interface_specialization.constants : NULL,
        .interface_specialization_constant_count =
            interface_specialization.count,
        .interface_specialization_data = interface_specialization.data,
        .interface_specialization_data_size = interface_specialization.data_size,
        .specialization_constants = specialization.count ?
            specialization.constants : NULL,
        .specialization_constant_count = specialization.count,
        .specialization_data = specialization.data,
        .specialization_data_size = specialization.data_size,
        .pipeline = context,
        .optimize = true,
    };
    const OpenAgcPsbcResult compile_result = openagcPsbcCompile(&info, output);
    if (compile_result != OPENAGC_PSBC_SUCCESS)
        fprintf(stderr, "vulkan-ps5: PSBC stage=%u failed: %s (%d)\n",
                (unsigned)psbc_stage,
                openagcPsbcResultString(compile_result), compile_result);
    return psbc_result(compile_result);
}

static void free_pipeline(VkDevice device, const VkAllocationCallbacks *allocator,
                          VkPs5Pipeline *pipeline) {
    if (!pipeline) return;
    if (pipeline->native_compute_pipeline)
        vk_ps5_destroy_or_defer_native(device,
            VK_PS5_NATIVE_COMPUTE_PIPELINE,
            pipeline->native_compute_pipeline);
    if (pipeline->native_graphics_pipeline)
        vk_ps5_destroy_or_defer_native(device,
            VK_PS5_NATIVE_GRAPHICS_PIPELINE,
            pipeline->native_graphics_pipeline);
    for (uint32_t i = 0; i < pipeline->stage_count; ++i) {
        if (pipeline->runtime[i].native_shader)
            vk_ps5_destroy_or_defer_native(device, VK_PS5_NATIVE_SHADER,
                pipeline->runtime[i].native_shader);
        openagcPsbcFreeOutput(&pipeline->stages[i]);
    }
    vk_ps5_device_free(device, allocator, pipeline);
}

static VkResult finalize_runtime_shader(
    VkDevice device, const VkAllocationCallbacks *allocator,
    OpenAgcPsbcOutput *output, VkPs5RuntimeShader *runtime) {
    AgcShaderDesc native_desc = AGC_SHADER_DESC_INIT;

    native_desc.stage = output->metadata.stage;
    native_desc.code = output->shader.data;
    native_desc.code_size = output->shader.size;
    native_desc.reflection = &output->metadata;
    native_desc.front_code = output->front_shader.data;
    native_desc.front_code_size = output->front_shader.size;
    int32_t native_result = agcCreateShader(vk_ps5_native_device(device),
        &native_desc, &runtime->native_shader);
    if (native_result != AGC_OK) {
        fprintf(stderr, "vulkan-ps5: native shader creation failed: 0x%08x\n",
            (unsigned)native_result);
        return native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_INVALID_SHADER_NV;
    }
    (void)device;
    (void)allocator;
    return VK_SUCCESS;
}

static VkResult finalize_pipeline(
    VkDevice device, const VkAllocationCallbacks *allocator,
    VkPs5Pipeline *pipeline) {
    for (uint32_t i = 0; i < pipeline->stage_count; ++i) {
        VkResult result = finalize_runtime_shader(
            device, allocator, &pipeline->stages[i], &pipeline->runtime[i]);
        if (result != VK_SUCCESS) return result;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                         uint32_t createInfoCount,
                         const VkComputePipelineCreateInfo *pCreateInfos,
                         const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    (void)pipelineCache;
    if (!device || !pPipelines || (createInfoCount && !pCreateInfos))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const VkComputePipelineCreateInfo *create = &pCreateInfos[i];
        if (create->sType != VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO ||
            create->stage.stage != VK_SHADER_STAGE_COMPUTE_BIT || !create->layout)
            return VK_ERROR_INITIALIZATION_FAILED;
        const VkPs5PipelineLayout *layout = (const VkPs5PipelineLayout *)create->layout;
        const OpenAgcPsbcPipelineContext context = {
            .descriptor_bindings = layout->bindings,
            .descriptor_binding_count = layout->binding_count,
            .push_constant_size = layout->push_constant_size,
        };
        VkPs5Pipeline *pipeline = alloc_object(device, pAllocator, sizeof(*pipeline),
                                                _Alignof(VkPs5Pipeline));
        if (!pipeline) return VK_ERROR_OUT_OF_HOST_MEMORY;
        VkResult result = compile_stage(&create->stage, OPENAGC_PSBC_STAGE_COMPUTE,
                                        NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                        NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                        &context, &pipeline->stages[0]);
        if (result != VK_SUCCESS) {
            fprintf(stderr,
                "vulkan-ps5: compute shader compilation failed result=%d\n",
                result);
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        pipeline->stage_count = 1;
        pipeline->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        pipeline->stage_types[0] = OPENAGC_PSBC_STAGE_COMPUTE;
        result = finalize_pipeline(device, pAllocator, pipeline);
        if (result != VK_SUCCESS) {
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        AgcComputePipelineDesc native_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
        native_desc.shader = pipeline->runtime[0].native_shader;
        native_desc.local_size_x = pipeline->stages[0].metadata.local_size_x;
        native_desc.local_size_y = pipeline->stages[0].metadata.local_size_y;
        native_desc.local_size_z = pipeline->stages[0].metadata.local_size_z;
        native_desc.descriptor_mappings =
            pipeline->stages[0].metadata.descriptor_mappings;
        native_desc.descriptor_mapping_count =
            pipeline->stages[0].metadata.descriptor_mapping_count;
        native_desc.push_constant_ranges =
            pipeline->stages[0].metadata.push_constant_ranges;
        native_desc.push_constant_range_count =
            pipeline->stages[0].metadata.push_constant_range_count;
        int32_t native_result = agcCreateComputePipeline(
            vk_ps5_native_device(device), &native_desc,
            &pipeline->native_compute_pipeline);
        if (native_result != AGC_OK) {
            fprintf(stderr,
                "vulkan-ps5: native compute pipeline creation failed: 0x%08x\n",
                (unsigned)native_result);
            free_pipeline(device, pAllocator, pipeline);
            return native_result == AGC_ERROR_OUT_OF_MEMORY ?
                VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
        }
        pPipelines[i] = (VkPipeline)pipeline;
    }
    return VK_SUCCESS;
}

VkResult vk_ps5_initialize_meta_clear(VkDevice device,
                                      VkPipelineLayout *layout_out,
                                      VkPipeline *pipeline_out)
{
    if (!device || !layout_out || !pipeline_out)
        return VK_ERROR_INITIALIZATION_FAILED;
    *layout_out = VK_NULL_HANDLE;
    *pipeline_out = VK_NULL_HANDLE;

    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0u,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo set_create = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1u,
        .pBindings = &binding,
    };
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorSetLayout(
        device, &set_create, NULL, &set_layout);
    if (result != VK_SUCCESS)
        return result;

    const VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0u,
        .size = 32u,
    };
    const VkPipelineLayoutCreateInfo layout_create = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1u,
        .pPushConstantRanges = &push_range,
    };
    result = vkCreatePipelineLayout(
        device, &layout_create, NULL, layout_out);
    vkDestroyDescriptorSetLayout(device, set_layout, NULL);
    if (result != VK_SUCCESS)
        return result;

    const VkShaderModuleCreateInfo shader_create = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(vulkan_ps5_meta_clear_color_spv),
        .pCode = vulkan_ps5_meta_clear_color_spv,
    };
    VkShaderModule shader = VK_NULL_HANDLE;
    result = vkCreateShaderModule(device, &shader_create, NULL, &shader);
    if (result == VK_SUCCESS) {
        const VkComputePipelineCreateInfo pipeline_create = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shader,
                .pName = "main",
            },
            .layout = *layout_out,
        };
        result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1u,
            &pipeline_create, NULL, pipeline_out);
    }
    vkDestroyShaderModule(device, shader, NULL);
    if (result != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, *layout_out, NULL);
        *layout_out = VK_NULL_HANDLE;
    }
    return result;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                          uint32_t createInfoCount,
                          const VkGraphicsPipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines) {
    (void)pipelineCache;
    if (!device || !pPipelines || (createInfoCount && !pCreateInfos))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < createInfoCount; ++i) pPipelines[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        const VkGraphicsPipelineCreateInfo *create = &pCreateInfos[i];
        const VkPipelineRenderingCreateInfo *rendering = NULL;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)create->pNext;
             next; next = next->pNext) {
            if (next->sType ==
                    VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO) {
                if (rendering)
                    return VK_ERROR_INITIALIZATION_FAILED;
                rendering = (const VkPipelineRenderingCreateInfo *)next;
            }
        }
        if (create->sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO ||
            !create->layout || (!create->renderPass && !rendering) ||
            !create->pVertexInputState ||
            !create->pInputAssemblyState)
            return VK_ERROR_INITIALIZATION_FAILED;
        if (rendering && (rendering->pNext || rendering->viewMask ||
                rendering->colorAttachmentCount >
                    AGC_GFX1013_MAX_COLOR_TARGETS ||
                (rendering->colorAttachmentCount &&
                 !rendering->pColorAttachmentFormats)))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        AgcGfx1013PrimitiveTopology translated_topology;
        if (!primitive_topology(create->pInputAssemblyState->topology,
                &translated_topology))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (create->pInputAssemblyState->primitiveRestartEnable &&
            create->pInputAssemblyState->topology !=
                VK_PRIMITIVE_TOPOLOGY_LINE_STRIP &&
            create->pInputAssemblyState->topology !=
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
            create->pInputAssemblyState->topology !=
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN &&
            create->pInputAssemblyState->topology !=
                VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY &&
            create->pInputAssemblyState->topology !=
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        VkBool32 dynamic_depth_bias = VK_FALSE;
        VkBool32 dynamic_line_width = VK_FALSE;
        VkBool32 dynamic_blend_constants = VK_FALSE;
        VkBool32 dynamic_stencil_reference = VK_FALSE;
        VkBool32 dynamic_viewport = VK_FALSE;
        VkBool32 dynamic_scissor = VK_FALSE;
        if (create->pDynamicState) {
            if (create->pDynamicState->dynamicStateCount &&
                !create->pDynamicState->pDynamicStates)
                return VK_ERROR_INITIALIZATION_FAILED;
            for (uint32_t dynamic_index = 0u;
                 dynamic_index < create->pDynamicState->dynamicStateCount;
                 ++dynamic_index) {
                switch (create->pDynamicState->pDynamicStates[dynamic_index]) {
                case VK_DYNAMIC_STATE_DEPTH_BIAS:
                    if (dynamic_depth_bias)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_depth_bias = VK_TRUE;
                    break;
                case VK_DYNAMIC_STATE_LINE_WIDTH:
                    if (dynamic_line_width)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_line_width = VK_TRUE;
                    break;
                case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
                    if (dynamic_blend_constants)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_blend_constants = VK_TRUE;
                    break;
                case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
                    if (dynamic_stencil_reference)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_stencil_reference = VK_TRUE;
                    break;
                case VK_DYNAMIC_STATE_VIEWPORT:
                    if (dynamic_viewport)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_viewport = VK_TRUE;
                    break;
                case VK_DYNAMIC_STATE_SCISSOR:
                    if (dynamic_scissor)
                        return VK_ERROR_FEATURE_NOT_PRESENT;
                    dynamic_scissor = VK_TRUE;
                    break;
                default:
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
            }
        }
        if (!create->pViewportState ||
            create->pViewportState->sType !=
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO ||
            !create->pViewportState->viewportCount ||
            create->pViewportState->viewportCount > VK_PS5_MAX_VIEWPORTS ||
            create->pViewportState->scissorCount !=
                create->pViewportState->viewportCount ||
            (!dynamic_viewport && !create->pViewportState->pViewports) ||
            (!dynamic_scissor && !create->pViewportState->pScissors))
            return VK_ERROR_INITIALIZATION_FAILED;
        if (!create->pRasterizationState || !create->pMultisampleState ||
            !create->pColorBlendState)
            return VK_ERROR_INITIALIZATION_FAILED;
        const VkPipelineRasterizationStateCreateInfo *raster =
            create->pRasterizationState;
        const VkPipelineMultisampleStateCreateInfo *multisample =
            create->pMultisampleState;
        const VkPipelineColorBlendStateCreateInfo *blend =
            create->pColorBlendState;
        const VkPipelineRasterizationLineStateCreateInfo *line_state = NULL;
        const VkPipelineRasterizationDepthClipStateCreateInfoEXT *
            depth_clip_state = NULL;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)raster->pNext;
             next; next = next->pNext) {
            if (next->sType ==
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO) {
                if (line_state)
                    return VK_ERROR_INITIALIZATION_FAILED;
                line_state =
                    (const VkPipelineRasterizationLineStateCreateInfo *)next;
            } else if (next->sType ==
                    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT) {
                if (depth_clip_state)
                    return VK_ERROR_INITIALIZATION_FAILED;
                depth_clip_state =
                    (const VkPipelineRasterizationDepthClipStateCreateInfoEXT *)next;
            }
        }
        if (line_state &&
            ((line_state->lineRasterizationMode !=
                  VK_LINE_RASTERIZATION_MODE_DEFAULT &&
              line_state->lineRasterizationMode !=
                  VK_LINE_RASTERIZATION_MODE_RECTANGULAR) ||
             line_state->stippledLineEnable))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (depth_clip_state &&
            (!vk_ps5_device_depth_clip_enable(device) ||
             depth_clip_state->flags != 0u))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        AgcGfx1013PolygonMode translated_polygon_mode;
        if (!polygon_mode(raster->polygonMode, &translated_polygon_mode) ||
            (raster->cullMode & ~(VK_CULL_MODE_FRONT_BIT |
                                 VK_CULL_MODE_BACK_BIT)) != 0u ||
            (raster->frontFace != VK_FRONT_FACE_COUNTER_CLOCKWISE &&
             raster->frontFace != VK_FRONT_FACE_CLOCKWISE) ||
            (!dynamic_line_width && (!(raster->lineWidth >= 1.0f) ||
                !(raster->lineWidth <= 64.0f))) ||
            (multisample->rasterizationSamples != VK_SAMPLE_COUNT_1_BIT &&
             multisample->rasterizationSamples != VK_SAMPLE_COUNT_4_BIT) ||
            (multisample->sampleShadingEnable &&
             (!(multisample->minSampleShading >= 0.0f) ||
              !(multisample->minSampleShading <= 1.0f))) ||
            multisample->alphaToCoverageEnable ||
            !blend->attachmentCount ||
            blend->attachmentCount > AGC_GFX1013_MAX_COLOR_TARGETS ||
            !blend->pAttachments)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        AgcGfx1013ViewportArrayState viewport_state = {
            .count = create->pViewportState->viewportCount,
        };
        for (uint32_t viewport_index = 0u;
             viewport_index < viewport_state.count; ++viewport_index) {
            if ((!dynamic_viewport && !translate_viewport(
                    &create->pViewportState->pViewports[viewport_index],
                    &viewport_state.viewports[viewport_index])) ||
                (!dynamic_scissor && !translate_scissor(
                    &create->pViewportState->pScissors[viewport_index],
                    &viewport_state.scissors[viewport_index])))
                return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        const VkPipelineShaderStageCreateInfo *vertex = NULL, *tess_control = NULL;
        const VkPipelineShaderStageCreateInfo *tess_evaluation = NULL;
        const VkPipelineShaderStageCreateInfo *geometry = NULL, *fragment = NULL;
        for (uint32_t j = 0; j < create->stageCount; ++j) {
            const VkPipelineShaderStageCreateInfo *stage = &create->pStages[j];
            const VkPipelineShaderStageCreateInfo **slot = NULL;
            switch (stage->stage) {
            case VK_SHADER_STAGE_VERTEX_BIT: slot = &vertex; break;
            case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: slot = &tess_control; break;
            case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: slot = &tess_evaluation; break;
            case VK_SHADER_STAGE_GEOMETRY_BIT: slot = &geometry; break;
            case VK_SHADER_STAGE_FRAGMENT_BIT: slot = &fragment; break;
            default: return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            if (*slot) return VK_ERROR_INITIALIZATION_FAILED;
            *slot = stage;
        }
        if (!vertex || !fragment) return VK_ERROR_FEATURE_NOT_PRESENT;
        if ((tess_control == NULL) != (tess_evaluation == NULL))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if ((tess_control && create->pInputAssemblyState->topology !=
                VK_PRIMITIVE_TOPOLOGY_PATCH_LIST) ||
            (!tess_control && create->pInputAssemblyState->topology ==
                VK_PRIMITIVE_TOPOLOGY_PATCH_LIST))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (tess_control && (!create->pTessellationState ||
            !create->pTessellationState->patchControlPoints))
            return VK_ERROR_INITIALIZATION_FAILED;

        const VkPipelineVertexInputStateCreateInfo *vertex_input = create->pVertexInputState;
        if (vertex_input->vertexAttributeDescriptionCount >
                OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES ||
            vertex_input->vertexBindingDescriptionCount >
                VK_PS5_MAX_VERTEX_BINDINGS ||
            (vertex_input->vertexAttributeDescriptionCount &&
                !vertex_input->pVertexAttributeDescriptions) ||
            (vertex_input->vertexBindingDescriptionCount &&
                !vertex_input->pVertexBindingDescriptions))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        uint32_t vertex_binding_mask = 0u;
        uint32_t vertex_strides[VK_PS5_MAX_VERTEX_BINDINGS] = {0};
        VkVertexInputRate vertex_rates[VK_PS5_MAX_VERTEX_BINDINGS] = {0};
        uint32_t vertex_divisors[VK_PS5_MAX_VERTEX_BINDINGS] = {0};
        for (uint32_t j = 0;
             j < vertex_input->vertexBindingDescriptionCount; ++j) {
            const VkVertexInputBindingDescription *binding =
                &vertex_input->pVertexBindingDescriptions[j];
            if (binding->binding >= VK_PS5_MAX_VERTEX_BINDINGS ||
                (binding->inputRate != VK_VERTEX_INPUT_RATE_VERTEX &&
                 binding->inputRate != VK_VERTEX_INPUT_RATE_INSTANCE) ||
                binding->stride == 0u || binding->stride > 2048u ||
                (vertex_binding_mask & (1u << binding->binding)))
                return VK_ERROR_FEATURE_NOT_PRESENT;
            vertex_binding_mask |= 1u << binding->binding;
            vertex_strides[binding->binding] = binding->stride;
            vertex_rates[binding->binding] = binding->inputRate;
            vertex_divisors[binding->binding] =
                binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE ? 1u : 0u;
        }
        const VkPipelineVertexInputDivisorStateCreateInfoEXT *divisor_state = NULL;
        for (const VkBaseInStructure *next =
                 (const VkBaseInStructure *)vertex_input->pNext;
             next; next = next->pNext) {
            if (next->sType ==
                    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT) {
                if (divisor_state)
                    return VK_ERROR_INITIALIZATION_FAILED;
                divisor_state =
                    (const VkPipelineVertexInputDivisorStateCreateInfoEXT *)next;
            }
        }
        uint32_t divisor_binding_mask = 0u;
        if (divisor_state &&
            (divisor_state->vertexBindingDivisorCount &&
             !divisor_state->pVertexBindingDivisors))
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t j = 0;
             divisor_state && j < divisor_state->vertexBindingDivisorCount; ++j) {
            const VkVertexInputBindingDivisorDescriptionEXT *divisor =
                &divisor_state->pVertexBindingDivisors[j];
            if (divisor->binding >= VK_PS5_MAX_VERTEX_BINDINGS ||
                !(vertex_binding_mask & (1u << divisor->binding)) ||
                vertex_rates[divisor->binding] != VK_VERTEX_INPUT_RATE_INSTANCE ||
                divisor->divisor == 0u ||
                (divisor_binding_mask & (1u << divisor->binding)))
                return VK_ERROR_FEATURE_NOT_PRESENT;
            divisor_binding_mask |= 1u << divisor->binding;
            vertex_divisors[divisor->binding] = divisor->divisor;
        }
        OpenAgcPsbcVertexAttribute attributes[OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES];
        uint32_t vertex_attribute_mask = 0u;
        for (uint32_t j = 0; j < vertex_input->vertexAttributeDescriptionCount; ++j) {
            const VkVertexInputAttributeDescription *source =
                &vertex_input->pVertexAttributeDescriptions[j];
            uint32_t stride = 0;
            bool found_binding = false;
            for (uint32_t k = 0; k < vertex_input->vertexBindingDescriptionCount; ++k) {
                const VkVertexInputBindingDescription *binding =
                    &vertex_input->pVertexBindingDescriptions[k];
                if (binding->binding == source->binding) {
                    stride = binding->stride;
                    found_binding = true;
                    break;
                }
            }
            if (!found_binding || source->location >=
                    OPENAGC_PSBC_MAX_VERTEX_ATTRIBUTES ||
                (vertex_attribute_mask & (1u << source->location)) ||
                !psbc_vertex_format(source->format, &attributes[j].format))
                return VK_ERROR_FORMAT_NOT_SUPPORTED;
            vertex_attribute_mask |= 1u << source->location;
            attributes[j].location = source->location;
            attributes[j].binding = source->binding;
            attributes[j].offset = source->offset;
            attributes[j].stride = stride;
            attributes[j].input_rate =
                vertex_rates[source->binding] == VK_VERTEX_INPUT_RATE_INSTANCE ?
                OPENAGC_PSBC_VERTEX_INPUT_RATE_INSTANCE :
                OPENAGC_PSBC_VERTEX_INPUT_RATE_VERTEX;
            attributes[j].divisor = vertex_divisors[source->binding];
        }
        const VkPs5PipelineLayout *layout = (const VkPs5PipelineLayout *)create->layout;
        const VkPs5RenderPass *render_pass = (const VkPs5RenderPass *)create->renderPass;
        uint32_t color_attachment_count;
        VkBool32 has_depth_stencil;
        VkBool32 depth_stencil_read_only = VK_FALSE;
        if (render_pass) {
            if (create->subpass >= render_pass->subpass_count)
                return VK_ERROR_INITIALIZATION_FAILED;
            if (multisample->rasterizationSamples !=
                    render_pass->subpasses[create->subpass].samples)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            color_attachment_count =
                render_pass->subpasses[create->subpass].color_attachment_count;
            has_depth_stencil = render_pass->subpasses[create->subpass].
                depth_stencil_attachment != VK_ATTACHMENT_UNUSED;
            depth_stencil_read_only = render_pass->subpasses[create->subpass].
                depth_stencil_layout ==
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        } else {
            color_attachment_count = rendering->colorAttachmentCount;
            has_depth_stencil = rendering->depthAttachmentFormat !=
                    VK_FORMAT_UNDEFINED ||
                rendering->stencilAttachmentFormat != VK_FORMAT_UNDEFINED;
            for (uint32_t attachment = 0;
                 attachment < color_attachment_count; ++attachment) {
                AgcGfx1013ColorTargetFormat target_format;
                if (rendering->pColorAttachmentFormats[attachment] ==
                        VK_FORMAT_UNDEFINED ||
                    !color_target_format(
                        rendering->pColorAttachmentFormats[attachment],
                        &target_format))
                    return VK_ERROR_FORMAT_NOT_SUPPORTED;
            }
            if (has_depth_stencil) {
                AgcGfx1013DepthSurfaceFormat depth_format;
                VkFormat format = rendering->depthAttachmentFormat !=
                        VK_FORMAT_UNDEFINED ?
                    rendering->depthAttachmentFormat :
                    rendering->stencilAttachmentFormat;
                if (!depth_surface_format(format, &depth_format) ||
                    (rendering->depthAttachmentFormat != VK_FORMAT_UNDEFINED &&
                     rendering->stencilAttachmentFormat != VK_FORMAT_UNDEFINED &&
                     rendering->depthAttachmentFormat !=
                        rendering->stencilAttachmentFormat))
                    return VK_ERROR_FORMAT_NOT_SUPPORTED;
            }
        }
        AgcGfx1013ColorBlendState color_blend;
        bool dual_source_blend = false;
        if (!color_blend_state(blend, color_attachment_count, &color_blend,
                               &dual_source_blend))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        AgcGfx1013DepthStencilState depth_stencil = {0};
        if (has_depth_stencil) {
            const VkPipelineDepthStencilStateCreateInfo *depth =
                create->pDepthStencilState;
            if (!depth || depth->sType !=
                    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO)
                return VK_ERROR_INITIALIZATION_FAILED;
            if (depth->depthBoundsTestEnable ||
                depth->depthCompareOp > VK_COMPARE_OP_ALWAYS ||
                !stencil_face_state(&depth->front, &depth_stencil.front) ||
                !stencil_face_state(&depth->back, &depth_stencil.back))
                return VK_ERROR_FEATURE_NOT_PRESENT;
            depth_stencil.depth_test_enable = depth->depthTestEnable;
            depth_stencil.depth_write_enable =
                depth->depthTestEnable && depth->depthWriteEnable;
            depth_stencil.depth_compare_operation =
                (AgcGfx1013CompareOp)depth->depthCompareOp;
            depth_stencil.min_depth_bounds = depth->minDepthBounds;
            depth_stencil.max_depth_bounds = depth->maxDepthBounds;
            depth_stencil.stencil_test_enable = depth->stencilTestEnable;
            depth_stencil.back_face_enable = depth->stencilTestEnable;
            if (depth_stencil_read_only &&
                (depth->depthWriteEnable ||
                 (depth->stencilTestEnable &&
                  (depth->front.writeMask || depth->back.writeMask))))
                return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        OpenAgcPsbcPipelineContext context = {
            .vertex_attributes = attributes,
            .vertex_attribute_count = vertex_input->vertexAttributeDescriptionCount,
            .descriptor_bindings = layout->bindings,
            .descriptor_binding_count = layout->binding_count,
            .push_constant_size = layout->push_constant_size,
            .color_attachment_count =
                color_attachment_count,
            .rasterization_samples =
                (uint32_t)multisample->rasterizationSamples,
            .pixel_shader_sample_count = 1u,
            .dual_source_blend = dual_source_blend,
            .alpha_to_one = multisample->alphaToOneEnable,
            .tessellation_control_points = tess_control ?
                create->pTessellationState->patchControlPoints : 3,
            .tessellation_patches = 8,
            .robust_buffer_access =
                vk_ps5_device_robust_buffer_access(device),
        };
        if (multisample->sampleShadingEnable &&
            multisample->rasterizationSamples == VK_SAMPLE_COUNT_4_BIT) {
            const float minimum = multisample->minSampleShading * 4.0f;
            context.pixel_shader_sample_count =
                minimum > 2.0f ? 4u : (minimum > 1.0f ? 2u : 1u);
        }
        VkPs5Pipeline *pipeline = alloc_object(device, pAllocator, sizeof(*pipeline),
                                                _Alignof(VkPs5Pipeline));
        if (!pipeline) return VK_ERROR_OUT_OF_HOST_MEMORY;
        pipeline->vertex_binding_mask = vertex_binding_mask;
        pipeline->robust_buffer_access = context.robust_buffer_access;
        pipeline->vertex_attribute_mask = vertex_attribute_mask;
        for (uint32_t j = 0;
             j < vertex_input->vertexAttributeDescriptionCount; ++j) {
            const OpenAgcPsbcVertexAttribute *attribute = &attributes[j];
            pipeline->vertex_attribute_bindings[attribute->location] =
                attribute->binding;
            pipeline->vertex_attribute_offsets[attribute->location] =
                attribute->offset;
            pipeline->vertex_attribute_sizes[attribute->location] =
                psbc_vertex_format_size(attribute->format);
        }
        pipeline->color_blend = color_blend;
        pipeline->viewport_state = viewport_state;
        pipeline->viewport_dynamic = dynamic_viewport;
        pipeline->scissor_dynamic = dynamic_scissor;
        const uint32_t sample_count =
            (uint32_t)multisample->rasterizationSamples;
        pipeline->sample_state = (AgcGfx1013SampleState){
            .sample_count = sample_count,
            .pixel_shader_sample_count = context.pixel_shader_sample_count,
            .sample_mask = multisample->pSampleMask ?
                multisample->pSampleMask[0] & ((1u << sample_count) - 1u) :
                (1u << sample_count) - 1u,
        };
        pipeline->polygon_mode = translated_polygon_mode;
        pipeline->primitive_size = (AgcGfx1013PrimitiveSizeState){
            .point_size = 1.0f,
            .point_size_min = 1.0f,
            .point_size_max = 64.0f,
            .line_width = dynamic_line_width ? 1.0f : raster->lineWidth,
        };
        pipeline->line_width_dynamic = dynamic_line_width;
        pipeline->blend_constants_dynamic = dynamic_blend_constants;
        pipeline->stencil_reference_dynamic = dynamic_stencil_reference;
        pipeline->depth_bias = (AgcGfx1013DepthBiasState){
            .constant_factor = raster->depthBiasConstantFactor,
            .clamp = raster->depthBiasClamp,
            .slope_factor = raster->depthBiasSlopeFactor,
        };
        pipeline->depth_bias_enable = raster->depthBiasEnable;
        pipeline->depth_bias_dynamic = dynamic_depth_bias;
        pipeline->depth_clamp_enable = raster->depthClampEnable;
        pipeline->native_rasterization_flags = depth_clip_state ?
            (depth_clip_state->depthClipEnable ?
                AGC_RASTERIZATION_DEPTH_CLIP_ENABLE_BIT :
                AGC_RASTERIZATION_DEPTH_CLIP_DISABLE_BIT) : 0u;
        pipeline->rasterizer_discard_enable =
            raster->rasterizerDiscardEnable;
        pipeline->cull_mode = (AgcCullModeFlags)raster->cullMode;
        pipeline->front_face = raster->frontFace == VK_FRONT_FACE_CLOCKWISE ?
            AGC_FRONT_FACE_CLOCKWISE : AGC_FRONT_FACE_COUNTER_CLOCKWISE;
        pipeline->primitive_restart_enable =
            create->pInputAssemblyState->primitiveRestartEnable;
        pipeline->has_depth_stencil = has_depth_stencil;
        pipeline->depth_stencil = depth_stencil;
        pipeline->dynamic_rendering = render_pass == NULL;
        if (rendering) {
            pipeline->dynamic_color_attachment_count =
                rendering->colorAttachmentCount;
            if (rendering->colorAttachmentCount)
                memcpy(pipeline->dynamic_color_formats,
                    rendering->pColorAttachmentFormats,
                    rendering->colorAttachmentCount * sizeof(VkFormat));
            pipeline->dynamic_depth_format =
                rendering->depthAttachmentFormat;
            pipeline->dynamic_stencil_format =
                rendering->stencilAttachmentFormat;
        }
        memcpy(pipeline->vertex_strides, vertex_strides,
               sizeof(vertex_strides));
        VkResult result = VK_SUCCESS;
        uint32_t compiled = 0;
        if (tess_control) {
            context.enable_ngg = false;
            context.wave32 = true;
            result = compile_stage(tess_control, OPENAGC_PSBC_STAGE_TESS_CONTROL,
                                   vertex, OPENAGC_PSBC_STAGE_VERTEX,
                                   tess_evaluation,
                                   OPENAGC_PSBC_STAGE_TESS_EVALUATION,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_TESS_CONTROL;
                pipeline->stage_count = ++compiled;
            }
            if (result == VK_SUCCESS) {
                context.enable_ngg = true;
                context.wave32 = true;
                result = compile_stage(
                    geometry ? geometry : tess_evaluation,
                    geometry ? OPENAGC_PSBC_STAGE_GEOMETRY :
                               OPENAGC_PSBC_STAGE_TESS_EVALUATION,
                    geometry ? tess_evaluation : NULL,
                    OPENAGC_PSBC_STAGE_TESS_EVALUATION,
                    tess_control, OPENAGC_PSBC_STAGE_TESS_CONTROL,
                    &context, &pipeline->stages[compiled]);
                if (result == VK_SUCCESS) {
                    pipeline->stage_types[compiled] = geometry ?
                        OPENAGC_PSBC_STAGE_GEOMETRY :
                        OPENAGC_PSBC_STAGE_TESS_EVALUATION;
                    pipeline->stage_count = ++compiled;
                }
            }
        } else if (geometry) {
            context.enable_ngg = true;
            context.wave32 = true;
            result = compile_stage(geometry, OPENAGC_PSBC_STAGE_GEOMETRY,
                                   vertex, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_GEOMETRY;
                pipeline->stage_count = ++compiled;
            }
        } else {
            context.enable_ngg = true;
            context.wave32 = true;
            result = compile_stage(vertex, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_VERTEX;
                pipeline->stage_count = ++compiled;
            }
        }
        if (result == VK_SUCCESS) {
            context.enable_ngg = false;
            result = compile_stage(fragment, OPENAGC_PSBC_STAGE_FRAGMENT,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   NULL, OPENAGC_PSBC_STAGE_VERTEX,
                                   &context, &pipeline->stages[compiled]);
            if (result == VK_SUCCESS) {
                pipeline->stage_types[compiled] = OPENAGC_PSBC_STAGE_FRAGMENT;
                pipeline->stage_count = ++compiled;
            }
        }
        if (result != VK_SUCCESS) {
            fprintf(stderr,
                "vulkan-ps5: graphics shader compilation failed result=%d compiled=%u\n",
                result, compiled);
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        for (uint32_t stage_index = 0; stage_index < compiled; ++stage_index) {
            if (pipeline->stage_types[stage_index] ==
                    OPENAGC_PSBC_STAGE_FRAGMENT &&
                pipeline->stages[stage_index].metadata.
                    pixel_shader_sample_count >
                    pipeline->sample_state.pixel_shader_sample_count)
                pipeline->sample_state.pixel_shader_sample_count =
                    pipeline->stages[stage_index].metadata.
                        pixel_shader_sample_count;
        }
        pipeline->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
        {
            static const uint8_t primitive_types[
                AGC_GFX1013_TOPOLOGY_COUNT] = {
                1u, 2u, 3u, 4u, 6u, 5u, 10u, 11u, 12u, 13u, 9u,
            };
            pipeline->primitive_type = primitive_types[translated_topology];
        }
        result = finalize_pipeline(device, pAllocator, pipeline);
        if (result != VK_SUCCESS) {
            free_pipeline(device, pAllocator, pipeline);
            return result;
        }
        const uint32_t full_sample_mask =
            (1u << (uint32_t)multisample->rasterizationSamples) - 1u;
        const bool native_stage_graph_eligible = tess_control ?
            create->pInputAssemblyState->topology ==
                VK_PRIMITIVE_TOPOLOGY_PATCH_LIST :
            geometry ?
                create->pInputAssemblyState->topology !=
                    VK_PRIMITIVE_TOPOLOGY_PATCH_LIST :
                create->pInputAssemblyState->topology !=
                    VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        const bool native_eligible = native_stage_graph_eligible &&
            (!multisample->pSampleMask ||
             (multisample->pSampleMask[0] & full_sample_mask) ==
                full_sample_mask);
        if (native_eligible) {
            AgcShaderDescriptorMapping native_mappings[
                AGC_SHADER_MAX_DESCRIPTOR_BINDINGS];
            AgcShaderPushConstantRange native_push_ranges[
                AGC_SHADER_MAX_PUSH_CONSTANT_RANGES];
            AgcColorBlendAttachmentState native_attachments[
                AGC_GFX1013_MAX_COLOR_TARGETS];
            uint32_t native_mapping_count = 0u;
            uint32_t native_push_range_count = 0u;
            bool native_layout_valid = true;

            for (uint32_t stage_index = 0u;
                 stage_index < pipeline->stage_count; ++stage_index) {
                const AgcShaderReflection *reflection =
                    &pipeline->stages[stage_index].metadata;
                for (uint32_t mapping_index = 0u;
                     mapping_index < reflection->descriptor_mapping_count;
                     ++mapping_index) {
                    const AgcShaderDescriptorMapping *mapping =
                        &reflection->descriptor_mappings[mapping_index];
                    uint32_t existing = 0u;
                    for (; existing < native_mapping_count; ++existing) {
                        if (native_mappings[existing].set == mapping->set &&
                            native_mappings[existing].binding ==
                                mapping->binding)
                            break;
                    }
                    if (existing < native_mapping_count) {
                        const AgcShaderDescriptorMapping *previous =
                            &native_mappings[existing];
                        if (previous->type != mapping->type ||
                            previous->array_size != mapping->array_size ||
                            previous->byte_offset != mapping->byte_offset ||
                            previous->byte_stride != mapping->byte_stride)
                            native_layout_valid = false;
                    } else if (native_mapping_count <
                               AGC_SHADER_MAX_DESCRIPTOR_BINDINGS) {
                        native_mappings[native_mapping_count++] = *mapping;
                    } else {
                        native_layout_valid = false;
                    }
                }
                for (uint32_t range_index = 0u;
                     range_index < reflection->push_constant_range_count;
                     ++range_index) {
                    const AgcShaderPushConstantRange *range =
                        &reflection->push_constant_ranges[range_index];
                    uint32_t existing = 0u;
                    for (; existing < native_push_range_count; ++existing) {
                        if (native_push_ranges[existing].offset ==
                                range->offset &&
                            native_push_ranges[existing].size == range->size &&
                            native_push_ranges[existing].alignment ==
                                range->alignment)
                            break;
                    }
                    if (existing < native_push_range_count) {
                        native_push_ranges[existing].stage_mask |=
                            range->stage_mask;
                    } else if (native_push_range_count <
                               AGC_SHADER_MAX_PUSH_CONSTANT_RANGES) {
                        native_push_ranges[native_push_range_count++] = *range;
                    } else {
                        native_layout_valid = false;
                    }
                }
            }
            AgcGraphicsPipelineDesc native_desc =
                AGC_GRAPHICS_PIPELINE_DESC_INIT;
            native_desc.primitive_topology =
                (AgcPrimitiveTopology)translated_topology;
            if (tess_control) {
                native_desc.tessellation_control_shader =
                    pipeline->runtime[0].native_shader;
                if (geometry)
                    native_desc.geometry_shader =
                        pipeline->runtime[1].native_shader;
                else
                    native_desc.tessellation_evaluation_shader =
                        pipeline->runtime[1].native_shader;
            } else if (geometry) {
                native_desc.geometry_shader =
                    pipeline->runtime[0].native_shader;
            } else {
                native_desc.vertex_shader =
                    pipeline->runtime[0].native_shader;
            }
            native_desc.pixel_shader =
                pipeline->runtime[pipeline->stage_count - 1u].native_shader;
            native_desc.vertex_inputs =
                pipeline->stages[0].metadata.vertex_inputs;
            native_desc.vertex_input_count =
                pipeline->stages[0].metadata.vertex_input_count;
            native_desc.descriptor_mappings = native_mappings;
            native_desc.descriptor_mapping_count = native_mapping_count;
            native_desc.push_constant_ranges = native_push_ranges;
            native_desc.push_constant_range_count = native_push_range_count;
            native_desc.color_attachments = native_attachments;
            native_desc.color_attachment_count = color_attachment_count;

            for (uint32_t attachment_index = 0u;
                 attachment_index < color_attachment_count; ++attachment_index) {
                VkFormat format;
                if (render_pass) {
                    uint32_t render_attachment = render_pass->subpasses[
                        create->subpass].color_attachments[attachment_index];
                    format = render_pass->attachments[render_attachment].format;
                } else {
                    format = rendering->pColorAttachmentFormats[
                        attachment_index];
                }
                AgcFormat native_format;
                const VkPipelineColorBlendAttachmentState *source =
                    &blend->pAttachments[attachment_index];
                native_attachments[attachment_index] =
                    (AgcColorBlendAttachmentState)
                        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT;
                native_attachments[attachment_index].format =
                    native_image_format(format, &native_format) ?
                        (uint32_t)native_format : 0u;
                native_attachments[attachment_index].blend_enable =
                    source->blendEnable;
                native_attachments[attachment_index].write_mask =
                    source->colorWriteMask;
                native_attachments[attachment_index].color_operation =
                    (AgcBlendOperation)source->colorBlendOp;
                native_attachments[attachment_index].alpha_operation =
                    (AgcBlendOperation)source->alphaBlendOp;
                if (!native_attachments[attachment_index].format ||
                    source->colorBlendOp > VK_BLEND_OP_MAX ||
                    source->alphaBlendOp > VK_BLEND_OP_MAX ||
                    !native_blend_factor(source->srcColorBlendFactor,
                        &native_attachments[attachment_index].
                            source_color_factor) ||
                    !native_blend_factor(source->dstColorBlendFactor,
                        &native_attachments[attachment_index].
                            destination_color_factor) ||
                    !native_blend_factor(source->srcAlphaBlendFactor,
                        &native_attachments[attachment_index].
                            source_alpha_factor) ||
                    !native_blend_factor(source->dstAlphaBlendFactor,
                        &native_attachments[attachment_index].
                            destination_alpha_factor))
                    native_layout_valid = false;
            }

            AgcRasterizationState native_raster =
                AGC_RASTERIZATION_STATE_INIT;
            native_raster.polygon_mode =
                (AgcPolygonMode)raster->polygonMode;
            native_raster.cull_mode = (AgcCullModeFlags)raster->cullMode;
            native_raster.front_face = raster->frontFace ==
                    VK_FRONT_FACE_CLOCKWISE ? AGC_FRONT_FACE_CLOCKWISE :
                    AGC_FRONT_FACE_COUNTER_CLOCKWISE;
            native_raster.depth_clamp_enable = raster->depthClampEnable;
            native_raster.flags = pipeline->native_rasterization_flags;
            native_raster.rasterizer_discard_enable =
                raster->rasterizerDiscardEnable;
            native_raster.depth_bias_enable = raster->depthBiasEnable;
            native_raster.line_width = dynamic_line_width ?
                1.0f : raster->lineWidth;
            native_desc.rasterization = &native_raster;

            AgcDepthBias native_static_depth_bias = AGC_DEPTH_BIAS_INIT;
            if (raster->depthBiasEnable && !dynamic_depth_bias) {
                native_static_depth_bias.constant_factor =
                    raster->depthBiasConstantFactor;
                native_static_depth_bias.clamp = raster->depthBiasClamp;
                native_static_depth_bias.slope_factor =
                    raster->depthBiasSlopeFactor;
                native_desc.static_depth_bias =
                    &native_static_depth_bias;
            }
            native_desc.logic_operation_enable = blend->logicOpEnable;
            native_desc.logic_operation =
                (AgcLogicOperation)blend->logicOp;
            native_desc.primitive_restart_enable =
                create->pInputAssemblyState->primitiveRestartEnable;

            AgcDepthStencilPipelineState native_depth =
                AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
            if (has_depth_stencil) {
                VkFormat format;
                if (render_pass) {
                    uint32_t depth_attachment = render_pass->subpasses[
                        create->subpass].depth_stencil_attachment;
                    format = render_pass->attachments[depth_attachment].format;
                } else {
                    format = rendering->depthAttachmentFormat !=
                            VK_FORMAT_UNDEFINED ?
                        rendering->depthAttachmentFormat :
                        rendering->stencilAttachmentFormat;
                }
                AgcFormat native_format;
                const VkPipelineDepthStencilStateCreateInfo *source =
                    create->pDepthStencilState;
                native_depth.format = native_image_format(format,
                    &native_format) ? (uint32_t)native_format : 0u;
                native_depth.depth_test_enable = source->depthTestEnable;
                native_depth.depth_write_enable = source->depthWriteEnable;
                native_depth.depth_compare_operation =
                    (AgcCompareOperation)source->depthCompareOp;
                native_depth.depth_bounds_enable = source->depthBoundsTestEnable;
                native_depth.stencil_test_enable = source->stencilTestEnable;
                native_depth.min_depth_bounds = source->minDepthBounds;
                native_depth.max_depth_bounds = source->maxDepthBounds;
                native_depth.back_face_enable = source->stencilTestEnable;
                if (!native_depth.format ||
                    !native_stencil_face(&source->front,
                        &native_depth.front) ||
                    !native_stencil_face(&source->back, &native_depth.back))
                    native_layout_valid = false;
                native_desc.depth_stencil = &native_depth;
            }

            AgcMultisampleState native_multisample =
                AGC_MULTISAMPLE_STATE_INIT;
            native_multisample.rasterization_samples =
                (uint32_t)multisample->rasterizationSamples;
            native_multisample.sample_shading_enable =
                multisample->sampleShadingEnable;
            native_multisample.minimum_sample_shading =
                multisample->minSampleShading;
            native_multisample.alpha_to_coverage_enable =
                multisample->alphaToCoverageEnable;
            native_multisample.alpha_to_one_enable =
                multisample->alphaToOneEnable;
            native_desc.multisample = &native_multisample;
            native_desc.dynamic_state_mask |=
                AGC_DYNAMIC_STATE_VIEWPORT_BIT |
                AGC_DYNAMIC_STATE_SCISSOR_BIT;
            if (raster->depthBiasEnable && dynamic_depth_bias)
                native_desc.dynamic_state_mask |=
                    AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT;
            if (dynamic_line_width)
                native_desc.dynamic_state_mask |=
                    AGC_DYNAMIC_STATE_LINE_WIDTH_BIT;
            if (dynamic_blend_constants)
                native_desc.dynamic_state_mask |=
                    AGC_DYNAMIC_STATE_BLEND_CONSTANTS_BIT;
            if (dynamic_stencil_reference && native_depth.stencil_test_enable)
                native_desc.dynamic_state_mask |=
                    AGC_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;

            int32_t native_result = native_layout_valid ?
                agcCreateGraphicsPipeline(vk_ps5_native_device(device),
                    &native_desc, &pipeline->native_graphics_pipeline) :
                AGC_ERROR_VALIDATION_FAILED;
            if (native_result != AGC_OK) {
                AgcDebugMessage debug_message = AGC_DEBUG_MESSAGE_INIT;
                const int32_t debug_result = agcGetLastDebugMessage(
                    vk_ps5_native_device(device), &debug_message);
                fprintf(stderr,
                    "vulkan-ps5: native graphics pipeline creation failed: "
                    "0x%08x%s%s\n", (unsigned)native_result,
                    debug_result == AGC_OK ? ": " : "",
                    debug_result == AGC_OK ? debug_message.message : "");
                free_pipeline(device, pAllocator, pipeline);
                return native_result == AGC_ERROR_OUT_OF_MEMORY ?
                    VK_ERROR_OUT_OF_DEVICE_MEMORY :
                    VK_ERROR_INITIALIZATION_FAILED;
            }
        }
        pPipelines[i] = (VkPipeline)pipeline;
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                  const VkAllocationCallbacks *pAllocator) {
    free_pipeline(device, pAllocator, (VkPs5Pipeline *)pipeline);
}

#define DEFINE_SIMPLE_CREATE(name, InfoType, HandleType) \
VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL \
name(VkDevice device, const InfoType *pCreateInfo, \
     const VkAllocationCallbacks *pAllocator, HandleType *pObject) { \
    if (!device || !pCreateInfo || !pObject) return VK_ERROR_INITIALIZATION_FAILED; \
    VkPs5Opaque *object = alloc_object(device, pAllocator, sizeof(*object), \
                                        _Alignof(VkPs5Opaque)); \
    if (!object) return VK_ERROR_OUT_OF_HOST_MEMORY; \
    *pObject = (HandleType)object; \
    return VK_SUCCESS; \
}

#define DEFINE_SIMPLE_DESTROY(name, HandleType) \
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL \
name(VkDevice device, HandleType object, const VkAllocationCallbacks *pAllocator) { \
    if (object) vk_ps5_device_free(device, pAllocator, (void *)object); \
}

static AgcAddressMode native_sampler_address(VkSamplerAddressMode mode)
{
    switch (mode) {
    case VK_SAMPLER_ADDRESS_MODE_REPEAT: return AGC_ADDRESS_MODE_REPEAT;
    case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
        return AGC_ADDRESS_MODE_MIRRORED_REPEAT;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
        return AGC_ADDRESS_MODE_CLAMP_TO_EDGE;
    case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
        return AGC_ADDRESS_MODE_CLAMP_TO_BORDER;
    default: return AGC_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }
}

static VkResult create_native_sampler(VkDevice device,
    const VkSamplerCreateInfo *info, VkPs5Sampler *sampler)
{
    AgcSamplerDesc desc = AGC_SAMPLER_DESC_INIT;
    desc.min_filter = info->minFilter == VK_FILTER_LINEAR ?
        AGC_FILTER_LINEAR : AGC_FILTER_NEAREST;
    desc.mag_filter = info->magFilter == VK_FILTER_LINEAR ?
        AGC_FILTER_LINEAR : AGC_FILTER_NEAREST;
    desc.address_u = native_sampler_address(info->addressModeU);
    desc.address_v = native_sampler_address(info->addressModeV);
    desc.address_w = native_sampler_address(info->addressModeW);
    desc.mip_filter = info->maxLod == 0.0f ? AGC_MIP_FILTER_NONE :
        info->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST ?
        AGC_MIP_FILTER_NEAREST : AGC_MIP_FILTER_LINEAR;
    desc.anisotropy_enable = info->anisotropyEnable;
    desc.max_anisotropy = info->anisotropyEnable ?
        (uint32_t)info->maxAnisotropy : 1u;
    desc.compare_enable = info->compareEnable;
    desc.compare_operation = info->compareOp;
    desc.min_lod = info->minLod;
    desc.max_lod = info->maxLod;
    desc.lod_bias = info->mipLodBias;
    switch (info->borderColor) {
    case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
    case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
        desc.border_color = AGC_SAMPLER_BORDER_OPAQUE_BLACK; break;
    case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
    case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
        desc.border_color = AGC_SAMPLER_BORDER_OPAQUE_WHITE; break;
    case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
    case VK_BORDER_COLOR_INT_CUSTOM_EXT:
        desc.border_color = AGC_SAMPLER_BORDER_CUSTOM;
        memcpy(desc.custom_border_color, sampler->custom_border_color_value,
            sizeof(desc.custom_border_color));
        break;
    default:
        desc.border_color = AGC_SAMPLER_BORDER_TRANSPARENT_BLACK; break;
    }
    int32_t result = agcCreateSampler(vk_ps5_native_device(device), &desc,
        &sampler->native_sampler);
    return result == AGC_OK ? VK_SUCCESS :
        result == AGC_ERROR_OUT_OF_MEMORY ? VK_ERROR_OUT_OF_DEVICE_MEMORY :
        VK_ERROR_INITIALIZATION_FAILED;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
    if (!device || !pCreateInfo || !pSampler ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    const VkSamplerCustomBorderColorCreateInfoEXT *custom_border = NULL;
    for (const VkBaseInStructure *next =
             (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext) {
        if (next->sType ==
                VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT &&
            !custom_border) {
            custom_border =
                (const VkSamplerCustomBorderColorCreateInfoEXT *)next;
        } else {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
    }
    if (pCreateInfo->flags ||
        pCreateInfo->unnormalizedCoordinates ||
        pCreateInfo->minLod < 0.0f || pCreateInfo->maxLod < pCreateInfo->minLod)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (pCreateInfo->anisotropyEnable &&
        (!(pCreateInfo->maxAnisotropy >= 1.0f) ||
         pCreateInfo->maxAnisotropy > 16.0f))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if ((pCreateInfo->minFilter != VK_FILTER_NEAREST &&
         pCreateInfo->minFilter != VK_FILTER_LINEAR) ||
        (pCreateInfo->magFilter != VK_FILTER_NEAREST &&
         pCreateInfo->magFilter != VK_FILTER_LINEAR) ||
        (pCreateInfo->mipmapMode != VK_SAMPLER_MIPMAP_MODE_NEAREST &&
         pCreateInfo->mipmapMode != VK_SAMPLER_MIPMAP_MODE_LINEAR))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPs5Sampler *sampler = alloc_object(device, pAllocator, sizeof(*sampler),
                                         _Alignof(VkPs5Sampler));
    if (!sampler) return VK_ERROR_OUT_OF_HOST_MEMORY;
    switch (pCreateInfo->borderColor) {
    case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
    case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
    case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
    case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
    case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
    case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
        break;
    case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
    case VK_BORDER_COLOR_INT_CUSTOM_EXT: {
        if (!custom_border || custom_border->pNext) {
            vk_ps5_device_free(device, pAllocator, sampler);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        memcpy(sampler->custom_border_color_value,
            custom_border->customBorderColor.uint32,
            sizeof(sampler->custom_border_color_value));
        break;
    }
    default:
        vk_ps5_device_free(device, pAllocator, sampler);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    VkResult native_result = create_native_sampler(device, pCreateInfo, sampler);
    if (native_result != VK_SUCCESS) {
        vk_ps5_device_free(device, pAllocator, sampler);
        return native_result;
    }
    *pSampler = (VkSampler)sampler;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySampler(VkDevice device, VkSampler sampler,
                 const VkAllocationCallbacks *pAllocator)
{
    VkPs5Sampler *object = (VkPs5Sampler *)sampler;
    if (object && object->native_sampler)
        vk_ps5_destroy_or_defer_native(device, VK_PS5_NATIVE_SAMPLER,
            object->native_sampler);
    if (object) vk_ps5_device_free(device, pAllocator, object);
}
static const VkDescriptorSetLayoutBinding *descriptor_layout_binding(
    const VkPs5DescriptorSetLayout *layout, uint32_t binding)
{
    if (!layout) return NULL;
    for (uint32_t i = 0; i < layout->binding_count; ++i)
        if (layout->bindings[i].binding == binding)
            return &layout->bindings[i];
    return NULL;
}

static VkBool32 descriptor_template_type_supported(VkDescriptorType type)
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return VK_TRUE;
    default:
        return VK_FALSE;
    }
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorUpdateTemplate(
    VkDevice device, const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
    if (!device || !pCreateInfo || !pDescriptorUpdateTemplate ||
        pCreateInfo->sType !=
            VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO ||
        pCreateInfo->pNext || pCreateInfo->flags ||
        pCreateInfo->templateType !=
            VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET ||
        !pCreateInfo->descriptorSetLayout ||
        (pCreateInfo->descriptorUpdateEntryCount &&
         !pCreateInfo->pDescriptorUpdateEntries))
        return VK_ERROR_INITIALIZATION_FAILED;

    const VkPs5DescriptorSetLayout *layout =
        (const VkPs5DescriptorSetLayout *)pCreateInfo->descriptorSetLayout;
    for (uint32_t i = 0; i < pCreateInfo->descriptorUpdateEntryCount; ++i) {
        const VkDescriptorUpdateTemplateEntry *entry =
            &pCreateInfo->pDescriptorUpdateEntries[i];
        const VkDescriptorSetLayoutBinding *binding =
            descriptor_layout_binding(layout, entry->dstBinding);
        if (!entry->descriptorCount || !binding ||
            binding->descriptorType != entry->descriptorType ||
            !descriptor_template_type_supported(entry->descriptorType) ||
            entry->dstArrayElement > binding->descriptorCount ||
            entry->descriptorCount >
                binding->descriptorCount - entry->dstArrayElement)
            return VK_ERROR_INITIALIZATION_FAILED;
    }

    const size_t size = sizeof(VkPs5DescriptorUpdateTemplate) +
        (size_t)pCreateInfo->descriptorUpdateEntryCount *
            sizeof(VkDescriptorUpdateTemplateEntry);
    VkPs5DescriptorUpdateTemplate *update_template = alloc_object(
        device, pAllocator, size, _Alignof(VkPs5DescriptorUpdateTemplate));
    if (!update_template) return VK_ERROR_OUT_OF_HOST_MEMORY;
    update_template->type = pCreateInfo->templateType;
    update_template->set_layout = pCreateInfo->descriptorSetLayout;
    update_template->entry_count = pCreateInfo->descriptorUpdateEntryCount;
    if (update_template->entry_count)
        memcpy(update_template->entries, pCreateInfo->pDescriptorUpdateEntries,
               (size_t)update_template->entry_count *
                   sizeof(update_template->entries[0]));
    *pDescriptorUpdateTemplate = (VkDescriptorUpdateTemplate)update_template;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorUpdateTemplate(
    VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const VkAllocationCallbacks *pAllocator)
{
    if (descriptorUpdateTemplate)
        vk_ps5_device_free(device, pAllocator,
                           (void *)descriptorUpdateTemplate);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkFramebuffer *pFramebuffer) {
    if (!device || !pCreateInfo || !pFramebuffer ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO ||
        !pCreateInfo->renderPass || !pCreateInfo->width ||
        !pCreateInfo->height || pCreateInfo->layers != 1u ||
        pCreateInfo->attachmentCount > VK_PS5_MAX_RENDER_ATTACHMENTS ||
        (pCreateInfo->attachmentCount && !pCreateInfo->pAttachments))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5RenderPass *render_pass =
        (VkPs5RenderPass *)pCreateInfo->renderPass;
    if (pCreateInfo->attachmentCount != render_pass->attachment_count)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5Framebuffer *framebuffer = alloc_object(
        device, pAllocator, sizeof(*framebuffer), _Alignof(VkPs5Framebuffer));
    if (!framebuffer) return VK_ERROR_OUT_OF_HOST_MEMORY;
    framebuffer->render_pass = render_pass;
    framebuffer->attachment_count = pCreateInfo->attachmentCount;
    framebuffer->width = pCreateInfo->width;
    framebuffer->height = pCreateInfo->height;
    framebuffer->layers = pCreateInfo->layers;
    for (uint32_t i = 0; i < framebuffer->attachment_count; ++i) {
        VkPs5ImageView *view = (VkPs5ImageView *)pCreateInfo->pAttachments[i];
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        if (!view || !image || view->format != render_pass->attachments[i].format ||
            image->samples != render_pass->attachments[i].samples ||
            image->extent.width < framebuffer->width ||
            image->extent.height < framebuffer->height) {
            vk_ps5_device_free(device, pAllocator, framebuffer);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        framebuffer->attachments[i] = view;
    }
    *pFramebuffer = (VkFramebuffer)framebuffer;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                     const VkAllocationCallbacks *pAllocator) {
    if (framebuffer)
        vk_ps5_device_free(device, pAllocator, (void *)framebuffer);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorSetLayout(VkDevice device,
                            const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkDescriptorSetLayout *pSetLayout) {
    if (!device || !pCreateInfo || !pSetLayout ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO ||
        (pCreateInfo->bindingCount && !pCreateInfo->pBindings))
        return VK_ERROR_INITIALIZATION_FAILED;
    size_t size = sizeof(VkPs5DescriptorSetLayout) +
        (size_t)pCreateInfo->bindingCount * sizeof(VkDescriptorSetLayoutBinding);
    VkPs5DescriptorSetLayout *layout = alloc_object(device, pAllocator, size,
                                                     _Alignof(VkPs5DescriptorSetLayout));
    if (!layout) return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->binding_count = pCreateInfo->bindingCount;
    if (layout->binding_count)
        memcpy(layout->bindings, pCreateInfo->pBindings,
               (size_t)layout->binding_count * sizeof(layout->bindings[0]));
    *pSetLayout = (VkDescriptorSetLayout)layout;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout setLayout,
                             const VkAllocationCallbacks *pAllocator) {
    if (setLayout) vk_ps5_device_free(device, pAllocator, (void *)setLayout);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkPipelineLayout *pPipelineLayout) {
    if (!device || !pCreateInfo || !pPipelineLayout ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO ||
        pCreateInfo->setLayoutCount > OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
        (pCreateInfo->setLayoutCount && !pCreateInfo->pSetLayouts) ||
        (pCreateInfo->pushConstantRangeCount && !pCreateInfo->pPushConstantRanges))
        return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t binding_count = 0;
    for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; ++set) {
        const VkPs5DescriptorSetLayout *layout =
            (const VkPs5DescriptorSetLayout *)pCreateInfo->pSetLayouts[set];
        if (!layout || layout->binding_count >
            OPENAGC_PSBC_MAX_DESCRIPTOR_BINDINGS - binding_count)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        binding_count += layout->binding_count;
    }
    size_t size = sizeof(VkPs5PipelineLayout) +
        (size_t)binding_count * sizeof(OpenAgcPsbcDescriptorBinding);
    VkPs5PipelineLayout *pipeline = alloc_object(device, pAllocator, size,
                                                  _Alignof(VkPs5PipelineLayout));
    if (!pipeline) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pipeline->binding_count = binding_count;
    uint32_t index = 0;
    for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; ++set) {
        const VkPs5DescriptorSetLayout *layout =
            (const VkPs5DescriptorSetLayout *)pCreateInfo->pSetLayouts[set];
        for (uint32_t j = 0; j < layout->binding_count; ++j, ++index) {
            const VkDescriptorSetLayoutBinding *source = &layout->bindings[j];
            OpenAgcPsbcDescriptorBinding *dest = &pipeline->bindings[index];
            if (!source->descriptorCount ||
                !psbc_descriptor_type(source->descriptorType, &dest->type)) {
                vk_ps5_device_free(device, pAllocator, pipeline);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            dest->set = set;
            dest->binding = source->binding;
            dest->array_size = source->descriptorCount;
        }
    }
    for (uint32_t i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
        const VkPushConstantRange *range = &pCreateInfo->pPushConstantRanges[i];
        if (range->offset > UINT32_MAX - range->size) {
            vk_ps5_device_free(device, pAllocator, pipeline);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        uint32_t end = range->offset + range->size;
        if (end > pipeline->push_constant_size) pipeline->push_constant_size = end;
    }
    if (pipeline->push_constant_size > 256u) {
        vk_ps5_device_free(device, pAllocator, pipeline);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    *pPipelineLayout = (VkPipelineLayout)pipeline;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks *pAllocator) {
    if (pipelineLayout) vk_ps5_device_free(device, pAllocator, (void *)pipelineLayout);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
    if (!device || !pCreateInfo || !pRenderPass ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (const VkBaseInStructure *next = (const VkBaseInStructure *)pCreateInfo->pNext;
         next; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO) {
            const VkRenderPassMultiviewCreateInfo *multiview =
                (const VkRenderPassMultiviewCreateInfo *)next;
            for (uint32_t i = 0; i < multiview->subpassCount; ++i)
                if (multiview->pViewMasks[i] != 0) return VK_ERROR_FEATURE_NOT_PRESENT;
        }
    }
    if (!pCreateInfo->subpassCount || !pCreateInfo->pSubpasses ||
        pCreateInfo->subpassCount > VK_PS5_MAX_SUBPASSES ||
        pCreateInfo->attachmentCount > VK_PS5_MAX_RENDER_ATTACHMENTS ||
        (pCreateInfo->attachmentCount && !pCreateInfo->pAttachments))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5RenderPass *render_pass = alloc_object(device, pAllocator,
                                                 sizeof(*render_pass),
                                                 _Alignof(VkPs5RenderPass));
    if (!render_pass) return VK_ERROR_OUT_OF_HOST_MEMORY;
    render_pass->attachment_count = pCreateInfo->attachmentCount;
    render_pass->subpass_count = pCreateInfo->subpassCount;
    if (render_pass->attachment_count)
        memcpy(render_pass->attachments, pCreateInfo->pAttachments,
               (size_t)render_pass->attachment_count *
                   sizeof(render_pass->attachments[0]));
    for (uint32_t i = 0; i < render_pass->subpass_count; ++i) {
        const VkSubpassDescription *source = &pCreateInfo->pSubpasses[i];
        render_pass->subpasses[i].depth_stencil_attachment =
            VK_ATTACHMENT_UNUSED;
        render_pass->subpasses[i].samples = VK_SAMPLE_COUNT_1_BIT;
        if (source->pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS ||
            source->colorAttachmentCount > AGC_GFX1013_MAX_COLOR_TARGETS ||
            (source->colorAttachmentCount && !source->pColorAttachments)) {
            vk_ps5_device_free(device, pAllocator, render_pass);
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        render_pass->subpasses[i].color_attachment_count =
            source->colorAttachmentCount;
        for (uint32_t j = 0; j < source->colorAttachmentCount; ++j) {
            uint32_t attachment = source->pColorAttachments[j].attachment;
            if (attachment != VK_ATTACHMENT_UNUSED &&
                attachment >= render_pass->attachment_count) {
                vk_ps5_device_free(device, pAllocator, render_pass);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            if (attachment != VK_ATTACHMENT_UNUSED) {
                AgcGfx1013ColorTargetFormat target_format;
                if (source->pColorAttachments[j].layout !=
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
                    (render_pass->attachments[attachment].samples !=
                         VK_SAMPLE_COUNT_1_BIT &&
                     render_pass->attachments[attachment].samples !=
                         VK_SAMPLE_COUNT_4_BIT) ||
                    !color_target_format(
                        render_pass->attachments[attachment].format,
                        &target_format)) {
                    vk_ps5_device_free(device, pAllocator, render_pass);
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
                if (render_pass->subpasses[i].samples != VK_SAMPLE_COUNT_1_BIT &&
                    render_pass->subpasses[i].samples !=
                        render_pass->attachments[attachment].samples) {
                    vk_ps5_device_free(device, pAllocator, render_pass);
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
                render_pass->subpasses[i].samples =
                    render_pass->attachments[attachment].samples;
            }
            render_pass->subpasses[i].color_attachments[j] = attachment;
        }
        if (source->pDepthStencilAttachment &&
            source->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED) {
            uint32_t attachment =
                source->pDepthStencilAttachment->attachment;
            AgcGfx1013DepthSurfaceFormat depth_format;
            if (attachment >= render_pass->attachment_count) {
                vk_ps5_device_free(device, pAllocator, render_pass);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            VkImageLayout layout = source->pDepthStencilAttachment->layout;
            if ((layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
                 layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) ||
                render_pass->attachments[attachment].samples !=
                    VK_SAMPLE_COUNT_1_BIT ||
                !depth_surface_format(
                    render_pass->attachments[attachment].format,
                    &depth_format)) {
                vk_ps5_device_free(device, pAllocator, render_pass);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            render_pass->subpasses[i].depth_stencil_attachment = attachment;
            render_pass->subpasses[i].depth_stencil_layout = layout;
            if (render_pass->subpasses[i].samples != VK_SAMPLE_COUNT_1_BIT) {
                vk_ps5_device_free(device, pAllocator, render_pass);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
        }
    }
    *pRenderPass = (VkRenderPass)render_pass;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass2(VkDevice device, const VkRenderPassCreateInfo2 *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkRenderPass *pRenderPass)
{
    if (!device || !pCreateInfo || !pRenderPass ||
        pCreateInfo->sType != VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2 ||
        pCreateInfo->pNext || pCreateInfo->correlatedViewMaskCount ||
        !pCreateInfo->subpassCount || !pCreateInfo->pSubpasses ||
        (pCreateInfo->attachmentCount && !pCreateInfo->pAttachments) ||
        (pCreateInfo->dependencyCount && !pCreateInfo->pDependencies) ||
        pCreateInfo->attachmentCount > VK_PS5_MAX_RENDER_ATTACHMENTS ||
        pCreateInfo->subpassCount > VK_PS5_MAX_SUBPASSES)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkAttachmentDescription attachments[VK_PS5_MAX_RENDER_ATTACHMENTS];
    VkSubpassDescription subpasses[VK_PS5_MAX_SUBPASSES];
    VkAttachmentReference colors[VK_PS5_MAX_SUBPASSES]
                                [AGC_GFX1013_MAX_COLOR_TARGETS];
    VkAttachmentReference depths[VK_PS5_MAX_SUBPASSES];
    memset(attachments, 0, sizeof(attachments));
    memset(subpasses, 0, sizeof(subpasses));
    memset(colors, 0, sizeof(colors));
    memset(depths, 0, sizeof(depths));

    for (uint32_t i = 0; i < pCreateInfo->attachmentCount; ++i) {
        const VkAttachmentDescription2 *source = &pCreateInfo->pAttachments[i];
        if (source->sType != VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2 ||
            source->pNext)
            return VK_ERROR_INITIALIZATION_FAILED;
        attachments[i] = (VkAttachmentDescription){
            .flags = source->flags,
            .format = source->format,
            .samples = source->samples,
            .loadOp = source->loadOp,
            .storeOp = source->storeOp,
            .stencilLoadOp = source->stencilLoadOp,
            .stencilStoreOp = source->stencilStoreOp,
            .initialLayout = source->initialLayout,
            .finalLayout = source->finalLayout,
        };
    }
    for (uint32_t i = 0; i < pCreateInfo->subpassCount; ++i) {
        const VkSubpassDescription2 *source = &pCreateInfo->pSubpasses[i];
        if (source->sType != VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2 ||
            source->pNext || source->viewMask || source->inputAttachmentCount ||
            source->pResolveAttachments || source->preserveAttachmentCount ||
            source->colorAttachmentCount > AGC_GFX1013_MAX_COLOR_TARGETS ||
            (source->colorAttachmentCount && !source->pColorAttachments))
            return VK_ERROR_FEATURE_NOT_PRESENT;
        for (uint32_t j = 0; j < source->colorAttachmentCount; ++j) {
            const VkAttachmentReference2 *reference =
                &source->pColorAttachments[j];
            if (reference->sType !=
                    VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2 ||
                reference->pNext)
                return VK_ERROR_INITIALIZATION_FAILED;
            colors[i][j] = (VkAttachmentReference){
                .attachment = reference->attachment,
                .layout = reference->layout,
            };
        }
        const VkAttachmentReference *depth = NULL;
        if (source->pDepthStencilAttachment) {
            const VkAttachmentReference2 *reference =
                source->pDepthStencilAttachment;
            if (reference->sType !=
                    VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2 ||
                reference->pNext)
                return VK_ERROR_INITIALIZATION_FAILED;
            depths[i] = (VkAttachmentReference){
                .attachment = reference->attachment,
                .layout = reference->layout,
            };
            depth = &depths[i];
        }
        subpasses[i] = (VkSubpassDescription){
            .flags = source->flags,
            .pipelineBindPoint = source->pipelineBindPoint,
            .colorAttachmentCount = source->colorAttachmentCount,
            .pColorAttachments = colors[i],
            .pDepthStencilAttachment = depth,
        };
    }
    for (uint32_t i = 0; i < pCreateInfo->dependencyCount; ++i) {
        const VkSubpassDependency2 *dependency = &pCreateInfo->pDependencies[i];
        if (dependency->sType != VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2 ||
            dependency->pNext || dependency->viewOffset)
            return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    const VkRenderPassCreateInfo legacy = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .flags = pCreateInfo->flags,
        .attachmentCount = pCreateInfo->attachmentCount,
        .pAttachments = attachments,
        .subpassCount = pCreateInfo->subpassCount,
        .pSubpasses = subpasses,
    };
    return vkCreateRenderPass(device, &legacy, pAllocator, pRenderPass);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                    const VkAllocationCallbacks *pAllocator) {
    if (renderPass) vk_ps5_device_free(device, pAllocator, (void *)renderPass);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkDescriptorPool *pDescriptorPool) {
    if (!device || !pCreateInfo || !pDescriptorPool || !pCreateInfo->maxSets)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5DescriptorPool *pool = alloc_object(device, pAllocator, sizeof(*pool),
                                              _Alignof(VkPs5DescriptorPool));
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->device = device;
    pool->max_sets = pCreateInfo->maxSets;
    *pDescriptorPool = (VkDescriptorPool)pool;
    return VK_SUCCESS;
}

static void clear_descriptor_pool(VkPs5DescriptorPool *pool) {
    VkPs5DescriptorSet *set = pool->sets;
    while (set) {
        VkPs5DescriptorSet *next = set->next;
        vk_ps5_device_free(pool->device, NULL, set);
        set = next;
    }
    pool->sets = NULL;
    pool->allocated_sets = 0;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                        const VkAllocationCallbacks *pAllocator) {
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)descriptorPool;
    if (!pool) return;
    clear_descriptor_pool(pool);
    vk_ps5_device_free(device, pAllocator, pool);
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                      VkDescriptorPoolResetFlags flags) {
    (void)device; (void)flags;
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)descriptorPool;
    if (!pool) return VK_ERROR_INITIALIZATION_FAILED;
    clear_descriptor_pool(pool);
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateDescriptorSets(VkDevice device,
                         const VkDescriptorSetAllocateInfo *pAllocateInfo,
                         VkDescriptorSet *pDescriptorSets) {
    if (!device || !pAllocateInfo || !pDescriptorSets ||
        pAllocateInfo->sType != VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO ||
        (pAllocateInfo->descriptorSetCount && !pAllocateInfo->pSetLayouts))
        return VK_ERROR_INITIALIZATION_FAILED;
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)pAllocateInfo->descriptorPool;
    if (!pool || pAllocateInfo->descriptorSetCount >
        pool->max_sets - pool->allocated_sets) return VK_ERROR_OUT_OF_POOL_MEMORY;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i)
        pDescriptorSets[i] = VK_NULL_HANDLE;
    VkPs5DescriptorSet *old_head = pool->sets;
    uint32_t old_count = pool->allocated_sets;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        VkPs5DescriptorSetLayout *layout = (VkPs5DescriptorSetLayout *)
            pAllocateInfo->pSetLayouts[i];
        if (!layout) goto invalid_layout;
        uint32_t descriptor_count = 0;
        for (uint32_t binding = 0; binding < layout->binding_count; ++binding) {
            if (layout->bindings[binding].descriptorCount >
                UINT32_MAX - descriptor_count)
                goto invalid_layout;
            descriptor_count += layout->bindings[binding].descriptorCount;
        }
        size_t size = sizeof(VkPs5DescriptorSet) +
            (size_t)descriptor_count * sizeof(VkPs5DescriptorValue);
        VkPs5DescriptorSet *set = alloc_object(device, NULL, size,
                                               _Alignof(VkPs5DescriptorSet));
        if (!set) {
            while (pool->sets != old_head) {
                VkPs5DescriptorSet *rollback = pool->sets;
                pool->sets = rollback->next;
                vk_ps5_device_free(device, NULL, rollback);
            }
            pool->allocated_sets = old_count;
            for (uint32_t j = 0; j < pAllocateInfo->descriptorSetCount; ++j)
                pDescriptorSets[j] = VK_NULL_HANDLE;
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        set->layout = layout;
        set->descriptor_count = descriptor_count;
        set->pool = pool;
        set->next = pool->sets;
        pool->sets = set;
        pool->allocated_sets++;
        pDescriptorSets[i] = (VkDescriptorSet)set;
    }
    return VK_SUCCESS;

invalid_layout:
    while (pool->sets != old_head) {
        VkPs5DescriptorSet *rollback = pool->sets;
        pool->sets = rollback->next;
        vk_ps5_device_free(device, NULL, rollback);
    }
    pool->allocated_sets = old_count;
    for (uint32_t j = 0; j < pAllocateInfo->descriptorSetCount; ++j)
        pDescriptorSets[j] = VK_NULL_HANDLE;
    return VK_ERROR_INITIALIZATION_FAILED;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                     uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    VkPs5DescriptorPool *pool = (VkPs5DescriptorPool *)descriptorPool;
    if (!pool || (descriptorSetCount && !pDescriptorSets))
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        VkPs5DescriptorSet **link = &pool->sets;
        while (*link && *link != (VkPs5DescriptorSet *)pDescriptorSets[i])
            link = &(*link)->next;
        if (!*link) return VK_ERROR_INITIALIZATION_FAILED;
        VkPs5DescriptorSet *set = *link;
        *link = set->next;
        pool->allocated_sets--;
        vk_ps5_device_free(device, NULL, set);
    }
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                       const VkWriteDescriptorSet *pDescriptorWrites,
                       uint32_t descriptorCopyCount,
                       const VkCopyDescriptorSet *pDescriptorCopies) {
    (void)device;
    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet *write = &pDescriptorWrites[i];
        VkPs5DescriptorSet *set = (VkPs5DescriptorSet *)write->dstSet;
        if (!set) continue;
        uint32_t first = 0;
        const VkDescriptorSetLayoutBinding *layout_binding = NULL;
        for (uint32_t j = 0; j < set->layout->binding_count; ++j) {
            const VkDescriptorSetLayoutBinding *candidate =
                &set->layout->bindings[j];
            if (candidate->binding == write->dstBinding) {
                layout_binding = candidate;
                break;
            }
            first += candidate->descriptorCount;
        }
        if (!layout_binding || layout_binding->descriptorType !=
                write->descriptorType ||
            write->dstArrayElement > layout_binding->descriptorCount ||
            write->descriptorCount > layout_binding->descriptorCount -
                write->dstArrayElement)
            continue;
        switch (write->descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (!write->pBufferInfo) continue;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            if (!write->pImageInfo) continue;
            break;
        default:
            continue;
        }
        first += write->dstArrayElement;
        for (uint32_t j = 0; j < write->descriptorCount; ++j) {
            set->values[first + j].type = write->descriptorType;
            if (write->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                write->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                write->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                write->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                set->values[first + j].buffer = write->pBufferInfo[j];
            else
                set->values[first + j].image = write->pImageInfo[j];
            set->values[first + j].valid = VK_TRUE;
        }
    }
    for (uint32_t i = 0; i < descriptorCopyCount; ++i) {
        const VkCopyDescriptorSet *copy = &pDescriptorCopies[i];
        VkPs5DescriptorSet *source = (VkPs5DescriptorSet *)copy->srcSet;
        VkPs5DescriptorSet *dest = (VkPs5DescriptorSet *)copy->dstSet;
        uint32_t source_first = 0, dest_first = 0;
        const VkDescriptorSetLayoutBinding *source_binding = NULL;
        const VkDescriptorSetLayoutBinding *dest_binding = NULL;
        if (!source || !dest) continue;
        for (uint32_t j = 0; j < source->layout->binding_count; ++j) {
            if (source->layout->bindings[j].binding == copy->srcBinding) {
                source_binding = &source->layout->bindings[j];
                break;
            }
            source_first += source->layout->bindings[j].descriptorCount;
        }
        for (uint32_t j = 0; j < dest->layout->binding_count; ++j) {
            if (dest->layout->bindings[j].binding == copy->dstBinding) {
                dest_binding = &dest->layout->bindings[j];
                break;
            }
            dest_first += dest->layout->bindings[j].descriptorCount;
        }
        if (!source_binding || !dest_binding ||
            source_binding->descriptorType != dest_binding->descriptorType ||
            copy->srcArrayElement > source_binding->descriptorCount ||
            copy->descriptorCount > source_binding->descriptorCount -
                copy->srcArrayElement ||
            copy->dstArrayElement > dest_binding->descriptorCount ||
            copy->descriptorCount > dest_binding->descriptorCount -
                copy->dstArrayElement)
            continue;
        source_first += copy->srcArrayElement;
        dest_first += copy->dstArrayElement;
        memmove(&dest->values[dest_first], &source->values[source_first],
            (size_t)copy->descriptorCount * sizeof(VkPs5DescriptorValue));
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet,
                                  VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                  const void *pData) {
    VkPs5DescriptorSet *set = (VkPs5DescriptorSet *)descriptorSet;
    const VkPs5DescriptorUpdateTemplate *update_template =
        (const VkPs5DescriptorUpdateTemplate *)descriptorUpdateTemplate;
    if (!device || !set || !update_template || !pData ||
        update_template->type !=
            VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET ||
        update_template->set_layout != (VkDescriptorSetLayout)set->layout)
        return;
    for (uint32_t i = 0; i < update_template->entry_count; ++i) {
        const VkDescriptorUpdateTemplateEntry *entry =
            &update_template->entries[i];
        for (uint32_t j = 0; j < entry->descriptorCount; ++j) {
            const uint8_t *data = (const uint8_t *)pData + entry->offset +
                (size_t)j * entry->stride;
            VkWriteDescriptorSet write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSet,
                .dstBinding = entry->dstBinding,
                .dstArrayElement = entry->dstArrayElement + j,
                .descriptorCount = 1,
                .descriptorType = entry->descriptorType,
            };
            switch (entry->descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                write.pBufferInfo = (const VkDescriptorBufferInfo *)data;
                break;
            default:
                write.pImageInfo = (const VkDescriptorImageInfo *)data;
                break;
            }
            vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
        }
    }
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDescriptorSetLayoutSupport(VkDevice device,
                                const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                VkDescriptorSetLayoutSupport *pSupport) {
    (void)device;
    if (pSupport) pSupport->supported = pCreateInfo && pCreateInfo->bindingCount <= 1024;
}

VK_PS5_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSamplerYcbcrConversion(VkDevice device,
                               const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSamplerYcbcrConversion *pYcbcrConversion) {
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (pYcbcrConversion) *pYcbcrConversion = VK_NULL_HANDLE;
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySamplerYcbcrConversion(VkDevice device,
                                VkSamplerYcbcrConversion ycbcrConversion,
                                const VkAllocationCallbacks *pAllocator) {
    (void)device; (void)ycbcrConversion; (void)pAllocator;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass,
                           VkExtent2D *pGranularity) {
    (void)device; (void)renderPass;
    if (pGranularity) *pGranularity = (VkExtent2D){1, 1};
}

#define IGNORE(x) (void)(x)

static AgcResourceOwner native_owner_for_usage(AgcResourceUsage usage)
{
    return usage == kAgcResourceUsageUndefined ||
        usage == kAgcResourceUsageHostRead ||
        usage == kAgcResourceUsageHostWrite ?
        kAgcResourceOwnerHost : kAgcResourceOwnerGraphics;
}

static VkResult native_command_result(int32_t result)
{
    if (result == AGC_OK)
        return VK_SUCCESS;
    if (result == AGC_ERROR_OUT_OF_MEMORY ||
        result == AGC_ERROR_COMMAND_SPACE_EXHAUSTED)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (result == AGC_ERROR_NOT_SUPPORTED)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    return VK_ERROR_INITIALIZATION_FAILED;
}

static bool native_usage_from_access(VkAccessFlags access,
                                     VkImageLayout layout,
                                     AgcResourceUsage *usage)
{
    const VkAccessFlags known = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
        VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
        VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
        VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT |
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    if (!usage || (access & ~known) != 0u)
        return false;
    if (layout == VK_IMAGE_LAYOUT_UNDEFINED ||
        layout == VK_IMAGE_LAYOUT_PREINITIALIZED) {
        *usage = kAgcResourceUsageUndefined;
        return true;
    }
    if (layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        *usage = kAgcResourceUsageVideoOutScanout;
        return true;
    }
    if (layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ||
        (access & VK_ACCESS_TRANSFER_READ_BIT) != 0u) {
        if ((access & VK_ACCESS_TRANSFER_WRITE_BIT) != 0u)
            return false;
        *usage = kAgcResourceUsageCopySource;
        return true;
    }
    if (layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ||
        (access & VK_ACCESS_TRANSFER_WRITE_BIT) != 0u) {
        *usage = kAgcResourceUsageCopyDestination;
        return true;
    }
    if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
        (access & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0u) {
        *usage = kAgcResourceUsageColorTarget;
        return true;
    }
    if (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
        (access & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) != 0u) {
        *usage = kAgcResourceUsageDepthStencilWrite;
        return true;
    }
    if (layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
        (access & VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT) != 0u) {
        *usage = kAgcResourceUsageDepthStencilRead;
        return true;
    }
    if ((access & VK_ACCESS_HOST_WRITE_BIT) != 0u) {
        *usage = kAgcResourceUsageHostWrite;
        return true;
    }
    if ((access & VK_ACCESS_HOST_READ_BIT) != 0u) {
        *usage = kAgcResourceUsageHostRead;
        return true;
    }
    if ((access & (VK_ACCESS_SHADER_WRITE_BIT |
                   VK_ACCESS_MEMORY_WRITE_BIT)) != 0u) {
        *usage = kAgcResourceUsageShaderWrite;
        return true;
    }
    if ((access & (VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                   VK_ACCESS_INDEX_READ_BIT |
                   VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                   VK_ACCESS_UNIFORM_READ_BIT |
                   VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                   VK_ACCESS_SHADER_READ_BIT |
                   VK_ACCESS_MEMORY_READ_BIT)) != 0u ||
        layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        *usage = kAgcResourceUsageShaderRead;
        return true;
    }
    if (access == 0u) {
        *usage = kAgcResourceUsageUndefined;
        return true;
    }
    return false;
}

static AgcResourceUsage native_buffer_recorded_usage(
    const VkPs5CommandBuffer *command, const VkPs5Buffer *buffer)
{
    for (uint32_t index = command->native_buffer_state_count;
         index > 0u; --index)
        if (command->native_buffer_states[index - 1u].buffer == buffer)
            return command->native_buffer_states[index - 1u].usage;
    return buffer ? buffer->native_usage : kAgcResourceUsageUndefined;
}

static bool native_record_buffer_usage(VkPs5CommandBuffer *command,
                                       VkPs5Buffer *buffer,
                                       AgcResourceUsage usage)
{
    for (uint32_t index = 0u; index < command->native_buffer_state_count;
         ++index) {
        if (command->native_buffer_states[index].buffer == buffer) {
            command->native_buffer_states[index].usage = usage;
            return true;
        }
    }
    if (command->native_buffer_state_count >=
        VK_PS5_MAX_NATIVE_RESOURCE_STATES)
        return false;
    command->native_buffer_states[command->native_buffer_state_count].buffer =
        buffer;
    command->native_buffer_states[command->native_buffer_state_count].usage =
        usage;
    command->native_buffer_state_count++;
    return true;
}

static VkResult native_prepare_buffer_range(VkPs5CommandBuffer *command,
                                            VkPs5Buffer *buffer,
                                            uint64_t offset, uint64_t size,
                                            AgcResourceUsage after)
{
    AgcResourceStateInfo state = AGC_RESOURCE_STATE_INFO_INIT;
    if (!command || !buffer || !buffer->native_buffer || !size)
        return VK_ERROR_INITIALIZATION_FAILED;
    int32_t result = agcGetCommandBufferRangeStateInfo(
        command->native_graphics_command_buffer, buffer->native_buffer,
        offset, size, &state);
    if (result != AGC_OK)
        return native_command_result(result);
    if (state.usage != after ||
        state.owner != native_owner_for_usage(after)) {
        AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
        transition.before = state.usage;
        transition.after = after;
        transition.before_owner = state.owner;
        transition.after_owner = native_owner_for_usage(after);
        transition.buffer = buffer->native_buffer;
        transition.buffer_offset = offset;
        transition.buffer_size = size;
        result = agcCmdTransitionResources(
            command->native_graphics_command_buffer, 1u, &transition);
        if (result != AGC_OK) {
            fprintf(stderr,
                "vulkan-ps5: prepared buffer transition failed "
                "result=0x%08x offset=%llu size=%llu buffer_size=%llu "
                "before=%u/%u after=%u/%u\n",
                (unsigned)result, (unsigned long long)offset,
                (unsigned long long)size,
                (unsigned long long)buffer->size,
                transition.before, transition.before_owner,
                transition.after, transition.after_owner);
            return native_command_result(result);
        }
    }
    if (!native_record_buffer_usage(command, buffer,
            offset == 0u && size == buffer->size ? after :
                kAgcResourceUsageUndefined))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    return VK_SUCCESS;
}

static AgcResourceUsage native_image_recorded_usage(
    const VkPs5CommandBuffer *command, const VkPs5Image *image)
{
    for (uint32_t index = command->native_image_state_count;
         index > 0u; --index)
        if (command->native_image_states[index - 1u].image == image)
            return command->native_image_states[index - 1u].usage;
    return image ? image->native_usage : kAgcResourceUsageUndefined;
}

static void native_commit_resource_states(VkPs5CommandBuffer *command)
{
    for (uint32_t index = 0u;
         index < command->native_buffer_state_count; ++index)
        command->native_buffer_states[index].buffer->native_usage =
            command->native_buffer_states[index].usage;
    for (uint32_t index = 0u;
         index < command->native_image_state_count; ++index)
        command->native_image_states[index].image->native_usage =
            command->native_image_states[index].usage;
}

static bool native_record_image_usage(VkPs5CommandBuffer *command,
                                      VkPs5Image *image,
                                      AgcResourceUsage usage)
{
    for (uint32_t index = 0u; index < command->native_image_state_count;
         ++index) {
        if (command->native_image_states[index].image == image) {
            command->native_image_states[index].usage = usage;
            return true;
        }
    }
    if (command->native_image_state_count >=
        VK_PS5_MAX_NATIVE_RESOURCE_STATES)
        return false;
    command->native_image_states[command->native_image_state_count].image =
        image;
    command->native_image_states[command->native_image_state_count].usage =
        usage;
    command->native_image_state_count++;
    return true;
}

static bool native_queue_family_barrier(uint32_t source, uint32_t destination)
{
    return (source == VK_QUEUE_FAMILY_IGNORED &&
            destination == VK_QUEUE_FAMILY_IGNORED) ||
        (source == 0u && destination == 0u);
}

static bool native_image_range(const VkPs5Image *image,
                               const VkImageSubresourceRange *source,
                               AgcImageSubresourceRange *destination)
{
    uint32_t mip_count;
    uint32_t layer_count;
    AgcImageAspectFlags aspects = 0u;
    const VkImageAspectFlags known = VK_IMAGE_ASPECT_COLOR_BIT |
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (!image || !source || !destination || !source->aspectMask ||
        (source->aspectMask & ~known) != 0u ||
        source->baseMipLevel >= image->mip_levels ||
        source->baseArrayLayer >= image->array_layers)
        return false;
    mip_count = source->levelCount == VK_REMAINING_MIP_LEVELS ?
        image->mip_levels - source->baseMipLevel : source->levelCount;
    layer_count = source->layerCount == VK_REMAINING_ARRAY_LAYERS ?
        image->array_layers - source->baseArrayLayer : source->layerCount;
    if (!mip_count || mip_count > image->mip_levels - source->baseMipLevel ||
        !layer_count ||
        layer_count > image->array_layers - source->baseArrayLayer)
        return false;
    if (source->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
        aspects |= AGC_IMAGE_ASPECT_COLOR_BIT;
    if (source->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
        aspects |= AGC_IMAGE_ASPECT_DEPTH_BIT;
    if (source->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
        aspects |= AGC_IMAGE_ASPECT_STENCIL_BIT;
    if ((image->is_depth_surface &&
         (aspects & AGC_IMAGE_ASPECT_COLOR_BIT) != 0u) ||
        (!image->is_depth_surface &&
         aspects != AGC_IMAGE_ASPECT_COLOR_BIT))
        return false;
    *destination = (AgcImageSubresourceRange){
        aspects, source->baseMipLevel, mip_count,
        source->baseArrayLayer, layer_count, 0u
    };
    return true;
}

static bool native_image_range_is_whole(
    const VkPs5Image *image, const AgcImageSubresourceRange *range)
{
    AgcImageAspectFlags aspects = AGC_IMAGE_ASPECT_COLOR_BIT;
    if (image->is_depth_surface) {
        aspects = image->format == VK_FORMAT_S8_UINT ?
            AGC_IMAGE_ASPECT_STENCIL_BIT : AGC_IMAGE_ASPECT_DEPTH_BIT;
        if (image->format == VK_FORMAT_D16_UNORM_S8_UINT ||
            image->format == VK_FORMAT_D32_SFLOAT_S8_UINT)
            aspects |= AGC_IMAGE_ASPECT_STENCIL_BIT;
    }
    return range->aspect_mask == aspects && range->base_mip_level == 0u &&
        range->mip_level_count == image->mip_levels &&
        range->base_array_layer == 0u &&
        range->array_layer_count == image->array_layers;
}

static bool native_usage_from_layout(VkImageLayout layout,
                                     AgcResourceUsage *usage)
{
    if (!usage)
        return false;
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        *usage = kAgcResourceUsageUndefined;
        return true;
    case VK_IMAGE_LAYOUT_GENERAL:
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        *usage = kAgcResourceUsageShaderRead;
        return true;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        *usage = kAgcResourceUsageColorTarget;
        return true;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        *usage = kAgcResourceUsageDepthStencilWrite;
        return true;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        *usage = kAgcResourceUsageDepthStencilRead;
        return true;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        *usage = kAgcResourceUsageCopySource;
        return true;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        *usage = kAgcResourceUsageCopyDestination;
        return true;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        *usage = kAgcResourceUsageVideoOutScanout;
        return true;
    default:
        return false;
    }
}

static bool native_whole_image_range(const VkPs5Image *image,
                                     AgcImageSubresourceRange *range)
{
    VkImageSubresourceRange source = {
        .aspectMask = image->is_depth_surface ?
            (image->format == VK_FORMAT_S8_UINT ?
                VK_IMAGE_ASPECT_STENCIL_BIT :
                VK_IMAGE_ASPECT_DEPTH_BIT |
                    ((image->format == VK_FORMAT_D16_UNORM_S8_UINT ||
                      image->format == VK_FORMAT_D32_SFLOAT_S8_UINT) ?
                         VK_IMAGE_ASPECT_STENCIL_BIT : 0u)) :
            VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = image->mip_levels,
        .baseArrayLayer = 0u,
        .layerCount = image->array_layers,
    };
    return native_image_range(image, &source, range);
}

static VkResult native_transition_whole_image(
    VkPs5CommandBuffer *command, VkPs5Image *image,
    AgcResourceUsage before, AgcResourceUsage after)
{
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    if (!command || !image || !image->native_image ||
        !native_whole_image_range(image, &transition.image_range))
        return VK_ERROR_INITIALIZATION_FAILED;
    transition.resource_type = kAgcResourceTypeImage;
    transition.before = before;
    transition.after = after;
    transition.before_owner = native_owner_for_usage(before);
    transition.after_owner = native_owner_for_usage(after);
    transition.image = image->native_image;
    int32_t result = agcCmdTransitionResources(
        command->native_graphics_command_buffer, 1u, &transition);
    if (result != AGC_OK) {
        fprintf(stderr,
            "vulkan-ps5: whole image transition failed result=0x%08x "
            "before=%u/%u after=%u/%u aspect=0x%x mip=%u+%u "
            "layer=%u+%u format=%u\n",
            (unsigned)result, transition.before, transition.before_owner,
            transition.after, transition.after_owner,
            transition.image_range.aspect_mask,
            transition.image_range.base_mip_level,
            transition.image_range.mip_level_count,
            transition.image_range.base_array_layer,
            transition.image_range.array_layer_count, image->format);
        return native_command_result(result);
    }
    if (!native_record_image_usage(command, image, after))
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    return VK_SUCCESS;
}

static float native_srgb_encode(float value)
{
    if (value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value <= 0.0031308f ? value * 12.92f :
        1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
}

static uint32_t native_unorm8(float value)
{
    if (value <= 0.0f)
        return 0u;
    if (value >= 1.0f)
        return 255u;
    return (uint32_t)(value * 255.0f + 0.5f);
}

static uint32_t native_round_shift_even(uint32_t value, uint32_t shift)
{
    if (!shift)
        return value;
    if (shift >= 32u)
        return 0u;
    const uint32_t result = value >> shift;
    const uint32_t remainder = value & ((1u << shift) - 1u);
    const uint32_t halfway = 1u << (shift - 1u);
    return result + (remainder > halfway ||
        (remainder == halfway && (result & 1u)));
}

static uint16_t native_float16(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    const uint16_t sign = (uint16_t)((bits >> 16u) & 0x8000u);
    const uint32_t source_exponent = (bits >> 23u) & 0xffu;
    const uint32_t source_mantissa = bits & 0x7fffffu;
    if (source_exponent == 0xffu) {
        uint16_t mantissa = (uint16_t)(source_mantissa >> 13u);
        if (source_mantissa && !mantissa)
            mantissa = 1u;
        return (uint16_t)(sign | 0x7c00u | mantissa);
    }
    if (!source_exponent)
        return sign;

    int32_t exponent = (int32_t)source_exponent - 127 + 15;
    if (exponent >= 31)
        return (uint16_t)(sign | 0x7c00u);
    if (exponent <= 0) {
        if (exponent < -10)
            return sign;
        const uint32_t mantissa = native_round_shift_even(
            source_mantissa | 0x800000u, (uint32_t)(14 - exponent));
        return (uint16_t)(sign | mantissa);
    }

    uint32_t mantissa = native_round_shift_even(source_mantissa, 13u);
    if (mantissa == 0x400u) {
        mantissa = 0u;
        ++exponent;
        if (exponent >= 31)
            return (uint16_t)(sign | 0x7c00u);
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10u) | mantissa);
}

static uint32_t native_unsigned_float(float value, uint32_t mantissa_bits)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    const uint32_t source_exponent = (bits >> 23u) & 0xffu;
    const uint32_t source_mantissa = bits & 0x7fffffu;
    const uint32_t exponent_mask = 0x1fu << mantissa_bits;
    if (source_exponent == 0xffu)
        return exponent_mask | (source_mantissa ?
            (1u << (mantissa_bits - 1u)) : 0u);
    if ((bits & 0x80000000u) != 0u || !source_exponent)
        return 0u;

    int32_t exponent = (int32_t)source_exponent - 127 + 15;
    if (exponent >= 31)
        return exponent_mask;
    if (exponent <= 0) {
        if (exponent < -(int32_t)mantissa_bits)
            return 0u;
        return native_round_shift_even(source_mantissa | 0x800000u,
            (uint32_t)(24 - (int32_t)mantissa_bits - exponent));
    }

    uint32_t mantissa = native_round_shift_even(source_mantissa,
        23u - mantissa_bits);
    if (mantissa == (1u << mantissa_bits)) {
        mantissa = 0u;
        ++exponent;
        if (exponent >= 31)
            return exponent_mask;
    }
    return ((uint32_t)exponent << mantissa_bits) | mantissa;
}

static uint32_t native_unorm(float value, uint32_t maximum)
{
    if (value <= 0.0f)
        return 0u;
    if (value >= 1.0f)
        return maximum;
    return (uint32_t)(value * (float)maximum + 0.5f);
}

static bool native_pack_rgba8_clear(VkFormat format,
                                    const VkClearColorValue *clear,
                                    uint32_t *value_out)
{
    if (!clear || !value_out || !native_image_is_rgba8_clearable(format))
        return false;
    float red = clear->float32[0];
    float green = clear->float32[1];
    float blue = clear->float32[2];
    const bool srgb = format == VK_FORMAT_R8G8B8A8_SRGB ||
        format == VK_FORMAT_A8B8G8R8_SRGB_PACK32 ||
        format == VK_FORMAT_B8G8R8A8_SRGB;
    if (srgb) {
        red = native_srgb_encode(red);
        green = native_srgb_encode(green);
        blue = native_srgb_encode(blue);
    }
    const uint32_t r = native_unorm8(red);
    const uint32_t g = native_unorm8(green);
    const uint32_t b = native_unorm8(blue);
    const uint32_t a = native_unorm8(clear->float32[3]);
    *value_out = format == VK_FORMAT_B8G8R8A8_UNORM ||
        format == VK_FORMAT_B8G8R8A8_SRGB ?
        b | (g << 8u) | (r << 16u) | (a << 24u) :
        r | (g << 8u) | (b << 16u) | (a << 24u);
    return true;
}

VkBool32 vk_ps5_pack_clear_color(VkFormat format,
                                 const VkClearColorValue *clear,
                                 uint32_t pattern[4],
                                 uint32_t *pattern_word_count)
{
    if (!clear || !pattern || !pattern_word_count)
        return VK_FALSE;
    memset(pattern, 0, 4u * sizeof(*pattern));
    *pattern_word_count = 0u;
    uint32_t packed;
    switch (format) {
    case VK_FORMAT_R8_UNORM: {
        const uint32_t red = native_unorm8(clear->float32[0]);
        pattern[0] = red * 0x01010101u;
        break;
    }
    case VK_FORMAT_R8G8_UNORM: {
        const uint32_t red = native_unorm8(clear->float32[0]);
        const uint32_t green = native_unorm8(clear->float32[1]);
        pattern[0] = red | (green << 8u) |
            (red << 16u) | (green << 24u);
        break;
    }
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        if (!native_pack_rgba8_clear(format, clear, &pattern[0]))
            return VK_FALSE;
        break;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        pattern[0] = native_unorm(clear->float32[0], 1023u) |
            (native_unorm(clear->float32[1], 1023u) << 10u) |
            (native_unorm(clear->float32[2], 1023u) << 20u) |
            (native_unorm(clear->float32[3], 3u) << 30u);
        break;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        pattern[0] = native_unsigned_float(clear->float32[0], 6u) |
            (native_unsigned_float(clear->float32[1], 6u) << 11u) |
            (native_unsigned_float(clear->float32[2], 5u) << 22u);
        break;
    case VK_FORMAT_R16_SFLOAT:
        packed = native_float16(clear->float32[0]);
        pattern[0] = packed | (packed << 16u);
        break;
    case VK_FORMAT_R16G16_SFLOAT:
        pattern[0] = native_float16(clear->float32[0]) |
            ((uint32_t)native_float16(clear->float32[1]) << 16u);
        break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        pattern[0] = native_float16(clear->float32[0]) |
            ((uint32_t)native_float16(clear->float32[1]) << 16u);
        pattern[1] = native_float16(clear->float32[2]) |
            ((uint32_t)native_float16(clear->float32[3]) << 16u);
        *pattern_word_count = 2u;
        return VK_TRUE;
    case VK_FORMAT_R32_SFLOAT:
        memcpy(&pattern[0], &clear->float32[0], sizeof(uint32_t));
        break;
    case VK_FORMAT_R32G32_SFLOAT:
        memcpy(&pattern[0], &clear->float32[0], 2u * sizeof(uint32_t));
        *pattern_word_count = 2u;
        return VK_TRUE;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        memcpy(pattern, clear->float32, 4u * sizeof(uint32_t));
        *pattern_word_count = 4u;
        return VK_TRUE;
    default:
        return VK_FALSE;
    }
    *pattern_word_count = 1u;
    return VK_TRUE;
}

static VkResult native_clear_color_attachment(
    VkPs5CommandBuffer *command, VkPs5ImageView *view,
    const VkRect2D *render_area, const VkClearColorValue *clear)
{
    VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
    if (!command || !view || !image || !render_area || !clear ||
        !image->native_clear_buffer || image->samples != VK_SAMPLE_COUNT_1_BIT ||
        view->base_array_layer != 0u ||
        view->layer_count != image->array_layers ||
        render_area->offset.x != 0 || render_area->offset.y != 0 ||
        render_area->extent.width != image->extent.width ||
        render_area->extent.height != image->extent.height ||
        (view->format != VK_FORMAT_R8G8B8A8_UNORM &&
         view->format != VK_FORMAT_R8G8B8A8_SRGB &&
         view->format != VK_FORMAT_A8B8G8R8_UNORM_PACK32 &&
         view->format != VK_FORMAT_A8B8G8R8_SRGB_PACK32 &&
         view->format != VK_FORMAT_B8G8R8A8_UNORM &&
         view->format != VK_FORMAT_B8G8R8A8_SRGB))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!native_require_complete_stream(command))
        return command->record_error;

    uint32_t value;
    if (!native_pack_rgba8_clear(view->format, clear, &value))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (image->array_layers > UINT64_MAX / image->array_pitch)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const uint64_t clear_size = image->array_pitch * image->array_layers;
    if (!clear_size || clear_size > image->size)
        return VK_ERROR_INITIALIZATION_FAILED;

    AgcResourceStateInfo state = AGC_RESOURCE_STATE_INFO_INIT;
    int32_t result = agcGetCommandBufferRangeStateInfo(
        command->native_graphics_command_buffer, image->native_clear_buffer,
        0u, clear_size, &state);
    if (result != AGC_OK)
        return native_command_result(result);
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    transition.before = state.usage;
    transition.after = kAgcResourceUsageCopyDestination;
    transition.before_owner = state.owner;
    transition.after_owner = kAgcResourceOwnerGraphics;
    transition.buffer = image->native_clear_buffer;
    transition.buffer_size = clear_size;
    result = agcCmdTransitionResources(
        command->native_graphics_command_buffer, 1u, &transition);
    if (result == AGC_OK)
        result = agcCmdFillBuffer(command->native_graphics_command_buffer,
            image->native_clear_buffer, 0u, clear_size, value);
    if (result == AGC_OK) {
        transition.before = kAgcResourceUsageCopyDestination;
        transition.after = kAgcResourceUsageCopySource;
        transition.before_owner = kAgcResourceOwnerGraphics;
        transition.after_owner = kAgcResourceOwnerGraphics;
        result = agcCmdTransitionResources(
            command->native_graphics_command_buffer, 1u, &transition);
    }
    if (result != AGC_OK)
        return native_command_result(result);
    return native_transition_whole_image(command, image,
        native_image_recorded_usage(command, image),
        kAgcResourceUsageColorTarget);
}

static VkResult native_clear_buffer_pattern(
    VkPs5CommandBuffer *command, AgcBuffer buffer, uint64_t offset,
    uint64_t size, uint64_t buffer_size, const uint32_t pattern[4],
    uint32_t pattern_word_count, uint32_t layer_count,
    uint64_t layer_stride, bool *compute_bound)
{
    if (!command || !buffer || !size || (offset & 3u) != 0u ||
        (size & 3u) != 0u || !buffer_size || size > buffer_size ||
        offset > buffer_size - size || !pattern || !pattern_word_count ||
        pattern_word_count > 4u || !layer_count || !layer_stride ||
        (layer_stride & 3u) != 0u || !compute_bound)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (buffer_size / sizeof(uint32_t) > UINT32_MAX ||
        offset / sizeof(uint32_t) > UINT32_MAX ||
        layer_stride / sizeof(uint32_t) > UINT32_MAX)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    VkPs5Pipeline *pipeline = (VkPs5Pipeline *)
        vk_ps5_device_meta_clear_pipeline(command->device);
    if (!pipeline || !pipeline->native_compute_pipeline)
        return VK_ERROR_INITIALIZATION_FAILED;
    int32_t result = AGC_OK;
    if (!*compute_bound) {
        result = agcCmdBindComputePipeline(
            command->native_graphics_command_buffer,
            pipeline->native_compute_pipeline);
        if (result == AGC_OK) {
            command->native_bound_compute = pipeline->native_compute_pipeline;
            AgcDescriptorWrite write = AGC_DESCRIPTOR_WRITE_INIT;
            write.type = AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER;
            write.buffer = buffer;
            write.buffer_range = buffer_size;
            result = agcCmdBindDescriptors(
                command->native_graphics_command_buffer, 1u, &write);
            if (result == AGC_OK)
                command->native_descriptor_bind_count++;
        }
        if (result == AGC_OK)
            *compute_bound = true;
    }

    const uint64_t maximum_chunk_words = 65535ull * 64ull * 4ull;
    uint64_t remaining_words = size / sizeof(uint32_t);
    uint64_t word_offset = 0u;
    while (result == AGC_OK && remaining_words) {
        const uint32_t chunk_words = remaining_words >
            maximum_chunk_words ? (uint32_t)maximum_chunk_words :
            (uint32_t)remaining_words;
        if (offset / sizeof(uint32_t) + word_offset > UINT32_MAX)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        uint32_t parameters[8] = {
            pattern[0], pattern[1], pattern[2], pattern[3],
            chunk_words, pattern_word_count,
            (uint32_t)(offset / sizeof(uint32_t) + word_offset),
            (uint32_t)(layer_stride / sizeof(uint32_t))
        };
        result = agcCmdPushConstants(
            command->native_graphics_command_buffer,
            1u << kAgcShaderStageCs, 0u, sizeof(parameters), parameters);
        const uint32_t group_count =
            (chunk_words + 64u * 4u - 1u) / (64u * 4u);
        if (result == AGC_OK)
            result = agcCmdDispatch(command->native_graphics_command_buffer,
                group_count, layer_count, 1u);
        if (result == AGC_OK)
            command->native_dispatch_count++;
        word_offset += chunk_words;
        remaining_words -= chunk_words;
    }
    return result == AGC_OK ? VK_SUCCESS : native_command_result(result);
}

static VkResult native_clear_layer_layout(VkDevice device,
    const VkPs5Image *image, const AgcImageSubresourceRange *range,
    uint32_t mip_index, AgcImageSubresourceLayout *base_out,
    uint64_t *layer_stride_out)
{
    AgcImageSubresourceLayout base = AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    uint64_t layer_stride = 0u;
    if (!device || !image || !range || !base_out || !layer_stride_out ||
        mip_index >= range->mip_level_count)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t layer = 0u; layer < range->array_layer_count; ++layer) {
        AgcImageSubresourceLayout current =
            AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        int32_t result = agcGetImageSubresourceLayout(
            vk_ps5_native_device(device), &image->native_desc,
            range->base_mip_level + mip_index,
            range->base_array_layer + layer, 0u, &current);
        if (result != AGC_OK || !current.size ||
            (current.offset & 3u) != 0u || (current.size & 3u) != 0u ||
            current.offset > image->size ||
            current.size > image->size - current.offset)
            return result == AGC_OK ? VK_ERROR_INITIALIZATION_FAILED :
                native_command_result(result);
        if (!layer) {
            base = current;
        } else {
            if (current.size != base.size || current.offset <= base.offset)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            const uint64_t current_stride =
                (current.offset - base.offset) / layer;
            if (!current_stride ||
                base.offset + (uint64_t)layer * current_stride !=
                    current.offset ||
                (layer_stride && current_stride != layer_stride))
                return VK_ERROR_FEATURE_NOT_PRESENT;
            layer_stride = current_stride;
        }
    }
    if (!layer_stride)
        layer_stride = base.size;
    *base_out = base;
    *layer_stride_out = layer_stride;
    return VK_SUCCESS;
}

static VkResult native_clear_color_image(
    VkPs5CommandBuffer *command, VkPs5Image *image, VkImageLayout layout,
    const VkClearColorValue *clear, uint32_t range_count,
    const VkImageSubresourceRange *ranges)
{
    uint32_t pattern[4];
    uint32_t pattern_word_count;
    if (!command || !image || !image->native_image ||
        !image->native_clear_buffer || !clear || !range_count || !ranges ||
        image->samples != VK_SAMPLE_COUNT_1_BIT || image->is_depth_surface ||
        (image->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u ||
        (layout != VK_IMAGE_LAYOUT_GENERAL &&
         layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        !vk_ps5_pack_clear_color(image->format, clear, pattern,
            &pattern_word_count))
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!native_require_complete_stream(command))
        return command->record_error;

    if ((image->size & 3u) != 0u)
        return VK_ERROR_INITIALIZATION_FAILED;
    /* Preflight every selected layout before recording a transition. */
    for (uint32_t range_index = 0u; range_index < range_count; ++range_index) {
        AgcImageSubresourceRange native_range;
        if (!native_image_range(image, &ranges[range_index], &native_range) ||
            native_range.aspect_mask != AGC_IMAGE_ASPECT_COLOR_BIT)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        for (uint32_t mip = 0u; mip < native_range.mip_level_count; ++mip) {
            AgcImageSubresourceLayout base;
            uint64_t layer_stride;
            VkResult result = native_clear_layer_layout(command->device,
                image, &native_range, mip, &base, &layer_stride);
            if (result != VK_SUCCESS)
                return result;
        }
    }

    AgcResourceStateInfo state = AGC_RESOURCE_STATE_INFO_INIT;
    int32_t native_result = agcGetCommandBufferRangeStateInfo(
        command->native_graphics_command_buffer, image->native_clear_buffer,
        0u, image->size, &state);
    if (native_result != AGC_OK)
        return native_command_result(native_result);
    AgcResourceTransition compute_transition =
        AGC_RESOURCE_TRANSITION_INIT;
    compute_transition.before = state.usage;
    compute_transition.after = kAgcResourceUsageShaderWrite;
    compute_transition.before_owner = state.owner;
    compute_transition.after_owner = kAgcResourceOwnerGraphics;
    compute_transition.buffer = image->native_clear_buffer;
    compute_transition.buffer_size = image->size;
    native_result = agcCmdTransitionResources(
        command->native_graphics_command_buffer, 1u, &compute_transition);
    if (native_result != AGC_OK)
        return native_command_result(native_result);

    bool compute_bound = false;
    for (uint32_t range_index = 0u; range_index < range_count; ++range_index) {
        AgcImageSubresourceRange native_range;
        if (!native_image_range(image, &ranges[range_index], &native_range))
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t mip = 0u; mip < native_range.mip_level_count; ++mip) {
            AgcImageSubresourceLayout base;
            uint64_t layer_stride;
            VkResult result = native_clear_layer_layout(command->device,
                image, &native_range, mip, &base, &layer_stride);
            if (result != VK_SUCCESS)
                return result;
            result = native_clear_buffer_pattern(command,
                image->native_clear_buffer, base.offset, base.size,
                image->size, pattern, pattern_word_count,
                native_range.array_layer_count, layer_stride,
                &compute_bound);
            if (result != VK_SUCCESS)
                return result;
        }
    }
    compute_transition.before = kAgcResourceUsageShaderWrite;
    compute_transition.after = kAgcResourceUsageCopySource;
    compute_transition.before_owner = kAgcResourceOwnerGraphics;
    native_result = agcCmdTransitionResources(
        command->native_graphics_command_buffer, 1u, &compute_transition);
    command->native_bound_compute = NULL;
    command->native_bound_graphics = NULL;
    if (native_result != AGC_OK)
        return native_command_result(native_result);
    VkResult transition_result = native_transition_whole_image(command, image,
        native_image_recorded_usage(command, image),
        kAgcResourceUsageCopyDestination);
    if (transition_result != VK_SUCCESS)
        return transition_result;
    if (command->bound_graphics) {
        vkCmdBindPipeline((VkCommandBuffer)command,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            (VkPipeline)command->bound_graphics);
        if (command->record_error != VK_SUCCESS)
            return command->record_error;
    }
    return VK_SUCCESS;
}

static bool native_image_supports_usage(const VkPs5Image *image,
                                        AgcResourceUsage usage)
{
    if (!image)
        return false;
    switch (usage) {
    case kAgcResourceUsageUndefined:
        return true;
    case kAgcResourceUsageCopySource:
    case kAgcResourceUsageHostRead:
        return (image->native_desc.usage &
            AGC_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    case kAgcResourceUsageCopyDestination:
    case kAgcResourceUsageHostWrite:
        return (image->native_desc.usage &
            AGC_IMAGE_USAGE_TRANSFER_DST_BIT) != 0u;
    case kAgcResourceUsageShaderRead:
        return (image->native_desc.usage & (AGC_IMAGE_USAGE_SAMPLED_BIT |
            AGC_IMAGE_USAGE_STORAGE_BIT)) != 0u;
    case kAgcResourceUsageShaderWrite:
        return (image->native_desc.usage & AGC_IMAGE_USAGE_STORAGE_BIT) != 0u;
    case kAgcResourceUsageColorTarget:
        return (image->native_desc.usage &
            AGC_IMAGE_USAGE_COLOR_TARGET_BIT) != 0u;
    case kAgcResourceUsageDepthStencilRead:
    case kAgcResourceUsageDepthStencilWrite:
        return (image->native_desc.usage &
            AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) != 0u;
    case kAgcResourceUsageVideoOutScanout:
        return (image->native_desc.usage & AGC_IMAGE_USAGE_SCANOUT_BIT) != 0u;
    default:
        return false;
    }
}

static bool native_image_copy_layers(
    const VkPs5Image *image, const VkImageSubresourceLayers *source,
    AgcImageSubresourceLayers *destination)
{
    if (!image || !source || !destination || image->is_depth_surface ||
        source->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
        source->mipLevel >= image->mip_levels || !source->layerCount ||
        source->baseArrayLayer >= image->array_layers ||
        source->layerCount > image->array_layers - source->baseArrayLayer)
        return false;
    *destination = (AgcImageSubresourceLayers){
        AGC_IMAGE_ASPECT_COLOR_BIT, source->mipLevel,
        source->baseArrayLayer, source->layerCount
    };
    return true;
}

static void reject_unsupported_command(VkCommandBuffer c)
{
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (command && command->state == VK_PS5_COMMAND_RECORDING &&
        command->record_error == VK_SUCCESS)
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyBuffer(VkCommandBuffer c, VkBuffer s, VkBuffer d, uint32_t n,
                const VkBufferCopy *r) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *source = (VkPs5Buffer *)s;
    VkPs5Buffer *destination = (VkPs5Buffer *)d;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (command->active_render_pass || !source || !destination || !n || !r ||
        !source->memory || !destination->memory ||
        !(source->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !(destination->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    for (uint32_t region = 0u; region < n; ++region) {
        const VkBufferCopy *copy = &r[region];
        if (!copy->size || ((copy->srcOffset | copy->dstOffset | copy->size) &
                3u) != 0u || copy->srcOffset > source->size ||
            copy->size > source->size - copy->srcOffset ||
            copy->dstOffset > destination->size ||
            copy->size > destination->size - copy->dstOffset) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }

    if (source->memory == destination->memory)
    for (uint32_t source_region = 0u; source_region < n; ++source_region) {
        uint64_t source_start = source->memory_offset +
            r[source_region].srcOffset;
        uint64_t source_end = source_start + r[source_region].size;
        for (uint32_t destination_region = 0u;
             destination_region < n; ++destination_region) {
            uint64_t destination_start = destination->memory_offset +
                r[destination_region].dstOffset;
            uint64_t destination_end =
                destination_start + r[destination_region].size;
            if (source_start < destination_end &&
                destination_start < source_end) {
                command->record_error = VK_ERROR_INITIALIZATION_FAILED;
                return;
            }
        }
    }
    if (!native_require_complete_stream(command))
        return;
    for (uint32_t region = 0u; region < n; ++region) {
        VkResult prepare = native_prepare_buffer_range(command, source,
            r[region].srcOffset, r[region].size,
            kAgcResourceUsageCopySource);
        if (prepare == VK_SUCCESS)
            prepare = native_prepare_buffer_range(command, destination,
                r[region].dstOffset, r[region].size,
                kAgcResourceUsageCopyDestination);
        if (prepare != VK_SUCCESS) {
            command->record_error = prepare;
            return;
        }
        int32_t result = agcCmdCopyBuffer(
            command->native_graphics_command_buffer,
            source->native_buffer, r[region].srcOffset,
            destination->native_buffer, r[region].dstOffset,
            r[region].size);
        if (result != AGC_OK) {
            command->record_error = native_command_result(result);
            return;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
               VkImageLayout dl, uint32_t n, const VkImageCopy *r) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Image *source = (VkPs5Image *)s;
    VkPs5Image *destination = (VkPs5Image *)d;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (command->active_render_pass || !source || !destination || !r ||
        !source->memory || !destination->memory || !source->native_image ||
        !destination->native_image || !n ||
        !(source->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !(destination->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        (sl != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
         sl != VK_IMAGE_LAYOUT_GENERAL) ||
        (dl != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
         dl != VK_IMAGE_LAYOUT_GENERAL)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (source == destination || source->format != destination->format ||
        (source->memory == destination->memory &&
         source->memory_offset < destination->memory_offset +
             destination->size &&
         destination->memory_offset < source->memory_offset +
             source->size)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t region = 0u; region < n; ++region) {
        AgcImageSubresourceLayers source_layers;
        AgcImageSubresourceLayers destination_layers;
        if (!native_image_copy_layers(source, &r[region].srcSubresource,
                &source_layers) ||
            !native_image_copy_layers(destination,
                &r[region].dstSubresource, &destination_layers) ||
            source_layers.array_layer_count !=
                destination_layers.array_layer_count ||
            !r[region].extent.width || !r[region].extent.height ||
            !r[region].extent.depth) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }
    if (!native_require_complete_stream(command))
        return;

    VkResult prepare = native_transition_whole_image(command, source,
        native_image_recorded_usage(command, source),
        kAgcResourceUsageCopySource);
    if (prepare == VK_SUCCESS)
        prepare = native_transition_whole_image(command, destination,
            native_image_recorded_usage(command, destination),
            kAgcResourceUsageCopyDestination);
    if (prepare != VK_SUCCESS) {
        command->record_error = prepare;
        return;
    }
    for (uint32_t region = 0u; region < n; ++region) {
        AgcImageCopyRegion copy = AGC_IMAGE_COPY_REGION_INIT;
        (void)native_image_copy_layers(source, &r[region].srcSubresource,
            &copy.source_subresource);
        (void)native_image_copy_layers(destination,
            &r[region].dstSubresource, &copy.destination_subresource);
        copy.source_offset = (AgcOffset3D){r[region].srcOffset.x,
            r[region].srcOffset.y, r[region].srcOffset.z};
        copy.destination_offset = (AgcOffset3D){r[region].dstOffset.x,
            r[region].dstOffset.y, r[region].dstOffset.z};
        copy.extent = (AgcExtent3D){r[region].extent.width,
            r[region].extent.height, r[region].extent.depth};
        int32_t result = agcCmdCopyImageRegions(
            command->native_graphics_command_buffer, source->native_image,
            destination->native_image, 1u, &copy);
        if (result != AGC_OK) {
            command->record_error = native_command_result(result);
            return;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyBufferToImage(VkCommandBuffer c, VkBuffer s, VkImage d, VkImageLayout dl,
                       uint32_t n, const VkBufferImageCopy *r) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *source = (VkPs5Buffer *)s;
    VkPs5Image *destination = (VkPs5Image *)d;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (command->active_render_pass || !source || !destination || !n || !r ||
        !source->memory || !source->native_buffer || !destination->memory ||
        !destination->native_image ||
        !(source->usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        !(destination->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        (dl != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
         dl != VK_IMAGE_LAYOUT_GENERAL)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t region = 0u; region < n; ++region) {
        AgcImageSubresourceLayers layers;
        if (!native_image_copy_layers(destination,
                &r[region].imageSubresource, &layers) ||
            !r[region].imageExtent.width || !r[region].imageExtent.height ||
            !r[region].imageExtent.depth) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }
    if (!native_require_complete_stream(command))
        return;
    VkResult prepare = native_prepare_buffer_range(command, source, 0u,
        source->size, kAgcResourceUsageCopySource);
    if (prepare == VK_SUCCESS)
        prepare = native_transition_whole_image(command, destination,
            native_image_recorded_usage(command, destination),
            kAgcResourceUsageCopyDestination);
    if (prepare != VK_SUCCESS) {
        command->record_error = prepare;
        return;
    }
    for (uint32_t region = 0u; region < n; ++region) {
        AgcBufferImageCopyRegion copy = AGC_BUFFER_IMAGE_COPY_REGION_INIT;
        copy.buffer_offset = r[region].bufferOffset;
        copy.buffer_row_length = r[region].bufferRowLength;
        copy.buffer_image_height = r[region].bufferImageHeight;
        (void)native_image_copy_layers(destination,
            &r[region].imageSubresource, &copy.image_subresource);
        copy.image_offset = (AgcOffset3D){r[region].imageOffset.x,
            r[region].imageOffset.y, r[region].imageOffset.z};
        copy.image_extent = (AgcExtent3D){r[region].imageExtent.width,
            r[region].imageExtent.height, r[region].imageExtent.depth};
        int32_t result = agcCmdCopyBufferToImage(
            command->native_graphics_command_buffer, source->native_buffer,
            destination->native_image, 1u, &copy);
        if (result != AGC_OK) {
            command->record_error = native_command_result(result);
            return;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImageToBuffer(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkBuffer d,
                       uint32_t n, const VkBufferImageCopy *r) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdCopyImageToBuffer");
    VkPs5Image *source = (VkPs5Image *)s;
    VkPs5Buffer *destination = (VkPs5Buffer *)d;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (command->active_render_pass || !source || !destination || !n || !r ||
        !source->memory || !source->native_image || !destination->memory ||
        !destination->native_buffer ||
        !(source->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !(destination->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
        (sl != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
         sl != VK_IMAGE_LAYOUT_GENERAL)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t region = 0u; region < n; ++region) {
        AgcImageSubresourceLayers layers;
        if (!native_image_copy_layers(source, &r[region].imageSubresource,
                &layers) || !r[region].imageExtent.width ||
            !r[region].imageExtent.height || !r[region].imageExtent.depth) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }
    if (!native_require_complete_stream(command))
        return;
    VkResult prepare = native_transition_whole_image(command, source,
        native_image_recorded_usage(command, source),
        kAgcResourceUsageCopySource);
    if (prepare == VK_SUCCESS)
        prepare = native_prepare_buffer_range(command, destination, 0u,
            destination->size, kAgcResourceUsageCopyDestination);
    if (prepare != VK_SUCCESS) {
        command->record_error = prepare;
        return;
    }
    for (uint32_t region = 0u; region < n; ++region) {
        AgcBufferImageCopyRegion copy = AGC_BUFFER_IMAGE_COPY_REGION_INIT;
        copy.buffer_offset = r[region].bufferOffset;
        copy.buffer_row_length = r[region].bufferRowLength;
        copy.buffer_image_height = r[region].bufferImageHeight;
        (void)native_image_copy_layers(source, &r[region].imageSubresource,
            &copy.image_subresource);
        copy.image_offset = (AgcOffset3D){r[region].imageOffset.x,
            r[region].imageOffset.y, r[region].imageOffset.z};
        copy.image_extent = (AgcExtent3D){r[region].imageExtent.width,
            r[region].imageExtent.height, r[region].imageExtent.depth};
        int32_t result = agcCmdCopyImageToBuffer(
            command->native_graphics_command_buffer, source->native_image,
            destination->native_buffer, 1u, &copy);
        if (result != AGC_OK) {
            command->record_error = native_command_result(result);
            return;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdUpdateBuffer(VkCommandBuffer c, VkBuffer d, VkDeviceSize o, VkDeviceSize n,
                  const void *p) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *destination = (VkPs5Buffer *)d;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (command->active_render_pass || !destination || !p ||
        !destination->memory || !destination->native_buffer || !n ||
        n > 65536u || ((o | n) & 3u) != 0u ||
        o > destination->size || n > destination->size - o ||
        !(destination->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (!native_require_complete_stream(command))
        return;
    VkResult prepare = native_prepare_buffer_range(command, destination,
        o, n, kAgcResourceUsageCopyDestination);
    if (prepare != VK_SUCCESS) {
        command->record_error = prepare;
        return;
    }
    int32_t result = agcCmdUpdateBuffer(
        command->native_graphics_command_buffer,
        destination->native_buffer, o, p, n);
    if (result != AGC_OK)
        command->record_error = native_command_result(result);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdFillBuffer(VkCommandBuffer c, VkBuffer d, VkDeviceSize o, VkDeviceSize n,
                uint32_t v) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *destination = (VkPs5Buffer *)d;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!destination || o > destination->size) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    VkDeviceSize size = n == VK_WHOLE_SIZE ? destination->size - o : n;
    if (command->active_render_pass || !destination->memory ||
        !destination->native_buffer || !size || ((o | size) & 3u) != 0u ||
        size > destination->size - o ||
        !(destination->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (!native_require_complete_stream(command))
        return;
    VkResult prepare = native_prepare_buffer_range(command, destination,
        o, size, kAgcResourceUsageCopyDestination);
    if (prepare != VK_SUCCESS) {
        command->record_error = prepare;
        return;
    }
    int32_t result = agcCmdFillBuffer(
        command->native_graphics_command_buffer,
        destination->native_buffer, o, size, v);
    if (result != AGC_OK)
        command->record_error = native_command_result(result);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d,
                     VkDependencyFlags f, uint32_t mn, const VkMemoryBarrier *m,
                     uint32_t bn, const VkBufferMemoryBarrier *b, uint32_t in,
                     const VkImageMemoryBarrier *i) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdPipelineBarrier");
    IGNORE(s);
    IGNORE(d);
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (command->active_render_pass || (f & ~VK_DEPENDENCY_BY_REGION_BIT) != 0u ||
        (mn && !m) || (bn && !b) || (in && !i) ||
        bn > VK_PS5_MAX_NATIVE_RESOURCE_STATES -
            command->native_buffer_state_count ||
        in > VK_PS5_MAX_NATIVE_RESOURCE_STATES -
            command->native_image_state_count) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t index = 0u; index < mn; ++index) {
        AgcResourceUsage before;
        AgcResourceUsage after;
        if (!native_usage_from_access(m[index].srcAccessMask,
                                      VK_IMAGE_LAYOUT_GENERAL, &before) ||
            !native_usage_from_access(m[index].dstAccessMask,
                                      VK_IMAGE_LAYOUT_GENERAL, &after)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
    }
    for (uint32_t index = 0u; index < bn; ++index) {
        const VkBufferMemoryBarrier *barrier = &b[index];
        VkPs5Buffer *buffer = (VkPs5Buffer *)barrier->buffer;
        AgcResourceUsage before;
        AgcResourceUsage after;
        uint64_t size;
        if (!buffer || !buffer->native_buffer ||
            !native_queue_family_barrier(barrier->srcQueueFamilyIndex,
                                         barrier->dstQueueFamilyIndex) ||
            !native_usage_from_access(barrier->srcAccessMask,
                                      VK_IMAGE_LAYOUT_GENERAL, &before) ||
            !native_usage_from_access(barrier->dstAccessMask,
                                      VK_IMAGE_LAYOUT_GENERAL, &after) ||
            barrier->offset > buffer->size) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        size = barrier->size == VK_WHOLE_SIZE ?
            buffer->size - barrier->offset : barrier->size;
        if (!size || size > buffer->size - barrier->offset) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }
    for (uint32_t index = 0u; index < in; ++index) {
        const VkImageMemoryBarrier *barrier = &i[index];
        VkPs5Image *image = (VkPs5Image *)barrier->image;
        AgcResourceUsage before;
        AgcResourceUsage after;
        AgcImageSubresourceRange range;
        if (!image || !image->native_image ||
            !native_queue_family_barrier(barrier->srcQueueFamilyIndex,
                                         barrier->dstQueueFamilyIndex) ||
            !native_usage_from_access(barrier->srcAccessMask,
                                      barrier->oldLayout, &before) ||
            !native_usage_from_access(barrier->dstAccessMask,
                                      barrier->newLayout, &after) ||
            !native_image_range(image, &barrier->subresourceRange, &range)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
    }
    for (uint32_t index = 0u; index < mn; ++index) {
        AgcResourceUsage before;
        AgcResourceUsage after;
        (void)native_usage_from_access(m[index].srcAccessMask,
                                       VK_IMAGE_LAYOUT_GENERAL, &before);
        (void)native_usage_from_access(m[index].dstAccessMask,
                                       VK_IMAGE_LAYOUT_GENERAL, &after);
        if (before == kAgcResourceUsageUndefined)
            continue;
        if (after == kAgcResourceUsageUndefined)
            after = before;
        if (!native_require_complete_stream(command))
            return;
        for (uint32_t state_index = 0u;
             state_index < command->native_buffer_state_count; ++state_index) {
            VkPs5NativeBufferState *state =
                &command->native_buffer_states[state_index];
            if (state->usage != before)
                continue;
            VkResult result = native_prepare_buffer_range(command,
                state->buffer, 0u, state->buffer->size, after);
            if (result != VK_SUCCESS) {
                command->record_error = result;
                return;
            }
        }
        for (uint32_t state_index = 0u;
             state_index < command->native_image_state_count; ++state_index) {
            VkPs5NativeImageState *state =
                &command->native_image_states[state_index];
            if (state->usage != before ||
                !native_image_supports_usage(state->image, after))
                continue;
            VkResult result = native_transition_whole_image(command,
                state->image, before, after);
            if (result != VK_SUCCESS) {
                command->record_error = result;
                return;
            }
        }
    }
    for (uint32_t index = 0u; index < bn; ++index) {
        const VkBufferMemoryBarrier *barrier = &b[index];
        VkPs5Buffer *buffer = (VkPs5Buffer *)barrier->buffer;
        AgcResourceUsage after;
        uint64_t size = barrier->size == VK_WHOLE_SIZE ?
            buffer->size - barrier->offset : barrier->size;
        AgcResourceStateInfo state = AGC_RESOURCE_STATE_INFO_INIT;
        AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
        (void)native_usage_from_access(barrier->dstAccessMask,
                                       VK_IMAGE_LAYOUT_GENERAL, &after);
        int32_t result = agcGetCommandBufferRangeStateInfo(
            command->native_graphics_command_buffer, buffer->native_buffer,
            barrier->offset, size, &state);
        if (result != AGC_OK) {
            command->record_error = native_command_result(result);
            return;
        }
        /* Buffer access masks define synchronization scopes, not layouts.
         * Derive the native prior usage and owner from the exact range so a
         * partial transition or a zero source mask cannot stale the coarse
         * whole-buffer mirror. */
        AgcResourceUsage effective_before = state.usage;
        AgcResourceUsage effective_after = after ==
            kAgcResourceUsageUndefined ? effective_before : after;
        transition.before = effective_before;
        transition.after = effective_after;
        transition.before_owner = state.owner;
        transition.after_owner = native_owner_for_usage(transition.after);
        transition.buffer = buffer->native_buffer;
        transition.buffer_offset = barrier->offset;
        transition.buffer_size = size;
        /* Access-only Vulkan barriers frequently restate the range's current
         * state.  They still establish an execution dependency, but require
         * no OpenAGC resource transition; in particular, undefined ranges
         * cannot be represented as undefined-to-undefined transitions. */
        result = AGC_OK;
        if (transition.before != transition.after ||
            transition.before_owner != transition.after_owner) {
            result = agcCmdTransitionResources(
                command->native_graphics_command_buffer, 1u, &transition);
        }
        if (result != AGC_OK) {
            fprintf(stderr,
                "vulkan-ps5: buffer barrier transition failed "
                "result=0x%08x src_access=0x%x dst_access=0x%x "
                "offset=%llu size=%llu buffer_size=%llu "
                "before=%u/%u after=%u/%u\n",
                (unsigned)result, barrier->srcAccessMask,
                barrier->dstAccessMask,
                (unsigned long long)barrier->offset,
                (unsigned long long)size,
                (unsigned long long)buffer->size,
                transition.before, transition.before_owner,
                transition.after, transition.after_owner);
        }
        AgcResourceUsage tracked_after = barrier->offset == 0u &&
            size == buffer->size ? effective_after :
            kAgcResourceUsageUndefined;
        if (result != AGC_OK ||
            !native_record_buffer_usage(command, buffer, tracked_after)) {
            command->record_error = result != AGC_OK ?
                native_command_result(result) : VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
    }
    for (uint32_t index = 0u; index < in; ++index) {
        const VkImageMemoryBarrier *barrier = &i[index];
        VkPs5Image *image = (VkPs5Image *)barrier->image;
        AgcResourceUsage before;
        AgcResourceUsage after;
        AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
        (void)native_usage_from_access(barrier->srcAccessMask,
                                       barrier->oldLayout, &before);
        (void)native_usage_from_access(barrier->dstAccessMask,
                                       barrier->newLayout, &after);
        transition.resource_type = kAgcResourceTypeImage;
        AgcResourceUsage current =
            native_image_recorded_usage(command, image);
        AgcResourceUsage effective_before = before ==
            kAgcResourceUsageUndefined ? current : before;
        AgcResourceUsage effective_after = after ==
            kAgcResourceUsageUndefined ? effective_before : after;
        transition.before = effective_before;
        transition.after = effective_after;
        transition.before_owner = native_owner_for_usage(transition.before);
        transition.after_owner = native_owner_for_usage(transition.after);
        transition.image = image->native_image;
        (void)native_image_range(image, &barrier->subresourceRange,
                                 &transition.image_range);
        int32_t result = AGC_OK;
        if (transition.before != transition.after ||
            transition.before_owner != transition.after_owner) {
            result = agcCmdTransitionResources(
                command->native_graphics_command_buffer, 1u, &transition);
        }
        if (result != AGC_OK) {
            fprintf(stderr,
                "vulkan-ps5: image barrier transition failed "
                "result=0x%08x old_layout=%u new_layout=%u "
                "src_access=0x%x dst_access=0x%x before=%u/%u "
                "after=%u/%u aspect=0x%x mip=%u+%u layer=%u+%u\n",
                (unsigned)result, barrier->oldLayout, barrier->newLayout,
                barrier->srcAccessMask, barrier->dstAccessMask,
                transition.before, transition.before_owner,
                transition.after, transition.after_owner,
                transition.image_range.aspect_mask,
                transition.image_range.base_mip_level,
                transition.image_range.mip_level_count,
                transition.image_range.base_array_layer,
                transition.image_range.array_layer_count);
        }
        AgcResourceUsage tracked_after = native_image_range_is_whole(
            image, &transition.image_range) ? effective_after :
            kAgcResourceUsageUndefined;
        if (result != AGC_OK ||
            !native_record_image_usage(command, image, tracked_after)) {
            command->record_error = result != AGC_OK ?
                native_command_result(result) : VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginQuery(VkCommandBuffer c, VkQueryPool p, uint32_t q, VkQueryControlFlags f) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !pool ||
        pool->type != VK_QUERY_TYPE_OCCLUSION || q >= pool->count ||
        !command->active_render_pass || command->active_query_pool ||
        (f & ~VK_QUERY_CONTROL_PRECISE_BIT) != 0u) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (!native_require_complete_stream(command))
        return;
    int32_t result = agcCmdBeginOcclusionQuery(
        command->native_graphics_command_buffer, pool->native_buffer,
        (uint64_t)q * pool->record_size,
        (f & VK_QUERY_CONTROL_PRECISE_BIT) != 0u);
    if (result != AGC_OK) {
        command->record_error = native_command_result(result);
        return;
    }
    command->active_query_pool = pool;
    command->active_query = q;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndQuery(VkCommandBuffer c, VkQueryPool p, uint32_t q) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !pool ||
        command->active_query_pool != pool || command->active_query != q) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (!native_require_complete_stream(command)) {
        command->active_query_pool = NULL;
        return;
    }
    int32_t result = agcCmdEndOcclusionQuery(
        command->native_graphics_command_buffer, pool->native_buffer,
        (uint64_t)q * pool->record_size);
    if (result != AGC_OK)
        command->record_error = native_command_result(result);
    command->active_query_pool = NULL;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResetQueryPool(VkCommandBuffer c, VkQueryPool p, uint32_t f, uint32_t n) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5QueryPool *pool = (VkPs5QueryPool *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !pool ||
        command->active_render_pass || f > pool->count || n > pool->count - f) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (!n)
        return;
    if (!native_require_complete_stream(command))
        return;
    int32_t result = agcCmdResetOcclusionQueries(
        command->native_graphics_command_buffer, pool->native_buffer,
        (uint64_t)f * pool->record_size, n);
    if (result != AGC_OK)
        command->record_error = native_command_result(result);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdWriteTimestamp(VkCommandBuffer c, VkPipelineStageFlagBits s, VkQueryPool p,
                    uint32_t q) {
    IGNORE(s); IGNORE(p); IGNORE(q);
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (command && command->state == VK_PS5_COMMAND_RECORDING)
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyQueryPoolResults(VkCommandBuffer c, VkQueryPool p, uint32_t f, uint32_t n,
                          VkBuffer d, VkDeviceSize o, VkDeviceSize s,
                          VkQueryResultFlags flags) {
    IGNORE(p); IGNORE(f); IGNORE(n); IGNORE(d); IGNORE(o); IGNORE(s); IGNORE(flags);
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (command && command->state == VK_PS5_COMMAND_RECORDING)
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdExecuteCommands(VkCommandBuffer c, uint32_t n, const VkCommandBuffer *p) {
    IGNORE(c); IGNORE(n); IGNORE(p);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdBindPipeline");
    VkPs5Pipeline *pipeline = (VkPs5Pipeline *)p;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        !pipeline || pipeline->bind_point != b) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    int32_t native_result = AGC_OK;
    if (b == VK_PIPELINE_BIND_POINT_COMPUTE) {
        command->bound_compute = pipeline;
        if (pipeline->native_compute_pipeline &&
            command->native_bound_compute !=
                pipeline->native_compute_pipeline) {
            native_result = agcCmdBindComputePipeline(
                command->native_graphics_command_buffer,
                pipeline->native_compute_pipeline);
            if (native_result == AGC_OK)
                command->native_bound_compute =
                    pipeline->native_compute_pipeline;
        }
    } else if (b == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        command->bound_graphics = pipeline;
        if (pipeline->native_graphics_pipeline &&
            command->native_bound_graphics !=
                pipeline->native_graphics_pipeline) {
            native_result = agcCmdBindGraphicsPipeline(
                command->native_graphics_command_buffer,
                pipeline->native_graphics_pipeline);
            if (native_result == AGC_OK)
                command->native_bound_graphics =
                    pipeline->native_graphics_pipeline;
            if (native_result == AGC_OK && pipeline->line_width_dynamic &&
                command->dynamic_line_width_set)
                native_result = agcCmdSetLineWidth(
                    command->native_graphics_command_buffer,
                    command->dynamic_line_width);
            if (native_result == AGC_OK && pipeline->depth_bias_enable &&
                pipeline->depth_bias_dynamic &&
                command->dynamic_depth_bias_set) {
                AgcDepthBias depth_bias = AGC_DEPTH_BIAS_INIT;
                depth_bias.constant_factor =
                    command->dynamic_depth_bias.constant_factor;
                depth_bias.clamp = command->dynamic_depth_bias.clamp;
                depth_bias.slope_factor =
                    command->dynamic_depth_bias.slope_factor;
                native_result = agcCmdSetDepthBias(
                    command->native_graphics_command_buffer, &depth_bias);
            }
            if (native_result == AGC_OK &&
                pipeline->blend_constants_dynamic &&
                command->dynamic_blend_constants_set)
                native_result = agcCmdSetBlendConstants(
                    command->native_graphics_command_buffer,
                    command->dynamic_blend_constants);
            if (native_result == AGC_OK &&
                pipeline->stencil_reference_dynamic &&
                pipeline->depth_stencil.stencil_test_enable &&
                command->dynamic_stencil_reference_set)
                native_result = agcCmdSetStencilReference(
                    command->native_graphics_command_buffer,
                    command->dynamic_stencil_reference_front,
                    command->dynamic_stencil_reference_back);
        }
    } else {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    if (native_result != AGC_OK)
        command->record_error = native_result == AGC_ERROR_BUFFER_TOO_SMALL ||
            native_result == AGC_ERROR_OUT_OF_MEMORY ?
            VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_INITIALIZATION_FAILED;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint b, VkPipelineLayout l,
                        uint32_t f, uint32_t n, const VkDescriptorSet *s,
                        uint32_t dn, const uint32_t *d) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdBindDescriptorSets");
    IGNORE(l); IGNORE(d);
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        (n && !s) || f > OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ||
        n > OPENAGC_PSBC_MAX_DESCRIPTOR_SETS - f || dn != 0u) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = dn ? VK_ERROR_FEATURE_NOT_PRESENT :
                VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    VkPs5DescriptorSet **sets = b == VK_PIPELINE_BIND_POINT_COMPUTE ?
        command->compute_sets : b == VK_PIPELINE_BIND_POINT_GRAPHICS ?
        command->graphics_sets : NULL;
    if (!sets) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (!s[i]) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        sets[f + i] = (VkPs5DescriptorSet *)s[i];
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(VkCommandBuffer c, VkImage i, VkImageLayout l,
                     const VkClearColorValue *v, uint32_t n,
                     const VkImageSubresourceRange *r) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdClearColorImage");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    command->record_error = native_clear_color_image(command,
        (VkPs5Image *)i, l, v, n, r);
}

static VkPs5DescriptorValue *descriptor_value(
    VkPs5DescriptorSet *set, uint32_t binding, uint32_t array_element,
    VkDescriptorType *type)
{
    uint32_t first = 0u;
    if (!set) return NULL;
    for (uint32_t i = 0; i < set->layout->binding_count; ++i) {
        const VkDescriptorSetLayoutBinding *candidate = &set->layout->bindings[i];
        if (candidate->binding == binding) {
            if (array_element >= candidate->descriptorCount) return NULL;
            if (type) *type = candidate->descriptorType;
            return &set->values[first + array_element];
        }
        first += candidate->descriptorCount;
    }
    return NULL;
}

static uint64_t native_push_constant_mask(uint32_t offset, uint32_t size)
{
    uint32_t dword_count = size / 4u;
    return dword_count == 64u ? UINT64_MAX :
        ((UINT64_C(1) << dword_count) - 1u) << (offset / 4u);
}

static VkResult native_replay_push_constants(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline)
{
    for (uint32_t stage_index = 0u;
         stage_index < pipeline->stage_count; ++stage_index) {
        const AgcShaderReflection *reflection =
            &pipeline->stages[stage_index].metadata;
        for (uint32_t range_index = 0u;
             range_index < reflection->push_constant_range_count;
             ++range_index) {
            const AgcShaderPushConstantRange *range =
                &reflection->push_constant_ranges[range_index];
            uint64_t required = native_push_constant_mask(
                range->offset, range->size);
            const uint32_t stage = reflection->stage;
            const uint32_t stage_bit = 1u << stage;
            if ((range->stage_mask & stage_bit) != 0u &&
                (command->push_constant_masks[stage] & required) ==
                    required) {
                int32_t result = agcCmdPushConstants(
                    command->native_graphics_command_buffer, stage_bit,
                    range->offset, range->size,
                    command->push_constant_data[stage] + range->offset);
                if (result != AGC_OK) {
                    fprintf(stderr,
                        "vulkan-ps5: agcCmdPushConstants failed "
                        "stage=%u offset=%u size=%u result=0x%x\n",
                        stage, range->offset, range->size,
                        (unsigned int)result);
                    return native_command_result(result);
                }
            }
        }
    }
    return VK_SUCCESS;
}

static VkResult prepare_native_compute_descriptors(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline,
    bool *ready)
{
    enum { VK_PS5_MAX_NATIVE_DESCRIPTOR_WRITES = 256 };
    AgcDescriptorWrite writes[VK_PS5_MAX_NATIVE_DESCRIPTOR_WRITES];
    uint32_t write_count = 0u;
    const OpenAgcPsbcMetadata *metadata = &pipeline->stages[0].metadata;
    *ready = false;
    VkResult push_result = native_replay_push_constants(command, pipeline);
    if (push_result != VK_SUCCESS)
        return push_result;
    for (uint32_t mapping_index = 0u;
         mapping_index < metadata->descriptor_mapping_count;
         ++mapping_index) {
        const OpenAgcPsbcDescriptorMapping *mapping =
            &metadata->descriptor_mappings[mapping_index];
        uint32_t array_size =
            AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size);
        VkPs5DescriptorSet *set = mapping->set <
            OPENAGC_PSBC_MAX_DESCRIPTOR_SETS ?
            command->compute_sets[mapping->set] : NULL;
        if (!array_size || array_size >
                VK_PS5_MAX_NATIVE_DESCRIPTOR_WRITES - write_count)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        if (!set)
            return VK_ERROR_INITIALIZATION_FAILED;
        for (uint32_t array = 0u; array < array_size; ++array) {
            VkDescriptorType layout_type;
            VkPs5DescriptorValue *value = descriptor_value(
                set, mapping->binding, array, &layout_type);
            OpenAgcPsbcDescriptorType psbc_type;
            AgcDescriptorWrite *write = &writes[write_count];
            if (!value || !value->valid ||
                !psbc_descriptor_type(layout_type, &psbc_type) ||
                psbc_type != mapping->type)
                return VK_ERROR_INITIALIZATION_FAILED;
            *write = (AgcDescriptorWrite)AGC_DESCRIPTOR_WRITE_INIT;
            write->set = mapping->set;
            write->binding = mapping->binding;
            write->array_element = array;
            write->type = (AgcShaderDescriptorType)mapping->type;
            if (mapping->type == OPENAGC_PSBC_DESCRIPTOR_UNIFORM_BUFFER ||
                mapping->type == OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER) {
                VkPs5Buffer *buffer = (VkPs5Buffer *)value->buffer.buffer;
                AgcResourceUsage usage;
                uint64_t range;
                if (!buffer) {
                    if (!vk_ps5_device_null_descriptor(command->device))
                        return VK_ERROR_INITIALIZATION_FAILED;
                    write_count++;
                    continue;
                }
                if (!buffer->native_buffer ||
                    value->buffer.offset > buffer->size)
                    return VK_ERROR_INITIALIZATION_FAILED;
                range = value->buffer.range == VK_WHOLE_SIZE ?
                    buffer->size - value->buffer.offset : value->buffer.range;
                if (!range || range > buffer->size - value->buffer.offset)
                    return VK_ERROR_INITIALIZATION_FAILED;
                usage = native_buffer_recorded_usage(command, buffer);
                if (usage != kAgcResourceUsageShaderRead &&
                    !(mapping->type ==
                        OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER &&
                      usage == kAgcResourceUsageShaderWrite))
                    return VK_SUCCESS;
                write->buffer = buffer->native_buffer;
                write->buffer_offset = value->buffer.offset;
                write->buffer_range = range;
            } else if (mapping->type ==
                       OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE) {
                VkPs5ImageView *view =
                    (VkPs5ImageView *)value->image.imageView;
                if (!view) {
                    if (!vk_ps5_device_null_descriptor(command->device))
                        return VK_ERROR_INITIALIZATION_FAILED;
                    write_count++;
                    continue;
                }
                VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
                AgcResourceUsage usage = image ?
                    native_image_recorded_usage(command, image) :
                    kAgcResourceUsageUndefined;
                VkResult view_result = ensure_native_image_view(view);
                if (view_result != VK_SUCCESS)
                    return view_result;
                if (!view || !view->native_view ||
                    (usage != kAgcResourceUsageShaderRead &&
                     usage != kAgcResourceUsageShaderWrite))
                    return VK_SUCCESS;
                write->image_view = view->native_view;
            } else {
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            write_count++;
        }
    }
    if (write_count) {
        int32_t result = agcCmdBindDescriptors(
            command->native_graphics_command_buffer, write_count, writes);
        if (result != AGC_OK) {
            AgcDebugMessage debug_message = AGC_DEBUG_MESSAGE_INIT;
            int32_t debug_result = agcGetLastDebugMessage(
                vk_ps5_native_device(command->device), &debug_message);
            fprintf(stderr,
                "vulkan-ps5: agcCmdBindDescriptors failed result=0x%x "
                "writes=%u%s%s\n",
                (unsigned int)result, write_count,
                debug_result == AGC_OK ? ": " : "",
                debug_result == AGC_OK ? debug_message.message : "");
            for (uint32_t i = 0u; i < write_count; ++i)
                fprintf(stderr,
                    "vulkan-ps5: descriptor write[%u] set=%u binding=%u "
                    "array=%u type=%u\n", i, writes[i].set,
                    writes[i].binding, writes[i].array_element,
                    writes[i].type);
            return native_command_result(result);
        }
        command->native_descriptor_bind_count++;
    }
    *ready = true;
    return VK_SUCCESS;
}

static VkResult native_ensure_compute_pipeline(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline)
{
    if (!command || !pipeline || !pipeline->native_compute_pipeline)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (command->native_bound_compute == pipeline->native_compute_pipeline)
        return VK_SUCCESS;
    int32_t result = agcCmdBindComputePipeline(
        command->native_graphics_command_buffer,
        pipeline->native_compute_pipeline);
    if (result != AGC_OK)
        return native_command_result(result);
    command->native_bound_compute = pipeline->native_compute_pipeline;
    return VK_SUCCESS;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    VkPs5Pipeline *pipeline = command->bound_compute;
    if (!pipeline || !x || !y || !z ||
        pipeline->stage_types[0] != OPENAGC_PSBC_STAGE_COMPUTE) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    const OpenAgcPsbcMetadata *metadata = &pipeline->stages[0].metadata;
    if (!metadata->local_size_x || !metadata->local_size_y ||
        !metadata->local_size_z) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    VkResult bind_result = native_ensure_compute_pipeline(command, pipeline);
    if (bind_result != VK_SUCCESS) {
        command->record_error = bind_result;
        return;
    }
    bool native_ready = false;
    VkResult native_prepare = prepare_native_compute_descriptors(
        command, pipeline, &native_ready);
    if (native_prepare != VK_SUCCESS) {
        command->record_error = native_prepare;
        return;
    }
    if (!native_ready) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    int32_t result = agcCmdDispatch(command->native_graphics_command_buffer,
        x, y, z);
    if (result != AGC_OK)
        command->record_error = native_command_result(result);
    else
        command->native_dispatch_count++;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatchIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *arguments = (VkPs5Buffer *)b;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    VkPs5Pipeline *pipeline = command->bound_compute;
    if (!pipeline || pipeline->stage_types[0] != OPENAGC_PSBC_STAGE_COMPUTE ||
        !arguments || !arguments->memory ||
        !(arguments->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ||
        (o & 3u) != 0u || o > arguments->size ||
        sizeof(VkDispatchIndirectCommand) > arguments->size - o ||
        arguments->memory_offset > UINT64_MAX - o) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    const OpenAgcPsbcMetadata *metadata = &pipeline->stages[0].metadata;
    if (!metadata->local_size_x || !metadata->local_size_y ||
        !metadata->local_size_z) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    if (!native_require_complete_stream(command))
        return;
    VkResult bind_result = native_ensure_compute_pipeline(command, pipeline);
    if (bind_result != VK_SUCCESS) {
        command->record_error = bind_result;
        return;
    }
    bool native_ready = false;
    VkResult native_prepare = prepare_native_compute_descriptors(
        command, pipeline, &native_ready);
    if (native_prepare != VK_SUCCESS) {
        command->record_error = native_prepare;
        return;
    }
    if (!native_ready || !arguments->native_buffer ||
        native_buffer_recorded_usage(command, arguments) !=
            kAgcResourceUsageShaderRead) {
        native_mark_stream_incomplete(command);
        return;
    }
    int32_t result = agcCmdDispatchIndirect(
        command->native_graphics_command_buffer,
        arguments->native_buffer, o);
    if (result != AGC_OK)
        command->record_error = native_command_result(result);
    else
        command->native_dispatch_count++;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetEvent(VkCommandBuffer c, VkEvent e, VkPipelineStageFlags s) {
    IGNORE(c); IGNORE(e); IGNORE(s);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResetEvent(VkCommandBuffer c, VkEvent e, VkPipelineStageFlags s) {
    IGNORE(c); IGNORE(e); IGNORE(s);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdWaitEvents(VkCommandBuffer c, uint32_t n, const VkEvent *e,
                VkPipelineStageFlags s, VkPipelineStageFlags d, uint32_t mn,
                const VkMemoryBarrier *m, uint32_t bn,
                const VkBufferMemoryBarrier *b, uint32_t in,
                const VkImageMemoryBarrier *i) {
    IGNORE(c); IGNORE(n); IGNORE(e); IGNORE(s); IGNORE(d); IGNORE(mn); IGNORE(m);
    IGNORE(bn); IGNORE(b); IGNORE(in); IGNORE(i);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdPushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s,
                   uint32_t o, uint32_t n, const void *v) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdPushConstants");
    VkPs5PipelineLayout *layout = (VkPs5PipelineLayout *)l;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!layout || !v || !n || (o & 3u) != 0u || (n & 3u) != 0u ||
        o > layout->push_constant_size ||
        n > layout->push_constant_size - o || !s) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    const VkShaderStageFlags supported =
        VK_SHADER_STAGE_VERTEX_BIT |
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        VK_SHADER_STAGE_GEOMETRY_BIT |
        VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_COMPUTE_BIT;
    VkShaderStageFlags stages = s == VK_SHADER_STAGE_ALL ? supported : s;
    if ((stages & ~supported) != 0u) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    uint32_t stage_mask = 0u;
    if (stages & VK_SHADER_STAGE_VERTEX_BIT)
        stage_mask |= (1u << kAgcShaderStageVs);
    if (stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
        stage_mask |= (1u << kAgcShaderStageHs);
    if (stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
        stage_mask |= (1u << kAgcShaderStageDs);
    if (stages & VK_SHADER_STAGE_GEOMETRY_BIT)
        stage_mask |= (1u << kAgcShaderStageGs);
    if (stages & VK_SHADER_STAGE_FRAGMENT_BIT)
        stage_mask |= (1u << kAgcShaderStagePs);
    if (stages & VK_SHADER_STAGE_COMPUTE_BIT)
        stage_mask |= (1u << kAgcShaderStageCs);
    uint64_t written = native_push_constant_mask(o, n);
#ifdef __PROSPERO__
    const uint32_t *words = (const uint32_t *)v;
    fprintf(stderr,
        "vulkan-ps5: vkCmdPushConstants stages=0x%x native=0x%x "
        "offset=%u size=%u words=%08x,%08x,%08x,%08x\n",
        s, stage_mask, o, n, n >= 4u ? words[0] : 0u,
        n >= 8u ? words[1] : 0u, n >= 12u ? words[2] : 0u,
        n >= 16u ? words[3] : 0u);
#endif
    for (uint32_t stage = 0u; stage < kAgcShaderStageCount; ++stage) {
        if ((stage_mask & (1u << stage)) != 0u) {
            memcpy(command->push_constant_data[stage] + o, v, n);
            command->push_constant_masks[stage] |= written;
        }
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetViewport(VkCommandBuffer c, uint32_t f, uint32_t n, const VkViewport *v) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdSetViewport");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (f > VK_PS5_MAX_VIEWPORTS || n > VK_PS5_MAX_VIEWPORTS - f ||
        (n && !v)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    AgcGfx1013Viewport translated[VK_PS5_MAX_VIEWPORTS];
    for (uint32_t i = 0u; i < n; ++i) {
        if (!translate_viewport(&v[i], &translated[i])) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
    }
    if (!n)
        return;
    memcpy(&command->dynamic_viewports[f], translated,
        n * sizeof(translated[0]));
    command->dynamic_viewport_mask |= ((1u << n) - 1u) << f;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetScissor(VkCommandBuffer c, uint32_t f, uint32_t n, const VkRect2D *r) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdSetScissor");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (f > VK_PS5_MAX_VIEWPORTS || n > VK_PS5_MAX_VIEWPORTS - f ||
        (n && !r)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    AgcGfx1013ScissorState translated[VK_PS5_MAX_VIEWPORTS];
    for (uint32_t i = 0u; i < n; ++i) {
        if (!translate_scissor(&r[i], &translated[i])) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
    }
    if (!n)
        return;
    memcpy(&command->dynamic_scissors[f], translated,
        n * sizeof(translated[0]));
    command->dynamic_scissor_mask |= ((1u << n) - 1u) << f;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetLineWidth(VkCommandBuffer c, float w) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdSetLineWidth");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!(w >= 1.0f) || !(w <= 64.0f)) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    command->dynamic_line_width = w;
    command->dynamic_line_width_set = VK_TRUE;
    if (command->bound_graphics &&
        command->bound_graphics->line_width_dynamic &&
        command->native_bound_graphics ==
            command->bound_graphics->native_graphics_pipeline &&
        agcCmdSetLineWidth(command->native_graphics_command_buffer, w) !=
            AGC_OK)
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDepthBias(VkCommandBuffer c, float constantFactor,
                  float clamp, float slopeFactor) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdSetDepthBias");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    command->dynamic_depth_bias = (AgcGfx1013DepthBiasState){
        .constant_factor = constantFactor,
        .clamp = clamp,
        .slope_factor = slopeFactor,
    };
    command->dynamic_depth_bias_set = VK_TRUE;
    if (command->bound_graphics &&
        command->bound_graphics->depth_bias_enable &&
        command->bound_graphics->depth_bias_dynamic &&
        command->native_bound_graphics ==
            command->bound_graphics->native_graphics_pipeline) {
        AgcDepthBias depth_bias = AGC_DEPTH_BIAS_INIT;
        depth_bias.constant_factor = constantFactor;
        depth_bias.clamp = clamp;
        depth_bias.slope_factor = slopeFactor;
        if (agcCmdSetDepthBias(command->native_graphics_command_buffer,
                &depth_bias) != AGC_OK)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetBlendConstants(VkCommandBuffer c, const float v[4]) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdSetBlendConstants");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !v ||
        command->record_error != VK_SUCCESS)
        return;
    memcpy(command->dynamic_blend_constants, v,
           sizeof(command->dynamic_blend_constants));
    command->dynamic_blend_constants_set = VK_TRUE;
    if (command->bound_graphics &&
        command->bound_graphics->blend_constants_dynamic &&
        command->native_bound_graphics ==
            command->bound_graphics->native_graphics_pipeline &&
        agcCmdSetBlendConstants(command->native_graphics_command_buffer, v) !=
            AGC_OK)
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDepthBounds(VkCommandBuffer c, float a, float b) { IGNORE(c); IGNORE(a); IGNORE(b); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilCompareMask(VkCommandBuffer c, VkStencilFaceFlags f, uint32_t m) {
    IGNORE(c); IGNORE(f); IGNORE(m);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilWriteMask(VkCommandBuffer c, VkStencilFaceFlags f, uint32_t m) {
    IGNORE(c); IGNORE(f); IGNORE(m);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilReference(VkCommandBuffer c, VkStencilFaceFlags f, uint32_t r) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdSetStencilReference");
    const VkStencilFaceFlags known = VK_STENCIL_FACE_FRONT_BIT |
                                     VK_STENCIL_FACE_BACK_BIT;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!(f & known) || (f & ~known)) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (f & VK_STENCIL_FACE_FRONT_BIT)
        command->dynamic_stencil_reference_front = r & 0xffu;
    if (f & VK_STENCIL_FACE_BACK_BIT)
        command->dynamic_stencil_reference_back = r & 0xffu;
    command->dynamic_stencil_reference_set = VK_TRUE;
    if (command->bound_graphics &&
        command->bound_graphics->stencil_reference_dynamic &&
        command->bound_graphics->depth_stencil.stencil_test_enable &&
        command->native_bound_graphics ==
            command->bound_graphics->native_graphics_pipeline &&
        agcCmdSetStencilReference(command->native_graphics_command_buffer,
            command->dynamic_stencil_reference_front,
            command->dynamic_stencil_reference_back) != AGC_OK)
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindIndexBuffer(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkIndexType t) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *buffer = (VkPs5Buffer *)b;
    uint32_t element_size = t == VK_INDEX_TYPE_UINT16 ? 2u :
        t == VK_INDEX_TYPE_UINT32 ? 4u : 0u;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !buffer ||
        !buffer->memory || !(buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
        !element_size || o >= buffer->size || (o & (element_size - 1u))) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = t == VK_INDEX_TYPE_UINT8_EXT ?
                VK_ERROR_FEATURE_NOT_PRESENT : VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    command->index_buffer = buffer;
    command->index_offset = o;
    command->index_size = buffer->size - o;
    command->index_type = t;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindIndexBuffer2(VkCommandBuffer c, VkBuffer b, VkDeviceSize o,
                      VkDeviceSize size, VkIndexType t) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    VkPs5Buffer *buffer = (VkPs5Buffer *)b;
    uint32_t element_size = t == VK_INDEX_TYPE_UINT16 ? 2u :
        t == VK_INDEX_TYPE_UINT32 ? 4u : 0u;
    VkDeviceSize resolved_size = size;
    if (buffer && o <= buffer->size && size == VK_WHOLE_SIZE)
        resolved_size = buffer->size - o;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING || !buffer ||
        !buffer->memory || !(buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
        !element_size || o >= buffer->size || (o & (element_size - 1u)) ||
        !resolved_size || resolved_size > buffer->size - o) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = t == VK_INDEX_TYPE_UINT8_EXT ?
                VK_ERROR_FEATURE_NOT_PRESENT : VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    command->index_buffer = buffer;
    command->index_offset = o;
    command->index_size = resolved_size;
    command->index_type = t;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetRenderingAreaGranularity(VkDevice device,
    const VkRenderingAreaInfo *pRenderingAreaInfo, VkExtent2D *pGranularity) {
    (void)device;
    if (!pRenderingAreaInfo || !pGranularity ||
        pRenderingAreaInfo->sType != VK_STRUCTURE_TYPE_RENDERING_AREA_INFO)
        return;
    *pGranularity = (VkExtent2D){1u, 1u};
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t f, uint32_t n,
                       const VkBuffer *b, const VkDeviceSize *o) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdBindVertexBuffers");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        f > VK_PS5_MAX_VERTEX_BINDINGS ||
        n > VK_PS5_MAX_VERTEX_BINDINGS - f || (n && (!b || !o))) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        VkPs5Buffer *buffer = (VkPs5Buffer *)b[i];
        if (!buffer || !buffer->memory ||
            !(buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            o[i] >= buffer->size) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        command->vertex_buffers[f + i] = buffer;
        command->vertex_offsets[f + i] = o[i];
    }
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers2(VkCommandBuffer c, uint32_t f, uint32_t n,
                        const VkBuffer *b, const VkDeviceSize *o,
                        const VkDeviceSize *sizes,
                        const VkDeviceSize *strides) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdBindVertexBuffers2");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    /* Dynamic vertex strides belong to VK_EXT_extended_dynamic_state, which
     * is not advertised until every command in that contract is native. */
    if (strides) {
        fprintf(stderr,
            "vulkan-ps5: vkCmdBindVertexBuffers2 rejected dynamic strides "
            "first=%u count=%u\n", f, n);
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    if (f > VK_PS5_MAX_VERTEX_BINDINGS ||
        n > VK_PS5_MAX_VERTEX_BINDINGS - f || (n && (!b || !o))) {
        fprintf(stderr,
            "vulkan-ps5: vkCmdBindVertexBuffers2 rejected arguments "
            "first=%u count=%u buffers=%u offsets=%u\n",
            f, n, b != NULL, o != NULL);
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t i = 0; i < n; ++i) {
        VkPs5Buffer *buffer = (VkPs5Buffer *)b[i];
        if (!buffer || !buffer->memory ||
            !(buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            o[i] >= buffer->size) {
            fprintf(stderr,
                "vulkan-ps5: vkCmdBindVertexBuffers2 rejected binding=%u "
                "buffer=%u memory=%u usage=0x%x offset=%llu size=%llu\n",
                f + i, buffer != NULL,
                buffer && buffer->memory != NULL,
                buffer ? buffer->usage : 0u,
                (unsigned long long)o[i],
                (unsigned long long)(buffer ? buffer->size : 0u));
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        if (sizes) {
            VkDeviceSize resolved_size = sizes[i] == VK_WHOLE_SIZE ?
                buffer->size - o[i] : sizes[i];
            if (!resolved_size || resolved_size > buffer->size - o[i]) {
                fprintf(stderr,
                    "vulkan-ps5: vkCmdBindVertexBuffers2 rejected "
                    "binding=%u range=%llu available=%llu\n",
                    f + i, (unsigned long long)resolved_size,
                    (unsigned long long)(buffer->size - o[i]));
                command->record_error = VK_ERROR_INITIALIZATION_FAILED;
                return;
            }
        }
        command->vertex_buffers[f + i] = buffer;
        command->vertex_offsets[f + i] = o[i];
    }
}

static VkResult native_bind_graphics_attachments(
    VkPs5CommandBuffer *command)
{
    VkPs5RenderPass *render_pass = command->active_render_pass;
    VkPs5Framebuffer *framebuffer = command->active_framebuffer;
    uint32_t subpass = command->active_subpass;
    if (command->native_attachments_render_pass) {
        return command->native_attachments_render_pass == render_pass &&
            command->native_attachments_framebuffer == framebuffer &&
            command->native_attachments_subpass == subpass ?
            VK_SUCCESS : VK_ERROR_FEATURE_NOT_PRESENT;
    }
    AgcColorTargetBinding targets[AGC_GFX1013_MAX_COLOR_TARGETS];
    uint32_t color_count = render_pass->subpasses[subpass].
        color_attachment_count;
    for (uint32_t slot = 0u; slot < color_count; ++slot) {
        uint32_t attachment = render_pass->subpasses[subpass].
            color_attachments[slot];
        VkPs5ImageView *view = attachment < framebuffer->attachment_count ?
            framebuffer->attachments[attachment] : NULL;
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        AgcResourceUsage usage = image ?
            native_image_recorded_usage(command, image) :
            kAgcResourceUsageUndefined;
        if (!image || !image->native_image ||
            usage != kAgcResourceUsageColorTarget) {
            fprintf(stderr,
                "vulkan-ps5: color attachment not renderable slot=%u "
                "attachment=%u image=%u native=%u usage=%u\n",
                slot, attachment, image != NULL,
                image && image->native_image != NULL, usage);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        targets[slot] = (AgcColorTargetBinding)
            AGC_COLOR_TARGET_BINDING_INIT;
        targets[slot].image = image->native_image;
        targets[slot].array_layer = view->base_array_layer;
    }
    int32_t result = agcCmdBindColorTargets(
        command->native_graphics_command_buffer, color_count, targets);
    if (result != AGC_OK) {
        AgcDebugMessage debug_message = AGC_DEBUG_MESSAGE_INIT;
        int32_t debug_result = agcGetLastDebugMessage(
            vk_ps5_native_device(command->device), &debug_message);
        fprintf(stderr,
            "vulkan-ps5: agcCmdBindColorTargets failed result=0x%x "
            "count=%u binding_size=%u binding_version=%u "
            "binding_flags=%u binding_reserved0=%u binding_reserved=%llu "
            "image_usage=0x%x image_format=%u image_depth=%u%s%s\n",
            (unsigned int)result, color_count, targets[0].struct_size,
            targets[0].version, targets[0].flags, targets[0].reserved0,
            (unsigned long long)(targets[0].reserved[0] |
                targets[0].reserved[1] | targets[0].reserved[2] |
                targets[0].reserved[3]),
            color_count ? ((VkPs5Image *)
                ((VkPs5ImageView *)framebuffer->attachments[
                    render_pass->subpasses[subpass].color_attachments[0]])->
                        image)->native_desc.usage : 0u,
            color_count ? ((VkPs5Image *)
                ((VkPs5ImageView *)framebuffer->attachments[
                    render_pass->subpasses[subpass].color_attachments[0]])->
                        image)->native_desc.format : 0u,
            color_count ? ((VkPs5Image *)
                ((VkPs5ImageView *)framebuffer->attachments[
                    render_pass->subpasses[subpass].color_attachments[0]])->
                        image)->native_desc.depth : 0u,
            debug_result == AGC_OK ? " message=" : "",
            debug_result == AGC_OK ? debug_message.message : "");
        return native_command_result(result);
    }
    uint32_t depth_attachment = render_pass->subpasses[subpass].
        depth_stencil_attachment;
    if (depth_attachment != VK_ATTACHMENT_UNUSED) {
        VkPs5ImageView *view = depth_attachment < framebuffer->attachment_count ?
            framebuffer->attachments[depth_attachment] : NULL;
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        AgcResourceUsage usage = image ?
            native_image_recorded_usage(command, image) :
            kAgcResourceUsageUndefined;
        if (!image || !image->native_image ||
            (usage != kAgcResourceUsageDepthStencilRead &&
             usage != kAgcResourceUsageDepthStencilWrite)) {
            fprintf(stderr,
                "vulkan-ps5: depth attachment not renderable attachment=%u "
                "image=%u native=%u usage=%u\n", depth_attachment,
                image != NULL, image && image->native_image != NULL, usage);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        AgcDepthStencilTargetBinding target =
            AGC_DEPTH_STENCIL_TARGET_BINDING_INIT;
        target.image = image->native_image;
        target.array_layer = view->base_array_layer;
        result = agcCmdBindDepthStencilTarget(
            command->native_graphics_command_buffer, &target);
        if (result != AGC_OK) {
            fprintf(stderr,
                "vulkan-ps5: agcCmdBindDepthStencilTarget failed "
                "result=0x%x\n", (unsigned int)result);
            return native_command_result(result);
        }
    }
    command->native_attachments_render_pass = render_pass;
    command->native_attachments_framebuffer = framebuffer;
    command->native_attachments_subpass = subpass;
    return VK_SUCCESS;
}

static VkResult native_bind_graphics_descriptors(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline, bool *ready)
{
    enum { VK_PS5_MAX_NATIVE_DESCRIPTOR_WRITES = 256 };
    AgcDescriptorWrite writes[VK_PS5_MAX_NATIVE_DESCRIPTOR_WRITES];
    uint32_t write_count = 0u;

    *ready = false;
    if (command->native_descriptor_graphics_pipeline) {
        if (command->native_descriptor_graphics_pipeline ==
                pipeline->native_graphics_pipeline &&
            memcmp(command->native_descriptor_graphics_sets,
                   command->graphics_sets,
                   sizeof(command->graphics_sets)) == 0) {
            *ready = true;
            return VK_SUCCESS;
        }
    }
    for (uint32_t stage_index = 0u; stage_index < pipeline->stage_count;
         ++stage_index) {
        const AgcShaderReflection *reflection =
            &pipeline->stages[stage_index].metadata;
        for (uint32_t mapping_index = 0u;
             mapping_index < reflection->descriptor_mapping_count;
             ++mapping_index) {
            const AgcShaderDescriptorMapping *mapping =
                &reflection->descriptor_mappings[mapping_index];
            uint32_t array_size =
                AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size);
            bool duplicate = false;
            for (uint32_t previous_stage = 0u;
                 previous_stage < stage_index && !duplicate;
                 ++previous_stage) {
                const AgcShaderReflection *previous =
                    &pipeline->stages[previous_stage].metadata;
                for (uint32_t previous_mapping = 0u;
                     previous_mapping < previous->descriptor_mapping_count;
                     ++previous_mapping) {
                    const AgcShaderDescriptorMapping *candidate =
                        &previous->descriptor_mappings[previous_mapping];
                    if (candidate->set == mapping->set &&
                        candidate->binding == mapping->binding) {
                        duplicate = true;
                        break;
                    }
                }
            }
            if (duplicate)
                continue;
            if (!array_size || array_size >
                    VK_PS5_MAX_NATIVE_DESCRIPTOR_WRITES - write_count ||
                mapping->set >= OPENAGC_PSBC_MAX_DESCRIPTOR_SETS)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            VkPs5DescriptorSet *set =
                command->graphics_sets[mapping->set];
            if (!set) {
                fprintf(stderr,
                    "vulkan-ps5: draw descriptor set %u is not bound "
                    "for binding %u\n", mapping->set, mapping->binding);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            for (uint32_t array = 0u; array < array_size; ++array) {
                VkDescriptorType layout_type;
                VkPs5DescriptorValue *value = descriptor_value(
                    set, mapping->binding, array, &layout_type);
                OpenAgcPsbcDescriptorType psbc_type;
                AgcDescriptorWrite *write = &writes[write_count];
                if (!value || !value->valid ||
                    !psbc_descriptor_type(layout_type, &psbc_type) ||
                    psbc_type != mapping->type) {
                    fprintf(stderr,
                        "vulkan-ps5: draw descriptor invalid set=%u "
                        "binding=%u array=%u value=%u valid=%u "
                        "layout_type=%u reflected_type=%u\n",
                        mapping->set, mapping->binding, array,
                        value != NULL, value ? value->valid : 0u,
                        value ? (unsigned int)layout_type : UINT32_MAX,
                        mapping->type);
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                *write = (AgcDescriptorWrite)AGC_DESCRIPTOR_WRITE_INIT;
                write->set = mapping->set;
                write->binding = mapping->binding;
                write->array_element = array;
                write->type = (AgcShaderDescriptorType)mapping->type;
                if (mapping->type ==
                        OPENAGC_PSBC_DESCRIPTOR_UNIFORM_BUFFER ||
                    mapping->type ==
                        OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER) {
                    VkPs5Buffer *buffer =
                        (VkPs5Buffer *)value->buffer.buffer;
                    uint64_t range;
                    AgcResourceUsage usage;
                    if (!buffer) {
                        if (!vk_ps5_device_null_descriptor(command->device)) {
                            fprintf(stderr,
                                "vulkan-ps5: draw null buffer descriptor "
                                "rejected set=%u binding=%u array=%u\n",
                                mapping->set, mapping->binding, array);
                            return VK_ERROR_INITIALIZATION_FAILED;
                        }
                        write_count++;
                        continue;
                    }
                    if (!buffer->native_buffer ||
                        value->buffer.offset > buffer->size) {
                        fprintf(stderr,
                            "vulkan-ps5: draw buffer descriptor range base "
                            "invalid set=%u binding=%u native=%u "
                            "offset=%llu size=%llu\n",
                            mapping->set, mapping->binding,
                            buffer->native_buffer != NULL,
                            (unsigned long long)value->buffer.offset,
                            (unsigned long long)buffer->size);
                        return VK_ERROR_INITIALIZATION_FAILED;
                    }
                    range = value->buffer.range == VK_WHOLE_SIZE ?
                        buffer->size - value->buffer.offset :
                        value->buffer.range;
                    if (!range ||
                        range > buffer->size - value->buffer.offset) {
                        fprintf(stderr,
                            "vulkan-ps5: draw buffer descriptor range "
                            "invalid set=%u binding=%u range=%llu "
                            "available=%llu\n",
                            mapping->set, mapping->binding,
                            (unsigned long long)range,
                            (unsigned long long)(buffer->size -
                                value->buffer.offset));
                        return VK_ERROR_INITIALIZATION_FAILED;
                    }
                    usage = native_buffer_recorded_usage(command, buffer);
                    if (usage != kAgcResourceUsageShaderRead &&
                        !(mapping->type ==
                              OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER &&
                          usage == kAgcResourceUsageShaderWrite))
                        return VK_SUCCESS;
                    write->buffer = buffer->native_buffer;
                    write->buffer_offset = value->buffer.offset;
                    write->buffer_range = range;
                } else if (mapping->type ==
                               OPENAGC_PSBC_DESCRIPTOR_SAMPLER) {
                    VkPs5Sampler *sampler =
                        (VkPs5Sampler *)value->image.sampler;
                    if (!sampler) {
                        if (!vk_ps5_device_null_descriptor(command->device)) {
                            fprintf(stderr,
                                "vulkan-ps5: draw null sampler descriptor "
                                "rejected set=%u binding=%u array=%u\n",
                                mapping->set, mapping->binding, array);
                            return VK_ERROR_INITIALIZATION_FAILED;
                        }
                        write_count++;
                        continue;
                    }
                    if (!sampler->native_sampler) {
                        fprintf(stderr,
                            "vulkan-ps5: draw sampler descriptor has no "
                            "native object set=%u binding=%u array=%u\n",
                            mapping->set, mapping->binding, array);
                        return VK_ERROR_INITIALIZATION_FAILED;
                    }
                    write->sampler = sampler->native_sampler;
                } else if (mapping->type ==
                               OPENAGC_PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER ||
                           mapping->type ==
                               OPENAGC_PSBC_DESCRIPTOR_SAMPLED_IMAGE ||
                           mapping->type ==
                               OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE ||
                           mapping->type ==
                               OPENAGC_PSBC_DESCRIPTOR_INPUT_ATTACHMENT) {
                    VkPs5ImageView *view =
                        (VkPs5ImageView *)value->image.imageView;
                    VkPs5Image *image =
                        view ? (VkPs5Image *)view->image : NULL;
                    AgcResourceUsage usage = image ?
                        native_image_recorded_usage(command, image) :
                        kAgcResourceUsageUndefined;
                    if (view) {
                        VkResult view_result = ensure_native_image_view(view);
                        if (view_result != VK_SUCCESS) {
                            fprintf(stderr,
                                "vulkan-ps5: draw image-view realization "
                                "failed set=%u binding=%u array=%u "
                                "format=%u result=%d\n",
                                mapping->set, mapping->binding, array,
                                view->format, view_result);
                            return view_result;
                        }
                        if (!view->native_view) {
                            fprintf(stderr,
                                "vulkan-ps5: draw image view has no native "
                                "object set=%u binding=%u array=%u format=%u\n",
                                mapping->set, mapping->binding, array,
                                view->format);
                            return VK_ERROR_INITIALIZATION_FAILED;
                        }
                        if (usage != kAgcResourceUsageShaderRead &&
                            !(mapping->type ==
                                  OPENAGC_PSBC_DESCRIPTOR_STORAGE_IMAGE &&
                              usage == kAgcResourceUsageShaderWrite))
                            return VK_SUCCESS;
                        write->image_view = view->native_view;
                    } else if (!vk_ps5_device_null_descriptor(
                                   command->device)) {
                        fprintf(stderr,
                            "vulkan-ps5: draw null image descriptor rejected "
                            "set=%u binding=%u array=%u\n",
                            mapping->set, mapping->binding, array);
                        return VK_ERROR_INITIALIZATION_FAILED;
                    }
                    if (mapping->type ==
                        OPENAGC_PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER) {
                        VkPs5Sampler *sampler =
                            (VkPs5Sampler *)value->image.sampler;
                        if (sampler && !sampler->native_sampler) {
                            fprintf(stderr,
                                "vulkan-ps5: draw sampler has no native "
                                "object set=%u binding=%u array=%u\n",
                                mapping->set, mapping->binding, array);
                            return VK_ERROR_INITIALIZATION_FAILED;
                        }
                        if (sampler)
                            write->sampler = sampler->native_sampler;
                        else if (!vk_ps5_device_null_descriptor(
                                     command->device)) {
                            fprintf(stderr,
                                "vulkan-ps5: draw null sampler rejected "
                                "set=%u binding=%u array=%u\n",
                                mapping->set, mapping->binding, array);
                            return VK_ERROR_INITIALIZATION_FAILED;
                        }
                    }
                } else {
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
                write_count++;
            }
        }
    }
    if (write_count) {
        int32_t result = agcCmdBindDescriptors(
            command->native_graphics_command_buffer, write_count, writes);
        if (result != AGC_OK)
            return native_command_result(result);
        command->native_descriptor_bind_count++;
    }
    command->native_descriptor_graphics_pipeline =
        pipeline->native_graphics_pipeline;
    memcpy(command->native_descriptor_graphics_sets,
           command->graphics_sets, sizeof(command->graphics_sets));
    *ready = true;
    return VK_SUCCESS;
}

static VkResult native_bind_graphics_vertex_buffers(
    VkPs5CommandBuffer *command, const VkPs5Pipeline *pipeline, bool *ready)
{
    AgcVertexBufferBinding bindings[VK_PS5_MAX_VERTEX_BINDINGS];
    uint32_t binding_count = 0u;
    *ready = false;
    if (command->native_vertex_graphics_pipeline) {
        if (command->native_vertex_graphics_pipeline ==
                pipeline->native_graphics_pipeline &&
            memcmp(command->native_vertex_buffers,
                   command->vertex_buffers,
                   sizeof(command->vertex_buffers)) == 0 &&
            memcmp(command->native_vertex_offsets,
                   command->vertex_offsets,
                   sizeof(command->vertex_offsets)) == 0) {
            *ready = true;
            return VK_SUCCESS;
        }
    }
    for (uint32_t binding = 0u; binding < VK_PS5_MAX_VERTEX_BINDINGS;
         ++binding) {
        if ((pipeline->vertex_binding_mask & (1u << binding)) == 0u)
            continue;
        VkPs5Buffer *buffer = command->vertex_buffers[binding];
        if (!buffer || !buffer->native_buffer) {
            fprintf(stderr,
                "vulkan-ps5: draw missing vertex binding=%u required_mask=0x%x\n",
                binding, pipeline->vertex_binding_mask);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (native_buffer_recorded_usage(command, buffer) !=
                kAgcResourceUsageShaderRead) {
            VkResult prepare = native_prepare_buffer_range(command, buffer,
                0u, buffer->size, kAgcResourceUsageShaderRead);
            if (prepare != VK_SUCCESS) {
                fprintf(stderr,
                    "vulkan-ps5: draw vertex binding=%u transition failed "
                    "result=%d required_mask=0x%x\n",
                    binding, prepare, pipeline->vertex_binding_mask);
                return prepare;
            }
        }
        bindings[binding_count] = (AgcVertexBufferBinding)
            AGC_VERTEX_BUFFER_BINDING_INIT;
        bindings[binding_count].binding = binding;
        bindings[binding_count].buffer = buffer->native_buffer;
        bindings[binding_count].offset = command->vertex_offsets[binding];
        bindings[binding_count].stride = pipeline->vertex_strides[binding];
        binding_count++;
    }
    if (binding_count) {
        int32_t result = agcCmdBindVertexBuffers(
            command->native_graphics_command_buffer,
            binding_count, bindings);
        if (result != AGC_OK) {
            fprintf(stderr,
                "vulkan-ps5: agcCmdBindVertexBuffers failed result=0x%x "
                "count=%u required_mask=0x%x\n",
                (unsigned int)result, binding_count,
                pipeline->vertex_binding_mask);
            return native_command_result(result);
        }
    }
    command->native_vertex_graphics_pipeline =
        pipeline->native_graphics_pipeline;
    memcpy(command->native_vertex_buffers, command->vertex_buffers,
           sizeof(command->vertex_buffers));
    memcpy(command->native_vertex_offsets, command->vertex_offsets,
           sizeof(command->vertex_offsets));
    *ready = true;
    return VK_SUCCESS;
}

static VkResult native_bind_graphics_viewport_state(
    VkPs5CommandBuffer *command,
    const AgcGfx1013ViewportArrayState *source)
{
    AgcViewport viewports[VK_PS5_MAX_VIEWPORTS];
    AgcScissor scissors[VK_PS5_MAX_VIEWPORTS];

    if (!command || !source || source->count == 0u ||
        source->count > VK_PS5_MAX_VIEWPORTS ||
        source->count > AGC_RUNTIME_MAX_VIEWPORTS)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0u; i < source->count; ++i) {
        const AgcGfx1013Viewport *viewport = &source->viewports[i];
        const AgcGfx1013ScissorState *scissor = &source->scissors[i];
        viewports[i] = (AgcViewport)AGC_VIEWPORT_INIT;
        viewports[i].x = viewport->x;
        viewports[i].y = viewport->y;
        viewports[i].width = viewport->width;
        viewports[i].height = viewport->height;
        viewports[i].min_depth = viewport->min_depth;
        viewports[i].max_depth = viewport->max_depth;
        scissors[i] = (AgcScissor)AGC_SCISSOR_INIT;
        scissors[i].x = (int32_t)scissor->left;
        scissors[i].y = (int32_t)scissor->top;
        scissors[i].width = scissor->right - scissor->left;
        scissors[i].height = scissor->bottom - scissor->top;
    }
    return native_command_result(agcCmdSetViewportScissors(
        command->native_graphics_command_buffer, source->count,
        viewports, scissors));
}

static void record_graphics_draw(
    VkPs5CommandBuffer *command, uint32_t element_count,
    uint32_t instance_count, uint32_t first_element, int32_t vertex_offset,
    uint32_t first_instance, bool indexed)
{
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    VkPs5Pipeline *pipeline = command->bound_graphics;
    bool baseline = pipeline && pipeline->stage_count == 2u &&
        (pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_VERTEX ||
         pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_GEOMETRY) &&
        pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_FRAGMENT;
    bool tessellation = pipeline && pipeline->stage_count == 3u &&
        pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_TESS_CONTROL &&
        (pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_TESS_EVALUATION ||
         pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_GEOMETRY) &&
        pipeline->stage_types[2] == OPENAGC_PSBC_STAGE_FRAGMENT;
    if (!pipeline || !command->active_render_pass ||
        (!baseline && !tessellation) || !element_count || !instance_count) {
        fprintf(stderr,
            "vulkan-ps5: draw rejected pipeline=%u render_pass=%u "
            "stages=%u baseline=%u tessellation=%u elements=%u instances=%u\n",
            pipeline != NULL, command->active_render_pass != NULL,
            pipeline ? pipeline->stage_count : 0u, baseline, tessellation,
            element_count, instance_count);
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    if (pipeline->dynamic_rendering != command->active_dynamic_rendering) {
        fprintf(stderr,
            "vulkan-ps5: draw dynamic-rendering mismatch pipeline=%u active=%u\n",
            pipeline->dynamic_rendering, command->active_dynamic_rendering);
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (command->active_dynamic_rendering) {
        VkPs5RenderPass *render_pass = command->active_render_pass;
        VkPs5Framebuffer *framebuffer = command->active_framebuffer;
        if (pipeline->dynamic_color_attachment_count !=
                render_pass->subpasses[0].color_attachment_count) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        for (uint32_t slot = 0;
             slot < pipeline->dynamic_color_attachment_count; ++slot) {
            uint32_t attachment =
                render_pass->subpasses[0].color_attachments[slot];
            if (attachment >= framebuffer->attachment_count ||
                !framebuffer->attachments[attachment] ||
                pipeline->dynamic_color_formats[slot] !=
                    framebuffer->attachments[attachment]->format) {
                command->record_error = VK_ERROR_INITIALIZATION_FAILED;
                return;
            }
        }
        uint32_t depth_attachment =
            render_pass->subpasses[0].depth_stencil_attachment;
        VkFormat depth_format = depth_attachment == VK_ATTACHMENT_UNUSED ?
            VK_FORMAT_UNDEFINED :
            framebuffer->attachments[depth_attachment]->format;
        if ((pipeline->dynamic_depth_format != VK_FORMAT_UNDEFINED &&
             pipeline->dynamic_depth_format != depth_format) ||
            (pipeline->dynamic_stencil_format != VK_FORMAT_UNDEFINED &&
             pipeline->dynamic_stencil_format != depth_format)) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
    }
    AgcGfx1013ViewportArrayState native_viewport_state;
    VkResult viewport_result = resolve_viewport_state(
        command, pipeline, &native_viewport_state);
    if (viewport_result != VK_SUCCESS) {
        fprintf(stderr,
            "vulkan-ps5: draw viewport resolution failed result=%d\n",
            viewport_result);
        command->record_error = viewport_result;
        return;
    }
    if (!pipeline->native_graphics_pipeline ||
        command->native_bound_graphics !=
            pipeline->native_graphics_pipeline) {
        fprintf(stderr,
            "vulkan-ps5: draw native pipeline not bound native=%u match=%u\n",
            pipeline->native_graphics_pipeline != NULL,
            command->native_bound_graphics ==
                pipeline->native_graphics_pipeline);
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    bool descriptors_ready;
    bool vertex_buffers_ready;
    VkResult native_result = native_replay_push_constants(command, pipeline);
    if (native_result != VK_SUCCESS)
        fprintf(stderr,
            "vulkan-ps5: draw push-constant replay failed result=%d\n",
            native_result);
    if (native_result == VK_SUCCESS) {
        native_result = native_bind_graphics_descriptors(
            command, pipeline, &descriptors_ready);
        if (native_result != VK_SUCCESS)
            fprintf(stderr,
                "vulkan-ps5: draw descriptor preparation failed result=%d\n",
                native_result);
    } else {
        descriptors_ready = false;
    }
    if (native_result == VK_SUCCESS && descriptors_ready)
        native_result = native_bind_graphics_vertex_buffers(
            command, pipeline, &vertex_buffers_ready);
    else
        vertex_buffers_ready = false;
    if (native_result == VK_SUCCESS && descriptors_ready &&
        vertex_buffers_ready) {
        native_result = native_bind_graphics_attachments(command);
        if (native_result != VK_SUCCESS)
            fprintf(stderr,
                "vulkan-ps5: draw attachment preparation failed result=%d\n",
                native_result);
    }
    if (native_result == VK_SUCCESS && descriptors_ready &&
        vertex_buffers_ready) {
        native_result = native_bind_graphics_viewport_state(
            command, &native_viewport_state);
        if (native_result != VK_SUCCESS)
            fprintf(stderr,
                "vulkan-ps5: draw viewport emission failed result=%d\n",
                native_result);
    }
    if (native_result != VK_SUCCESS) {
        fprintf(stderr,
            "vulkan-ps5: draw native preparation failed result=%d "
            "descriptors=%u vertex_buffers=%u\n",
            native_result, descriptors_ready, vertex_buffers_ready);
        command->record_error = native_result;
        return;
    }
    if (!descriptors_ready || !vertex_buffers_ready) {
        fprintf(stderr,
            "vulkan-ps5: draw bindings not ready descriptors=%u "
            "vertex_buffers=%u\n",
            descriptors_ready, vertex_buffers_ready);
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    int32_t result;
    if (indexed) {
        VkPs5Buffer *index = command->index_buffer;
        if (!index || !index->native_buffer) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        if (native_buffer_recorded_usage(command, index) !=
                kAgcResourceUsageShaderRead) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        AgcIndexSize index_size = command->index_type ==
            VK_INDEX_TYPE_UINT32 ? kAgcIndexSize32 : kAgcIndexSize16;
        result = agcCmdBindIndexBuffer(
            command->native_graphics_command_buffer,
            index->native_buffer, command->index_offset, index_size);
        if (result == AGC_OK)
            result = agcCmdDrawIndexed(
                command->native_graphics_command_buffer, element_count,
                instance_count, first_element, vertex_offset,
                first_instance);
    } else {
        result = agcCmdDraw(
            command->native_graphics_command_buffer,
            element_count, instance_count, first_element, first_instance);
    }
    if (result != AGC_OK) {
        fprintf(stderr,
            "vulkan-ps5: native draw failed indexed=%u result=0x%x\n",
            indexed, (unsigned int)result);
        command->record_error = native_command_result(result);
        return;
    }
    command->native_draw_count++;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) {
    debug_note_command((VkPs5CommandBuffer *)c, "vkCmdDraw");
    record_graphics_draw((VkPs5CommandBuffer *)c, v, i, fv, 0, fi, false);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexed(VkCommandBuffer c, uint32_t i, uint32_t n, uint32_t f,
                 int32_t v, uint32_t fi) {
    record_graphics_draw((VkPs5CommandBuffer *)c, i, n, f, v, fi, true);
}

static void record_graphics_indirect(
    VkPs5CommandBuffer *command, VkPs5Buffer *arguments,
    VkDeviceSize offset, uint32_t draw_count, uint32_t stride, bool indexed)
{
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (draw_count == 0u)
        return;
    VkPs5Pipeline *pipeline = command->bound_graphics;
    bool baseline = pipeline && pipeline->stage_count == 2u &&
        (pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_VERTEX ||
         pipeline->stage_types[0] == OPENAGC_PSBC_STAGE_GEOMETRY) &&
        pipeline->stage_types[1] == OPENAGC_PSBC_STAGE_FRAGMENT;
    uint32_t argument_size = indexed ?
        (uint32_t)sizeof(VkDrawIndexedIndirectCommand) :
        (uint32_t)sizeof(VkDrawIndirectCommand);
    if (!baseline || !command->active_render_pass || !arguments ||
        !arguments->memory ||
        !(arguments->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ||
        (offset & 3u) != 0u ||
        stride < argument_size || (stride & 3u) != 0u) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }
    VkDeviceSize last_offset = offset;
    if (draw_count > 1u) {
        uint64_t extra_count = (uint64_t)draw_count - 1u;
        if (extra_count > (UINT64_MAX - last_offset) / stride) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        last_offset += extra_count * stride;
    }
    if (last_offset > arguments->size ||
        argument_size > arguments->size - last_offset ||
        arguments->memory_offset > UINT64_MAX - offset) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    AgcGfx1013ViewportArrayState native_viewport_state;
    VkResult viewport_result = resolve_viewport_state(
        command, pipeline, &native_viewport_state);
    if (viewport_result != VK_SUCCESS) {
        command->record_error = viewport_result;
        return;
    }
    if (!native_require_complete_stream(command))
        return;

    if (!pipeline->native_graphics_pipeline ||
        command->native_bound_graphics != pipeline->native_graphics_pipeline ||
        !arguments->native_buffer ||
        native_buffer_recorded_usage(command, arguments) !=
            kAgcResourceUsageShaderRead) {
        native_mark_stream_incomplete(command);
        return;
    }
    bool descriptors_ready;
    bool vertex_buffers_ready;
    VkResult native_result = native_replay_push_constants(command, pipeline);
    if (native_result == VK_SUCCESS)
        native_result = native_bind_graphics_descriptors(
            command, pipeline, &descriptors_ready);
    else
        descriptors_ready = false;
    if (native_result == VK_SUCCESS && descriptors_ready)
        native_result = native_bind_graphics_vertex_buffers(
            command, pipeline, &vertex_buffers_ready);
    else
        vertex_buffers_ready = false;
    if (native_result == VK_SUCCESS && descriptors_ready &&
        vertex_buffers_ready)
        native_result = native_bind_graphics_attachments(command);
    if (native_result == VK_SUCCESS && descriptors_ready &&
        vertex_buffers_ready)
        native_result = native_bind_graphics_viewport_state(
            command, &native_viewport_state);
    if (native_result != VK_SUCCESS) {
        command->record_error = native_result;
        return;
    }
    if (!descriptors_ready || !vertex_buffers_ready) {
        native_mark_stream_incomplete(command);
        return;
    }
    int32_t native_draw_result;
    if (indexed) {
        VkPs5Buffer *index = command->index_buffer;
        if (!index || !index->native_buffer ||
            native_buffer_recorded_usage(command, index) !=
                kAgcResourceUsageShaderRead) {
            native_mark_stream_incomplete(command);
            return;
        }
        AgcIndexSize index_size = command->index_type ==
            VK_INDEX_TYPE_UINT32 ? kAgcIndexSize32 : kAgcIndexSize16;
        native_draw_result = agcCmdBindIndexBuffer(
            command->native_graphics_command_buffer,
            index->native_buffer, command->index_offset, index_size);
        if (native_draw_result == AGC_OK)
            native_draw_result = agcCmdDrawIndexedIndirect(
                command->native_graphics_command_buffer,
                arguments->native_buffer, offset, draw_count, stride);
    } else {
        native_draw_result = agcCmdDrawIndirect(
            command->native_graphics_command_buffer,
            arguments->native_buffer, offset, draw_count, stride);
    }
    if (native_draw_result != AGC_OK) {
        command->record_error = native_command_result(native_draw_result);
        return;
    }
    command->native_draw_count += draw_count;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, uint32_t n, uint32_t s) {
    record_graphics_indirect((VkPs5CommandBuffer *)c, (VkPs5Buffer *)b,
        o, n, s, false);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexedIndirect(VkCommandBuffer c, VkBuffer b, VkDeviceSize o,
                         uint32_t n, uint32_t s) {
    record_graphics_indirect((VkPs5CommandBuffer *)c, (VkPs5Buffer *)b,
        o, n, s, true);
}
static void reject_indirect_count(VkCommandBuffer c) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (command && command->state == VK_PS5_COMMAND_RECORDING &&
        command->record_error == VK_SUCCESS)
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndirectCount(VkCommandBuffer c, VkBuffer b, VkDeviceSize o,
                       VkBuffer count, VkDeviceSize count_offset,
                       uint32_t max_count, uint32_t stride) {
    (void)b; (void)o; (void)count; (void)count_offset;
    (void)max_count; (void)stride;
    reject_indirect_count(c);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexedIndirectCount(VkCommandBuffer c, VkBuffer b, VkDeviceSize o,
                              VkBuffer count, VkDeviceSize count_offset,
                              uint32_t max_count, uint32_t stride) {
    (void)b; (void)o; (void)count; (void)count_offset;
    (void)max_count; (void)stride;
    reject_indirect_count(c);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
               VkImageLayout dl, uint32_t n, const VkImageBlit *r, VkFilter f) {
    IGNORE(s); IGNORE(sl); IGNORE(d); IGNORE(dl); IGNORE(n); IGNORE(r); IGNORE(f);
    reject_unsupported_command(c);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearDepthStencilImage(VkCommandBuffer c, VkImage i, VkImageLayout l,
                            const VkClearDepthStencilValue *v, uint32_t n,
                            const VkImageSubresourceRange *r) {
    IGNORE(i); IGNORE(l); IGNORE(v); IGNORE(n); IGNORE(r);
    reject_unsupported_command(c);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearAttachments(VkCommandBuffer c, uint32_t n, const VkClearAttachment *a,
                      uint32_t rn, const VkClearRect *r) {
    IGNORE(n); IGNORE(a); IGNORE(rn); IGNORE(r);
    reject_unsupported_command(c);
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResolveImage(VkCommandBuffer c, VkImage s, VkImageLayout sl, VkImage d,
                  VkImageLayout dl, uint32_t n, const VkImageResolve *r) {
    IGNORE(s); IGNORE(sl); IGNORE(d); IGNORE(dl); IGNORE(n); IGNORE(r);
    reject_unsupported_command(c);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginRendering(VkCommandBuffer c, const VkRenderingInfo *info)
{
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdBeginRendering");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!info || info->sType != VK_STRUCTURE_TYPE_RENDERING_INFO ||
        info->pNext || info->flags || info->layerCount != 1u ||
        info->viewMask || !info->colorAttachmentCount ||
        info->colorAttachmentCount > AGC_GFX1013_MAX_COLOR_TARGETS ||
        !info->pColorAttachments || command->active_render_pass ||
        info->renderArea.offset.x < 0 || info->renderArea.offset.y < 0 ||
        !info->renderArea.extent.width || !info->renderArea.extent.height) {
        command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }

    VkPs5RenderPass *render_pass = &command->dynamic_render_pass;
    VkPs5Framebuffer *framebuffer = &command->dynamic_framebuffer;
    memset(render_pass, 0, sizeof(*render_pass));
    memset(framebuffer, 0, sizeof(*framebuffer));
    render_pass->subpass_count = 1u;
    render_pass->subpasses[0].color_attachment_count =
        info->colorAttachmentCount;
    render_pass->subpasses[0].depth_stencil_attachment =
        VK_ATTACHMENT_UNUSED;
    render_pass->subpasses[0].samples = VK_SAMPLE_COUNT_1_BIT;
    framebuffer->render_pass = render_pass;
    framebuffer->layers = 1u;

    VkSampleCountFlagBits samples = 0;
    uint32_t attachment_count = 0u;
    VkClearValue clear_values[VK_PS5_MAX_RENDER_ATTACHMENTS] = {{0}};
    for (uint32_t slot = 0; slot < info->colorAttachmentCount; ++slot) {
        const VkRenderingAttachmentInfo *attachment =
            &info->pColorAttachments[slot];
        VkPs5ImageView *view = (VkPs5ImageView *)attachment->imageView;
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        AgcGfx1013ColorTargetFormat target_format;
        AgcGfx1013ResourceUsage usage;
        if (attachment->sType !=
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO ||
            attachment->pNext || !view || !image ||
            attachment->resolveMode != VK_RESOLVE_MODE_NONE ||
            attachment->resolveImageView ||
            (attachment->loadOp != VK_ATTACHMENT_LOAD_OP_LOAD &&
             attachment->loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR &&
             attachment->loadOp != VK_ATTACHMENT_LOAD_OP_DONT_CARE) ||
            (attachment->storeOp != VK_ATTACHMENT_STORE_OP_STORE &&
             attachment->storeOp != VK_ATTACHMENT_STORE_OP_DONT_CARE) ||
            !color_target_format(view->format, &target_format) ||
            !layout_resource_usage(attachment->imageLayout, &usage)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        if (!samples)
            samples = image->samples;
        else if (samples != image->samples) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        render_pass->attachments[attachment_count] = (VkAttachmentDescription){
            .format = view->format,
            .samples = image->samples,
            .loadOp = attachment->loadOp,
            .storeOp = attachment->storeOp,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = attachment->imageLayout,
            .finalLayout = attachment->imageLayout,
        };
        render_pass->subpasses[0].color_attachments[slot] = attachment_count;
        framebuffer->attachments[attachment_count] = view;
        clear_values[attachment_count] = attachment->clearValue;
        if (!framebuffer->width) {
            framebuffer->width = image->extent.width;
            framebuffer->height = image->extent.height;
        }
        ++attachment_count;
    }

    const VkRenderingAttachmentInfo *depth = info->pDepthAttachment;
    const VkRenderingAttachmentInfo *stencil = info->pStencilAttachment;
    if (depth || stencil) {
        const VkRenderingAttachmentInfo *attachment = depth ? depth : stencil;
        if (depth && stencil &&
            (depth->imageView != stencil->imageView ||
             depth->imageLayout != stencil->imageLayout)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        VkPs5ImageView *view = (VkPs5ImageView *)attachment->imageView;
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        AgcGfx1013DepthSurfaceFormat depth_format;
        AgcGfx1013ResourceUsage usage;
        if (!view || !image || attachment_count >=
                VK_PS5_MAX_RENDER_ATTACHMENTS ||
            (depth && (depth->sType !=
                    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO ||
                depth->pNext || depth->resolveMode != VK_RESOLVE_MODE_NONE ||
                depth->resolveImageView ||
                (depth->loadOp != VK_ATTACHMENT_LOAD_OP_LOAD &&
                 depth->loadOp != VK_ATTACHMENT_LOAD_OP_DONT_CARE))) ||
            (stencil && (stencil->sType !=
                    VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO ||
                stencil->pNext || stencil->resolveMode != VK_RESOLVE_MODE_NONE ||
                stencil->resolveImageView ||
                (stencil->loadOp != VK_ATTACHMENT_LOAD_OP_LOAD &&
                 stencil->loadOp != VK_ATTACHMENT_LOAD_OP_DONT_CARE))) ||
            !depth_surface_format(view->format, &depth_format) ||
            !layout_resource_usage(attachment->imageLayout, &usage) ||
            (samples && samples != image->samples)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        render_pass->attachments[attachment_count] = (VkAttachmentDescription){
            .format = view->format,
            .samples = image->samples,
            .loadOp = depth ? depth->loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = depth ? depth->storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = stencil ? stencil->loadOp :
                VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = stencil ? stencil->storeOp :
                VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = attachment->imageLayout,
            .finalLayout = attachment->imageLayout,
        };
        render_pass->subpasses[0].depth_stencil_attachment = attachment_count;
        render_pass->subpasses[0].depth_stencil_layout =
            attachment->imageLayout;
        framebuffer->attachments[attachment_count] = view;
        ++attachment_count;
    }
    render_pass->attachment_count = attachment_count;
    render_pass->subpasses[0].samples = samples ? samples :
        VK_SAMPLE_COUNT_1_BIT;
    framebuffer->attachment_count = attachment_count;

    const VkRenderPassBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = (VkRenderPass)render_pass,
        .framebuffer = (VkFramebuffer)framebuffer,
        .renderArea = info->renderArea,
        .clearValueCount = attachment_count,
        .pClearValues = clear_values,
    };
    vkCmdBeginRenderPass(c, &begin, VK_SUBPASS_CONTENTS_INLINE);
    if (command->record_error == VK_SUCCESS)
        command->active_dynamic_rendering = VK_TRUE;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *b,
                     VkSubpassContents s) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!b || b->sType != VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO ||
        !b->renderPass || !b->framebuffer || command->active_render_pass ||
        s != VK_SUBPASS_CONTENTS_INLINE) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    VkPs5RenderPass *render_pass = (VkPs5RenderPass *)b->renderPass;
    VkPs5Framebuffer *framebuffer = (VkPs5Framebuffer *)b->framebuffer;
    uint32_t color_count = render_pass->subpasses[0].color_attachment_count;
    if (framebuffer->render_pass != render_pass || !render_pass->subpass_count ||
        !color_count || color_count > AGC_GFX1013_MAX_COLOR_TARGETS ||
        b->renderArea.offset.x < 0 || b->renderArea.offset.y < 0 ||
        !b->renderArea.extent.width || !b->renderArea.extent.height ||
        (uint32_t)b->renderArea.offset.x > framebuffer->width ||
        b->renderArea.extent.width >
            framebuffer->width - (uint32_t)b->renderArea.offset.x ||
        (uint32_t)b->renderArea.offset.y > framebuffer->height ||
        b->renderArea.extent.height >
            framebuffer->height - (uint32_t)b->renderArea.offset.y) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    for (uint32_t slot = 0u; slot < color_count; ++slot) {
        uint32_t attachment_index =
            render_pass->subpasses[0].color_attachments[slot];
        if (attachment_index == VK_ATTACHMENT_UNUSED ||
            attachment_index >= framebuffer->attachment_count) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        const VkAttachmentDescription *attachment =
            &render_pass->attachments[attachment_index];
        VkPs5ImageView *view = framebuffer->attachments[attachment_index];
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        AgcResourceUsage declared_before;
        if (!image || !image->native_image ||
            !(image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            !native_usage_from_layout(
                attachment->initialLayout, &declared_before)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        AgcResourceUsage before = native_image_recorded_usage(command, image);
        (void)declared_before;
        VkResult native_result;
        if (attachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            if (!b->pClearValues || attachment_index >= b->clearValueCount) {
                command->record_error = VK_ERROR_INITIALIZATION_FAILED;
                return;
            }
            native_result = native_clear_color_attachment(command, view,
                &b->renderArea, &b->pClearValues[attachment_index].color);
        } else {
            native_result = native_transition_whole_image(
                command, image, before, kAgcResourceUsageColorTarget);
        }
        if (native_result != VK_SUCCESS) {
            command->record_error = native_result;
            return;
        }
    }
    uint32_t depth_index =
        render_pass->subpasses[0].depth_stencil_attachment;
    if (depth_index != VK_ATTACHMENT_UNUSED) {
        if (depth_index >= framebuffer->attachment_count) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        const VkAttachmentDescription *attachment =
            &render_pass->attachments[depth_index];
        VkPs5ImageView *view = framebuffer->attachments[depth_index];
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        AgcResourceUsage declared_before;
        AgcResourceUsage after = render_pass->subpasses[0].
            depth_stencil_layout ==
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ?
            kAgcResourceUsageDepthStencilRead :
            kAgcResourceUsageDepthStencilWrite;
        if (attachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
            attachment->stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
            !image || !image->native_image || !image->is_depth_surface ||
            !(image->usage &
              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
            !native_usage_from_layout(
                attachment->initialLayout, &declared_before)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        AgcResourceUsage before = native_image_recorded_usage(command, image);
        (void)declared_before;
        VkResult native_result = native_transition_whole_image(
            command, image, before, after);
        if (native_result != VK_SUCCESS) {
            command->record_error = native_result;
            return;
        }
    }
    command->active_render_pass = render_pass;
    command->active_framebuffer = framebuffer;
    command->active_subpass = 0u;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdNextSubpass(VkCommandBuffer c, VkSubpassContents s) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!command->active_render_pass || s != VK_SUBPASS_CONTENTS_INLINE ||
        command->active_subpass + 1u >= command->active_render_pass->subpass_count) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
}
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndRenderPass(VkCommandBuffer c) {
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!command->active_render_pass || !command->active_framebuffer ||
        command->active_dynamic_rendering) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    if (command->active_query_pool) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    uint32_t color_count = command->active_render_pass->
        subpasses[command->active_subpass].color_attachment_count;
    for (uint32_t slot = 0; slot < color_count; ++slot) {
        uint32_t attachment_index = command->active_render_pass->
            subpasses[command->active_subpass].color_attachments[slot];
        const VkAttachmentDescription *attachment =
            &command->active_render_pass->attachments[attachment_index];
        AgcResourceUsage native_after;
        if (!native_usage_from_layout(
                attachment->finalLayout, &native_after)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        VkPs5ImageView *view = command->active_framebuffer->attachments[
            attachment_index];
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        if (!image) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        if (native_image_supports_usage(image, native_after)) {
            VkResult native_result = native_transition_whole_image(command,
                image, kAgcResourceUsageColorTarget, native_after);
            if (native_result != VK_SUCCESS) {
                command->record_error = native_result;
                return;
            }
        }
    }
    uint32_t depth_index = command->active_render_pass->
        subpasses[command->active_subpass].depth_stencil_attachment;
    if (depth_index != VK_ATTACHMENT_UNUSED) {
        const VkAttachmentDescription *depth_attachment =
            &command->active_render_pass->attachments[depth_index];
        AgcResourceUsage native_after;
        AgcResourceUsage native_before = command->active_render_pass->
            subpasses[command->active_subpass].depth_stencil_layout ==
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ?
            kAgcResourceUsageDepthStencilRead :
            kAgcResourceUsageDepthStencilWrite;
        if (!native_usage_from_layout(
                depth_attachment->finalLayout, &native_after)) {
            command->record_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        VkPs5ImageView *view = command->active_framebuffer->attachments[
            depth_index];
        VkPs5Image *image = view ? (VkPs5Image *)view->image : NULL;
        if (!image) {
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
            return;
        }
        if (native_image_supports_usage(image, native_after)) {
            VkResult native_result = native_transition_whole_image(command,
                image, native_before, native_after);
            if (native_result != VK_SUCCESS) {
                command->record_error = native_result;
                return;
            }
        }
    }
    command->active_render_pass = NULL;
    command->active_framebuffer = NULL;
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndRendering(VkCommandBuffer c)
{
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)c;
    debug_note_command(command, "vkCmdEndRendering");
    if (!command || command->state != VK_PS5_COMMAND_RECORDING ||
        command->record_error != VK_SUCCESS)
        return;
    if (!command->active_dynamic_rendering) {
        command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    command->active_dynamic_rendering = VK_FALSE;
    vkCmdEndRenderPass(c);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                      const VkRenderPassBeginInfo *pRenderPassBegin,
                      const VkSubpassBeginInfo *pSubpassBeginInfo)
{
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || !pSubpassBeginInfo ||
        pSubpassBeginInfo->sType != VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO ||
        pSubpassBeginInfo->pNext) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin,
                         pSubpassBeginInfo->contents);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdNextSubpass2(VkCommandBuffer commandBuffer,
                  const VkSubpassBeginInfo *pSubpassBeginInfo,
                  const VkSubpassEndInfo *pSubpassEndInfo)
{
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || !pSubpassBeginInfo || !pSubpassEndInfo ||
        pSubpassBeginInfo->sType != VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO ||
        pSubpassBeginInfo->pNext ||
        pSubpassEndInfo->sType != VK_STRUCTURE_TYPE_SUBPASS_END_INFO ||
        pSubpassEndInfo->pNext) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    vkCmdNextSubpass(commandBuffer, pSubpassBeginInfo->contents);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndRenderPass2(VkCommandBuffer commandBuffer,
                    const VkSubpassEndInfo *pSubpassEndInfo)
{
    VkPs5CommandBuffer *command = (VkPs5CommandBuffer *)commandBuffer;
    if (!command || !pSubpassEndInfo ||
        pSubpassEndInfo->sType != VK_STRUCTURE_TYPE_SUBPASS_END_INFO ||
        pSubpassEndInfo->pNext) {
        if (command && command->state == VK_PS5_COMMAND_RECORDING)
            command->record_error = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }
    vkCmdEndRenderPass(commandBuffer);
}

VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDeviceMask(VkCommandBuffer c, uint32_t m) { IGNORE(c); IGNORE(m); }
VK_PS5_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatchBase(VkCommandBuffer c, uint32_t bx, uint32_t by, uint32_t bz,
                  uint32_t x, uint32_t y, uint32_t z) {
    IGNORE(c); IGNORE(bx); IGNORE(by); IGNORE(bz); IGNORE(x); IGNORE(y); IGNORE(z);
}
