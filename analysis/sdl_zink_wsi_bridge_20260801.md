# SDL/Zink native-window WSI bridge (2026-08-01)

The PS5 EGL path now creates a real window drawable without adding another
VideoOut implementation. SDL passes its stable window identity to Mesa's
Prospero surfaceless platform. Mesa/Kopper lowers that drawable to
`VkHeadlessSurfaceCreateInfoEXT`; Vulkan-PS5 then creates its existing
headless surface, swapchain, dedicated scanout images, and OpenAGC
`AgcPresentChain`.

Zink requires sampled swapchain images, so the frozen WSI usage mask now adds
`VK_IMAGE_USAGE_SAMPLED_BIT`. Vulkan-PS5 already maps that bit to the public
OpenAGC sampled-image usage, while the existing scanout enable step adds
`AGC_IMAGE_USAGE_SCANOUT_BIT` before allocation.

Host evidence:

- Vulkan-PS5 generic CTest: 46/46 pass, including dynamic-rendering clear
  recording and the updated frozen WSI capability snapshot.
- Vulkan-PS5 Prospero shared-ICD build: pass.
- Mesa Prospero EGL/Zink/Kopper build and staged install: pass.
- SDL PS5 Zink qualification ELF build: pass.

Hardware status remains pending. The guarded FW 5.50 run must still prove EGL
context creation, deterministic readback, visible `eglSwapBuffers`
presentation, bounded teardown, and immediate relaunch before this bridge or
its linked artifacts are called hardware-qualified or assigned final hashes.
