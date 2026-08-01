#include <vulkan/vulkan.h>

#include "../src/vulkan_ps5_internal.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct AllocationState {
    atomic_int attempts;
    atomic_int live;
    atomic_int fail_at;
} AllocationState;

static void *VKAPI_PTR test_allocate(void *user, size_t size, size_t alignment,
                                     VkSystemAllocationScope scope) {
    (void)scope;
    AllocationState *state = user;
    int attempt = atomic_fetch_add(&state->attempts, 1);
    if (attempt == atomic_load(&state->fail_at)) return NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    void *ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) return NULL;
    atomic_fetch_add(&state->live, 1);
    return ptr;
}

static void *VKAPI_PTR test_reallocate(void *user, void *original, size_t size,
                                       size_t alignment, VkSystemAllocationScope scope) {
    if (!original) return test_allocate(user, size, alignment, scope);
    if (size == 0) {
        free(original);
        atomic_fetch_sub(&((AllocationState *)user)->live, 1);
        return NULL;
    }
    return NULL;
}

static void VKAPI_PTR test_free(void *user, void *memory) {
    if (!memory) return;
    free(memory);
    atomic_fetch_sub(&((AllocationState *)user)->live, 1);
}

static VkAllocationCallbacks make_allocator(AllocationState *state) {
    VkAllocationCallbacks allocator = {
        .pUserData = state,
        .pfnAllocation = test_allocate,
        .pfnReallocation = test_reallocate,
        .pfnFree = test_free,
    };
    return allocator;
}

static VkInstance create_instance(const VkAllocationCallbacks *allocator) {
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&create_info, allocator, &instance) == VK_SUCCESS);
    return instance;
}

static VkResult create_device_result(VkInstance instance,
                                     const VkAllocationCallbacks *allocator,
                                     VkDevice *device) {
    uint32_t count = 1;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    assert(vkEnumeratePhysicalDevices(instance, &count, &physical) == VK_SUCCESS);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    *device = VK_NULL_HANDLE;
    return vkCreateDevice(physical, &create_info, allocator, device);
}

static VkDevice create_device(VkInstance instance,
                              const VkAllocationCallbacks *allocator) {
    VkDevice device = VK_NULL_HANDLE;
    assert(create_device_result(instance, allocator, &device) == VK_SUCCESS);
    return device;
}

static void test_allocation_failures(void) {
    AllocationState state;
    atomic_init(&state.attempts, 0);
    atomic_init(&state.live, 0);
    atomic_init(&state.fail_at, 0);
    VkAllocationCallbacks allocator = make_allocator(&state);
    VkInstanceCreateInfo create_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkInstance instance = VK_NULL_HANDLE;
    assert(vkCreateInstance(&create_info, &allocator, &instance) == VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(instance == VK_NULL_HANDLE);
    assert(atomic_load(&state.live) == 0);

    atomic_store(&state.attempts, 0);
    atomic_store(&state.fail_at, -1);
    instance = create_instance(&allocator);
    atomic_store(&state.attempts, 0);
    VkDevice device = create_device(instance, &allocator);
    assert(vk_ps5_device_meta_clear_pipeline(device) != VK_NULL_HANDLE);
    int device_allocation_count = atomic_load(&state.attempts);
    assert(device_allocation_count > 1);
    vkDestroyDevice(device, &allocator);
    assert(atomic_load(&state.live) == 1);

    for (int fail_at = 0; fail_at < device_allocation_count; ++fail_at) {
        atomic_store(&state.attempts, 0);
        atomic_store(&state.fail_at, fail_at);
        device = VK_NULL_HANDLE;
        assert(create_device_result(instance, &allocator, &device) ==
            VK_ERROR_OUT_OF_HOST_MEMORY);
        assert(device == VK_NULL_HANDLE);
        assert(atomic_load(&state.live) == 1);
    }

    atomic_store(&state.attempts, 0);
    atomic_store(&state.fail_at, -1);
    device = create_device(instance, &allocator);
    int baseline = atomic_load(&state.live);
    assert(baseline > 2);

    VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    atomic_store(&state.attempts, 0);
    atomic_store(&state.fail_at, 0);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    assert(vkAllocateMemory(device, &info, NULL, &memory) == VK_ERROR_OUT_OF_HOST_MEMORY);
    assert(memory == VK_NULL_HANDLE);
    assert(atomic_load(&state.live) == baseline);

    atomic_store(&state.attempts, 0);
    atomic_store(&state.fail_at, -1);
    assert(vkAllocateMemory(device, &info, NULL, &memory) == VK_SUCCESS);
    vkFreeMemory(device, memory, NULL);
    vkDestroyDevice(device, &allocator);
    vkDestroyInstance(instance, &allocator);
    assert(atomic_load(&state.live) == 0);
}

static void *lifecycle_thread(void *opaque) {
    uintptr_t iterations = (uintptr_t)opaque;
    for (uintptr_t i = 0; i < iterations; ++i) {
        VkInstance instance = create_instance(NULL);
        VkDevice device = create_device(instance, NULL);
        VkMemoryAllocateInfo info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = 1024,
            .memoryTypeIndex = (uint32_t)(i & 1u),
        };
        VkDeviceMemory memory = VK_NULL_HANDLE;
        assert(vkAllocateMemory(device, &info, NULL, &memory) == VK_SUCCESS);
        void *mapped = NULL;
        assert(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS);
        memset(mapped, (int)i, 1024);
        vkUnmapMemory(device, memory);
        vkFreeMemory(device, memory, NULL);
        vkDestroyDevice(device, NULL);
        vkDestroyInstance(instance, NULL);
    }
    return NULL;
}

int main(void) {
    test_allocation_failures();
    enum { THREAD_COUNT = 4, ITERATIONS = 64 };
    pthread_t threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; ++i)
        assert(pthread_create(&threads[i], NULL, lifecycle_thread,
                              (void *)(uintptr_t)ITERATIONS) == 0);
    for (int i = 0; i < THREAD_COUNT; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    return 0;
}
