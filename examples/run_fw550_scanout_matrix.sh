#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

: "${VULKAN_PS5_SCANOUT_MATRIX_EXPECTED_SHA256:?set the rebuilt scanout-matrix ELF SHA-256}"
: "${VULKAN_PS5_CLEANUP_EXPECTED_SHA256:?set the pinned cleanup ELF SHA-256}"

VULKAN_PS5_QUALIFICATION_ELF="$build_dir/vulkan_ps5_scanout_matrix_probe.elf" \
VULKAN_PS5_QUALIFICATION_REMOTE_NAME=vulkan_ps5_scanout_matrix \
VULKAN_PS5_QUALIFICATION_LABEL=scanout_matrix \
VULKAN_PS5_QUALIFICATION_PASS_PATTERN='^scanout_matrix: PASS cases=A,B,C,D,E,F,G,H,I exact-readback$' \
VULKAN_PS5_QUALIFICATION_PASS_DESCRIPTION='nine exact scanout readback cases' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN='^scanout_matrix: A garlic-bgra-to-ordinary-linear-blit PASS pixels=2073600 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_2='^scanout_matrix: B rgba8-to-scanout-linear-blit PASS pixels=2073600 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_3='^scanout_matrix: C fixed-fragment-draw-to-scanout PASS pixels=2073600 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_4='^scanout_matrix: D prior-submit-bgra-to-scanout-linear-blit PASS pixels=2073600 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_5='^scanout_matrix: E eden-like-rendered-bgra-to-scanout-linear-blit PASS pixels=2073600 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_6='^scanout_matrix: F eden-onion-rendered-bgra-to-scanout-linear-blit PASS pixels=2073600 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_7='^scanout_matrix: G eden-shared-onion-semaphore-to-scanout-linear-blit PASS images=3 pixels=6220800 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_8='^scanout_matrix: H eden-consumer-recorded-before-producer PASS pixels=2073600 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_9='^scanout_matrix: I eden-sparse-readback-before-blit PASS source_samples=16 destination_samples=16 exact=ff00ffff$' \
VULKAN_PS5_QUALIFICATION_REJECT_PATTERN='scanout_matrix: .* FAIL|scanout_matrix: .* failed' \
VULKAN_PS5_SWAPCHAIN_EXPECTED_SHA256="$VULKAN_PS5_SCANOUT_MATRIX_EXPECTED_SHA256" \
    exec "$script_dir/run_fw550_swapchain.sh"
