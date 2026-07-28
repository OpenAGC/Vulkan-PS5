#!/bin/sh
set -eu

repo_dir=$(cd "$1" && pwd -P)
driver_build=$(cd "$2" && pwd -P)
vulkan_headers_root=$(cd "$3" && pwd -P)
toolchain=${4:-}
artifact_output=${5:-}
if [ -n "$toolchain" ]; then
    toolchain_dir=$(cd "$(dirname "$toolchain")" && pwd -P)
    toolchain="$toolchain_dir/$(basename "$toolchain")"
fi
workspace_root=$(dirname "$repo_dir")
test_root=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-package.XXXXXX")
test_root=$(cd "$test_root" && pwd -P)
trap 'rm -rf "$test_root"' EXIT

original_prefix="$test_root/original-sdk"
relocated_prefix="$test_root/relocated-sdk"
consumer_source="$test_root/consumer-source"
consumer_build="$test_root/consumer-build"
link_log="$test_root/consumer-link.log"

cmake -S "$vulkan_headers_root" -B "$test_root/vulkan-headers-build" \
    -DCMAKE_INSTALL_PREFIX="$original_prefix" \
    -DVULKAN_HEADERS_ENABLE_TESTS=OFF >/dev/null
cmake --build "$test_root/vulkan-headers-build" >/dev/null
cmake --install "$test_root/vulkan-headers-build" >/dev/null
cmake --install "$driver_build/openagc" --prefix "$original_prefix" >/dev/null
cmake --install "$driver_build" --prefix "$original_prefix" >/dev/null

mv "$original_prefix" "$relocated_prefix"
if [ -e "$original_prefix" ]; then
    echo "original SDK prefix still exists after relocation" >&2
    exit 1
fi
if grep -R -F "$workspace_root" "$relocated_prefix/lib/cmake" >/dev/null; then
    echo "installed CMake metadata contains a source-workspace path" >&2
    grep -R -n -F "$workspace_root" "$relocated_prefix/lib/cmake" >&2
    exit 1
fi

mkdir -p "$consumer_source"
cmake -E copy "$repo_dir/tests/package_consumer/CMakeLists.txt" \
    "$consumer_source/CMakeLists.txt"
cmake -E copy "$repo_dir/tests/package_consumer/main.c" \
    "$consumer_source/main.c"

if [ -n "$toolchain" ]; then
    cmake -S "$consumer_source" -B "$consumer_build" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCMAKE_FIND_ROOT_PATH="$relocated_prefix" \
        -DCMAKE_PREFIX_PATH=/ >/dev/null
else
    cmake -S "$consumer_source" -B "$consumer_build" \
        -DCMAKE_PREFIX_PATH="$relocated_prefix" >/dev/null
fi
if ! cmake --build "$consumer_build" --verbose >"$link_log" 2>&1; then
    cat "$link_log" >&2
    exit 1
fi
if grep -F "$workspace_root" "$link_log" >/dev/null; then
    echo "consumer build used a source-workspace path" >&2
    grep -n -F "$workspace_root" "$link_log" >&2
    exit 1
fi
for archive in libvulkan_ps5.a libopenagc.a libopenagc_psbc.a; do
    if ! grep -F "$relocated_prefix/lib/$archive" "$link_log" >/dev/null; then
        echo "consumer link omitted relocated $archive" >&2
        grep -n -E '(^|[[:space:]])(cc|c\\+\\+|clang|ld)([[:space:]]|$)|libvulkan_ps5|libopenagc' \
            "$link_log" >&2 || true
        exit 1
    fi
done

if [ -n "$toolchain" ]; then
    for library in kernel SceAgcDriver SceVideoOut SceSystemService \
                   unwind c++abi c++ m; do
        if ! grep -F -- "-l$library" "$link_log" >/dev/null; then
            echo "Prospero consumer link omitted -l$library" >&2
            exit 1
        fi
    done
    if [ -n "$artifact_output" ]; then
        mkdir -p "$(dirname "$artifact_output")"
        cmake -E copy "$consumer_build/vulkan_ps5_package_consumer.elf" \
            "$artifact_output"
        echo "retained Prospero consumer: $artifact_output"
    fi
else
    "$consumer_build/vulkan_ps5_package_consumer"
fi

echo "relocated VulkanPS5::ICD package consumer: PASS"
