#include <vulkan_ps5/vulkan_ps5.h>

int main(void) {
    uint32_t version = 0;
    return vkEnumerateInstanceVersion(&version) == VK_SUCCESS &&
           version >= VK_API_VERSION_1_1 ? 0 : 1;
}
