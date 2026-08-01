/*
 * Regenerate examples/bc_sampling/bc_probe_assets.h from the Mesa codecs
 * vendored by openagc-psbc.  This tool is host-only and is not linked into
 * the PS5 artifact.
 */
#include "util/format/u_format_bptc.h"
#include "util/format/u_format_rgtc.h"
#include "util/format/u_format_s3tc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void (*Pack8)(uint8_t *, unsigned, const uint8_t *, unsigned,
    unsigned, unsigned);
typedef void (*PackFloat)(uint8_t *, unsigned, const float *, unsigned,
    unsigned, unsigned);
typedef void (*UnpackFloat)(void *, unsigned, const uint8_t *, unsigned,
    unsigned, unsigned);

typedef struct Asset {
    const char *name;
    uint32_t vk_format;
    uint32_t block_size;
    uint8_t block[16];
    uint32_t expected[4];
} Asset;

static void fill_rgba8(uint8_t pixels[64], const uint8_t color[4])
{
    for (uint32_t pixel = 0u; pixel < 16u; ++pixel)
        memcpy(pixels + pixel * 4u, color, 4u);
}

static void fill_rgba_float(float pixels[64], const float color[4])
{
    for (uint32_t pixel = 0u; pixel < 16u; ++pixel)
        memcpy(pixels + pixel * 4u, color, 4u * sizeof(float));
}

static void decode_expected(Asset *asset, UnpackFloat unpack)
{
    float decoded[64] = {0};
    unpack(decoded, 4u * 4u * sizeof(float), asset->block,
        asset->block_size, 4u, 4u);
    memcpy(asset->expected, decoded + (2u * 4u + 1u) * 4u,
        sizeof(asset->expected));
}

static void make_unorm_pair(Asset *linear, Asset *srgb, Pack8 pack,
    UnpackFloat unpack_linear, UnpackFloat unpack_srgb,
    const uint8_t color[4])
{
    uint8_t pixels[64];
    fill_rgba8(pixels, color);
    pack(linear->block, linear->block_size, pixels, 4u * 4u, 4u, 4u);
    memcpy(srgb->block, linear->block, linear->block_size);
    decode_expected(linear, unpack_linear);
    decode_expected(srgb, unpack_srgb);
}

static void make_float(Asset *asset, PackFloat pack, UnpackFloat unpack,
    const float color[4])
{
    float pixels[64];
    fill_rgba_float(pixels, color);
    pack(asset->block, asset->block_size, pixels,
        4u * 4u * sizeof(float), 4u, 4u);
    decode_expected(asset, unpack);
}

static void print_asset(const Asset *asset)
{
    printf("    {\n        \"%s\", %uu, %uu,\n        {", asset->name,
        asset->vk_format, asset->block_size);
    for (uint32_t index = 0u; index < 16u; ++index) {
        if (index == 8u)
            printf("\n         ");
        printf("0x%02x", asset->block[index]);
        if (index != 15u)
            printf(index == 7u ? "," : ", ");
    }
    printf("},\n        {0x%08xu, 0x%08xu, 0x%08xu, 0x%08xu},\n    },\n",
        asset->expected[0], asset->expected[1],
        asset->expected[2], asset->expected[3]);
}

int main(void)
{
    /* Vulkan 1.2 core VkFormat numeric values are stable ABI constants. */
    Asset assets[] = {
        {"bc1_rgba_unorm", 133u, 8u, {0}, {0}},
        {"bc1_rgba_srgb", 134u, 8u, {0}, {0}},
        {"bc2_unorm", 135u, 16u, {0}, {0}},
        {"bc2_srgb", 136u, 16u, {0}, {0}},
        {"bc3_unorm", 137u, 16u, {0}, {0}},
        {"bc3_srgb", 138u, 16u, {0}, {0}},
        {"bc4_unorm", 139u, 8u, {0}, {0}},
        {"bc4_snorm", 140u, 8u, {0}, {0}},
        {"bc5_unorm", 141u, 16u, {0}, {0}},
        {"bc5_snorm", 142u, 16u, {0}, {0}},
        {"bc6h_ufloat", 143u, 16u, {0}, {0}},
        {"bc6h_sfloat", 144u, 16u, {0}, {0}},
        {"bc7_unorm", 145u, 16u, {0}, {0}},
        {"bc7_srgb", 146u, 16u, {0}, {0}},
    };
    /* Keep BC1 in its opaque four-color mode while retaining nontrivial alpha
     * for BC2, BC3, and BC7. */
    const uint8_t rgba_color[4] = {37u, 149u, 223u, 201u};
    const uint8_t rg_color[4] = {77u, 181u, 0u, 255u};
    const float signed_rg[4] = {-0.5f, 0.75f, 0.0f, 1.0f};
    const float bc6_unsigned[4] = {0.5f, 1.5f, 2.0f, 1.0f};
    const float bc6_signed[4] = {-0.5f, 0.75f, -1.25f, 1.0f};

    make_unorm_pair(&assets[0], &assets[1],
        util_format_dxt1_rgba_pack_rgba_8unorm,
        util_format_dxt1_rgba_unpack_rgba_float,
        util_format_dxt1_srgba_unpack_rgba_float, rgba_color);
    make_unorm_pair(&assets[2], &assets[3],
        util_format_dxt3_rgba_pack_rgba_8unorm,
        util_format_dxt3_rgba_unpack_rgba_float,
        util_format_dxt3_srgba_unpack_rgba_float, rgba_color);
    make_unorm_pair(&assets[4], &assets[5],
        util_format_dxt5_rgba_pack_rgba_8unorm,
        util_format_dxt5_rgba_unpack_rgba_float,
        util_format_dxt5_srgba_unpack_rgba_float, rgba_color);

    uint8_t pixels[64];
    fill_rgba8(pixels, rg_color);
    util_format_rgtc1_unorm_pack_rgba_8unorm(assets[6].block, 8u,
        pixels, 16u, 4u, 4u);
    decode_expected(&assets[6], util_format_rgtc1_unorm_unpack_rgba_float);
    util_format_rgtc2_unorm_pack_rgba_8unorm(assets[8].block, 16u,
        pixels, 16u, 4u, 4u);
    decode_expected(&assets[8], util_format_rgtc2_unorm_unpack_rgba_float);
    make_float(&assets[7], util_format_rgtc1_snorm_pack_rgba_float,
        util_format_rgtc1_snorm_unpack_rgba_float, signed_rg);
    make_float(&assets[9], util_format_rgtc2_snorm_pack_rgba_float,
        util_format_rgtc2_snorm_unpack_rgba_float, signed_rg);
    make_float(&assets[10], util_format_bptc_rgb_ufloat_pack_rgba_float,
        util_format_bptc_rgb_ufloat_unpack_rgba_float, bc6_unsigned);
    make_float(&assets[11], util_format_bptc_rgb_float_pack_rgba_float,
        util_format_bptc_rgb_float_unpack_rgba_float, bc6_signed);
    make_unorm_pair(&assets[12], &assets[13],
        util_format_bptc_rgba_unorm_pack_rgba_8unorm,
        util_format_bptc_rgba_unorm_unpack_rgba_float,
        util_format_bptc_srgba_unpack_rgba_float, rgba_color);

    puts("/* Generated by tools/generate_bc_probe_assets.c. */");
    puts("#ifndef VULKAN_PS5_BC_PROBE_ASSETS_H");
    puts("#define VULKAN_PS5_BC_PROBE_ASSETS_H");
    puts("#include <stdint.h>");
    puts("typedef struct BcProbeAsset {");
    puts("    const char *name;");
    puts("    uint32_t vk_format;");
    puts("    uint32_t block_size;");
    puts("    uint8_t block[16];");
    puts("    uint32_t expected_float_bits[4];");
    puts("} BcProbeAsset;");
    puts("static const BcProbeAsset bc_probe_assets[14] = {");
    for (uint32_t index = 0u; index < 14u; ++index)
        print_asset(&assets[index]);
    puts("};");
    puts("#endif");
    return 0;
}
