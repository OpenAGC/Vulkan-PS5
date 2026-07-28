# Vulkan-PS5

Vulkan-PS5 is an application-neutral Vulkan ICD for PlayStation 5 homebrew. The
current implementation includes the host-testable Milestone 1 ICD, the
Milestone 2 runtime-pipeline path, the hardware-qualified Milestone 3 OpenAGC
DCB path, and the hardware-qualified Milestone 4 headless-surface/swapchain
path. Milestone 5 also qualifies the relocatable SDK package through a separate
standard-Vulkan consumer on both host and FW 5.50.
Milestone 6 is tracked by `analysis/eden-compatibility.md` and the
`vulkan_ps5.eden_profile_report` test; the initial Eden suitability baseline is
30 hard gaps rather than an application-specific bypass.
Query-complete driver-properties, conservative shader-float-controls, and the
exact-count occlusion path reduce the live count to 27.
A deterministic mirror-clamp readback probe and bounded FW 5.50 runner are
prepared, but `VK_KHR_sampler_mirror_clamp_to_edge` remains hidden until the
exact gfx1013 address mode passes that hardware gate.
The runtime compiler and graphics-pipeline path also accept instance-rate
vertex bindings with nonzero `VK_EXT_vertex_attribute_divisor` divisors, while
the extension remains hidden pending one run of the prepared deterministic
hardware readback gate.
Sampler creation also carries validated 1x-16x anisotropy into gfx1013's
anisotropic point/linear modes and maximum-ratio field. The public
`samplerAnisotropy` bit remains false until a deterministic hardware readback
gate validates the filtering result. That gate is now prepared: equal bilinear
and 16x-anisotropic draws sample the same high-frequency stripe texture with an
elongated footprint, and the mapped-memory oracle requires the anisotropic
half's mean absolute deviation from neutral gray to fall materially below the
bilinear control. Its runner permits one launch and applies the established
exact-PID crash, cleanup, warning, and console-response checks.
Milestone 6 also includes a test-only, configurable VulkanMemoryAllocator
consumer matching Eden's dynamic-dispatch, externally synchronized upload,
download, stream, device-local, image, manual-bind, and suballocation patterns;
it passes both direct and loader/VVL host modes without adding VMA to the SDK's
public dependencies. The Prospero PIE passed its bounded FW 5.50 gate with
deterministic zero-allocation teardown and application-level SystemService
termination, so the allocator patterns are hardware-qualified at this scope.
It exposes the
complete Vulkan 1.0/1.1 core entrypoint surface, conservative gfx1013 physical
device properties, two OpenAGC-backed PS5 GPU memory classes and
synchronization objects, loader dispatch, a static SDK library, and a
loader-compatible shared library. Features which do not yet have verified
OpenAGC hardware implementations are deliberately not advertised.

The ICD consumes OpenAGC's application-neutral gfx1013 capability query for
qualified dimensions, formats, sample counts, compute limits, and memory
profiles. `OPENAGC_ROOT`, `VULKAN_HEADERS_ROOT`, and `OPENAGC_PSBC_ROOT` remain
configurable, including `OPENAGC_PSBC_LIBRARY` for selecting the host or
Prospero archive. The installed SDK carries the matching compiler archive and
public header; consumers still resolve Vulkan-Headers and OpenAGC through the
exported CMake package dependencies.

## Build

```sh
cmake -S . -B build -DVULKAN_HEADERS_ROOT=../Vulkan-Headers
cmake --build build
ctest --test-dir build --output-on-failure
```

Prospero builds select `libopenagc_psbc.prospero.a` automatically when cross
compiling. Final PS5 links require `libunwind`, `libc++abi`, `libc++`, and
`libm`. The `ps5-payload-libcxx` and `ps5-payload-openlibm` recipes in
`/Users/bizkut/Downloads/PS5/homebrew/pacbrew-repo` install those target headers
and archives into the payload SDK. Merely having the recipe checkout is not
sufficient—the packages must be built and installed into the SDK prefix.

Applications link `VulkanPS5::ICD` after installing the package, or link
`libvulkan_ps5.a` directly. They use only standard Vulkan headers and APIs.

## Reusable installed SDK

The Milestone 5 package test installs Vulkan-Headers, OpenAGC, the runtime
shader compiler, and Vulkan-PS5 into a fresh prefix, moves that prefix, and
then configures a separately copied consumer with only:

```cmake
find_package(VulkanPS5 CONFIG REQUIRED)
target_link_libraries(application PRIVATE VulkanPS5::ICD)
```

The consumer includes `<vulkan/vulkan.h>` and exercises an ordinary Vulkan
1.1 instance/device lifecycle. The relocation test rejects CMake metadata or
consumer link commands containing a source-workspace path and verifies that
the relocated `libvulkan_ps5.a`, `libopenagc.a`, and
`libopenagc_psbc.a` archives are used. Its Prospero mode also verifies the
transitive `kernel`, `SceAgcDriver`, `SceVideoOut`, `unwind`, `c++abi`, `c++`,
and `m` links. The sample links `SceSystemService` itself solely for safe raw
ELF termination.

Run the host relocation check through CTest, or run the Prospero check
directly:

```sh
ctest --test-dir build -R vulkan_ps5.package_relocation --output-on-failure
sh tests/package_relocation.sh . build-prospero-m2 ../Vulkan-Headers \
  /path/to/ps5-payload-sdk/toolchain/prospero.cmake \
  build-prospero-m2/vulkan_ps5_package_consumer.elf
```

The Prospero toolchain searches packages only below its find roots, so the
test registers the relocated SDK as an additional `CMAKE_FIND_ROOT_PATH`.
Applications using an SDK installed directly in the payload sysroot need no
such override.

After a fresh `ps5 up` signal, the retained consumer has a dedicated bounded
hardware gate:

```sh
PS5_HOST=10.0.1.41 sh examples/run_fw550_package_consumer.sh
```

The runner performs exactly one deployment, requires
`package-consumer: PASS result=0`, verifies self-requested app termination and
exact-PID removal through ps5debug-NG, checks that websrv remains responsive,
and rejects fatal or unexpected PID-scoped kernel messages. It permits at most
the single `amount:0x4000` warning already proven to be the FW 5.50 raw-ELF
baseline.

The retained FW 5.500.008 gate ran once on 2026-07-28. PID 153 printed the PASS
oracle, completed self-KillApp and `All processes exited`, was absent from the
ps5debug-NG process list, and left websrv responsive. Its scoped klog contained
only the proven raw-ELF baseline warning. Evidence is retained in
`examples/qualification-logs/20260728T070752Z-package-consumer.log` and
`20260728T070752Z-package-consumer-target.klog`.

## Headless surface and swapchain sample

`VK_EXT_headless_surface` is the standard PS5 VideoOut surface. The ICD
advertises `VK_KHR_surface`, `VK_EXT_headless_surface`, and `VK_KHR_swapchain`,
reports a fixed 1920x1080 BGRA8-sRGB FIFO contract, and owns three linear
write-combined scanout images. Firmware patching, buffer registration, flip
events, bounded waits, and teardown remain inside OpenAGC.

`vulkan_ps5_swapchain_example` uses only standard Vulkan calls and runs 1,800
acquire/submit/present frames with binary semaphores and a fence. Its host run,
the nine-test ICD suite, runner safety simulation, and Validation Layers pass;
its Prospero ELF links with `-lSceSystemService -lunwind -lc++abi -lc++ -lm`.
The current candidate SHA-256 is
`d94722b2c9473b8407769b9b1fe044dd5796c6d5f78bbba7ccec15cfb6975c90`.
Run exactly one bounded FW 5.50 gate after an explicit console-availability
signal:

Finite image-acquisition timeouts use a monotonic deadline. The WSI regression
also exhausts all three images, releases one from a delayed presentation
thread, and verifies that the waiting acquire wakes without holding the
swapchain lock across the VSYNC wait.

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_swapchain.sh
```

The runner never retries automatically. It takes a bounded post-run klog
snapshot, scopes it to the new eboot PID, rejects fatal signals, app crashes,
XO faults, duplicate or unrecognized kernel warnings, and any VM warning beyond
the one proven FW 5.50/raw-ELF baseline line. It requires a self-requested kernel `KillApp()` event
followed by `[AppMgr] All processes exited`, and asks ps5debug-NG to prove that
the exact launched PID no longer exists. SystemService removes a self-killed
process before `sceSystemServiceKillApp` can return, so stdout after that call
is intentionally not used as an oracle. On
timeout or a post-PASS safety failure it derives that PID from klog and asks
ps5debug-NG to kill only that process before returning failure.

The first FW 5.50 attempt stopped safely before registration: kernel evidence
showed that byte verification touched the execute-only VideoOut text page
before its permissions were changed. OpenAGC `290213c` performs verification
inside the short RWX window and restores RX on every exit. A second run reached
1,800 frames, but the kernel reported a teardown SIGSEGV and a `0x4000` VM
resource leak after the sample had printed PASS. OpenAGC `18011af` now uses the
hardware-proven teardown order (close VideoOut, then delete its equeue), and the
sample emits PASS only after swapchain, device, surface, and instance cleanup.
The corrected run completed those cleanup checkpoints, but returning from the
Prospero ELF entrypoint then jumped back into `main` (`RIP 0x4000bb`,
`main+0xbb`) and caused SIGSEGV. Subsequent `thr_exit` and libc `exit`
candidates completed Vulkan cleanup but left hbldr or the raw-ELF application
lifecycle incomplete; the libc run (`20260728T060157Z-swapchain-run1.log`)
left PID 145/app ID `0x16` owning a black screen. A guarded recovery payload
called `sceSystemServiceKillApp` for that app and restored the home screen,
which was confirmed visually. The Prospero sample now resolves its app ID with
`sceSystemServiceGetAppStatus`, requests SystemService termination after all
Vulkan cleanup, and keeps the main thread alive until the system completes it.
The termination helper is C11 `_Noreturn`; Prospero disassembly shows `main`
ending in the helper call followed by `ud2`, while both helper outcomes end in
sleep loops, so the candidate has no raw-loader return path.
The next run (`20260728T062155Z-swapchain-run1.log`) completed all 1,800 frames
and Vulkan cleanup. Its scoped klog recorded the self-requested `KillApp()`,
`All processes exited`, and shell focus restoration; exact-PID inspection found
no process and websrv remained responsive. The sole failure was a `0x4000` VM
resource warning, exactly matching OpenAGC's standalone multi-submit trailer
allocation. OpenAGC `1c0fb8f` now carves the 64-byte trailer from unused
`SceGnmDdid` space instead of allocating another 16 KiB VM resource.
The follow-up (`20260728T063200Z-swapchain-run1.log`) again completed 1,800
frames, Vulkan cleanup, the full kernel app-exit lifecycle, exact-PID absence,
and the bounded websrv response, but reproduced the same warning. This falsified
the trailer hypothesis. OpenAGC had restored the temporarily writable
execute-only VideoOut text range as read/execute; `0c22e06` now restores its
exact original execute-only protection so the kernel can coalesce the mapping.
The next gate (`20260728T063634Z-swapchain-run1.log`) reproduced the warning,
falsifying that mapping hypothesis as well. All 27 retained OpenAGC graphics
klogs contain the identical one-page warning. OpenAGC `4f66aa7` now balances
the remaining flip-event lifecycle explicitly—unregister event, close
VideoOut, then delete the still-live equeue.
The balanced-lifecycle run (`20260728T064111Z-swapchain-run1.log`) again
completed 1,800 frames, Vulkan cleanup, self-KillApp, exact-PID removal, and
shell restoration without a crash, but retained the same warning. The bounded
`vulkan_ps5_system_exit_probe` now isolates the raw-ELF/SystemService path: it
links only `kernel_sys`, `SceSystemService`, `unwind`, `c++abi`, `c++`, and `m`,
performs no Vulkan, OpenAGC, GPU, VideoOut, equeue, or custom-memory work, and
classifies either a clean teardown or exactly one baseline `0x4000` warning.
Its ELF SHA-256 is
`e585e74f872a4dfc7fa63910437b106843334666157672f9959c27558afe06a9`.
The bounded baseline run
(`20260728T064628Z-system-exit-probe-target.klog`) produced the exact same
single warning while completing self-KillApp, exact-PID removal, and the
post-run console probe. This proves the line is raw-ELF container bookkeeping,
not a Vulkan/OpenAGC/VideoOut leak. The diagnostic can be repeated only after a
fresh explicit console signal:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_system_exit_probe.sh
```

`vulkan_ps5_process_cleanup.elf` retains the proven recovery path and refuses
to act unless exactly one other `eboot.elf` exists. The balanced 1,800-frame
swapchain run plus the dependency-free baseline comparison close the FW 5.50
Milestone 4 hardware gate.

## Standalone compute and triangle samples

Configure with `-DVULKAN_PS5_BUILD_EXAMPLES=ON` to build
`vulkan_ps5_compute_example`. It uses only Vulkan 1.1 APIs, dispatches a runtime
compiled storage-buffer shader, waits on a fence, invalidates mapped memory,
and verifies 1,024 deterministic values. Command recording emits OpenAGC's
hardware-qualified gfx1013 compute-default groups once before the command
buffer's first dispatch. A Prospero cross-build produces
`vulkan_ps5_compute_example.elf`; FW 5.50 execution remains the qualification
gate. Running it on the generic host backend intentionally reports a mismatch
because that backend records submissions but does not execute shaders.

The same option builds `vulkan_ps5_triangle_example`. It renders a solid-green
triangle into a mapped 256x256 linear RGBA8 attachment through an ordinary
render pass, waits for completion, invalidates the allocation, and verifies the
center, background, and green-pixel count. Its Prospero output is
`vulkan_ps5_triangle_example.elf`; generic-host execution likewise reports the
expected all-zero readback.

`vulkan_ps5_geometry_example` adds a standard geometry stage to that workload.
The geometry shader shrinks the input triangle by one half in each dimension,
so its mapped-memory oracle expects roughly one quarter of the ordinary
triangle coverage and cannot pass through a vertex-only path accidentally. The
host command regression records an indexed draw with the fused VS+GS primitive
record; its Prospero output is `vulkan_ps5_geometry_example.elf`. Two
independent FW 5.500.008 launches each produced exactly 4608 green pixels, so
the standalone geometry workload is hardware-qualified. The ICD now reports
`geometryShader = VK_TRUE` through both core feature-query forms and accepts it
through legacy `pEnabledFeatures` and `VkPhysicalDeviceFeatures2`, while still
rejecting unadvertised features. The sample queries and requests geometry
normally. Both seven-test host configurations and the Prospero build pass; the
feature-requesting ELF links with `-lunwind -lc++abi -lc++ -lm` and has SHA-256
`386aae854e1aaf504a750aa29904c491e35220d52c718c3bcf048f54de6803a4`.
Two independent bounded FW 5.500.008 runs produced exactly 4608 green pixels
each (`20260728T051424Z-geometry-run1.log` and
`20260728T051510Z-geometry-run1.log`). Both returned normally, bounded post-run
websrv checks confirmed the console remained responsive, and neither was
retried. The standard public `geometryShader` path is hardware-qualified.

`vulkan_ps5_tessellation_example` uses a three-control-point patch, level-two
TCS factors, and a TES that scales the evaluated triangle to 62.5 percent. Its
distinct mapped-memory coverage oracle exercises the shared factor/offchip
rings, ring descriptor table, fused Wave32 LS+HS and TES+NGG records, and
`DRAW_INDEX_AUTO`. Two safe black-target diagnostics first identified and fixed
missing command-buffer ring programming, then left unqualified offchip
patch-output reads as the only material difference from OpenAGC's passing
path. The basic hardware gate now uses OpenAGC's qualified constant-position
shader dataflow. Its Prospero ELF also cross-links with the required target
runtimes. Two independent FW 5.500.008 launches each produced exactly 7200
green pixels, hardware-qualifying the basic standalone workload. The later
patch-output-read qualification below completed the requirement for exposing
the core tessellation feature.

The qualified patch-output candidate derives its TCS/TES offchip layout words
from openagc-psbc API v5 metadata and OpenAGC's typed layout builder rather
than reusing the OpenAGC fixture constant. Pipeline creation also supplies the
adjacent TES module while compiling TCS and the adjacent TCS module while
compiling TES or TES+GS. The compiler links that non-executable interface
module so both separately emitted programs share one offchip location remap.
Host command-recording tests cover the linked compiler path and derived PM4
values; the public feature bit was kept disabled until the restored patch-output
sample passes the bounded hardware gate twice. The first bounded API v5 FW
5.500.008 run returned safely and left the console responsive, but its target
remained zeroed, so no retry was attempted.

The next host-qualified correction uses openagc-psbc API v6 to carry RADV's
pipeline-specific hull LDS byte requirement into OpenAGC. The tessellation
binder encodes that allocation in `SPI_SHADER_PGM_RSRC2_HS`, enabling the
separate LS-front/HS-back memory path used when a TCS reads VS outputs. This
state is covered by command-recording tests. Its first bounded FW 5.500.008 run
returned normally and left etaHEN websrv responsive, but the target was still
zeroed (`20260728T023553Z-tessellation-run1.log`). The runner did not retry it;
hull LDS allocation alone therefore does not qualify patch-output reads.

Post-failure comparison with Mesa exposed a gfx10.3 encoding detail in that
candidate: HS LDS bytes must first be rounded to a 1024-byte allocation and
then represented in the register's 512-byte units. OpenAGC now emits only the
resulting even field values, and the Vulkan command regression rejects a zero
or odd encoding. Its one bounded FW 5.500.008 run returned normally and left
etaHEN websrv responsive, but again produced a zeroed target
(`20260728T024632Z-tessellation-run1.log`). It was not retried. Correct LDS
rounding is required, but it is not the remaining VS-to-TCS dataflow fix.

The next locally verified candidate fixes that dataflow directly. RADV had
copied its monolithic VS+TCS same-invocation optimization into both shader-info
records, while openagc-psbc disabled it only for the separately emitted LS
front. The HS back consequently retained `load_per_vertex_input` operations;
standalone ACO compiled those unavailable temporary-VGPR values as zero, which
made the TCS write zero positions to the offchip ring. openagc-psbc now forces
both halves through the common LDS ABI and rejects compilation if an HS input
survives lowering. Host inspection confirms the HS loads are `load_shared`, all
seven ICD tests pass, and the Prospero ELF links with `-lunwind -lc++abi -lc++
-lm`. Its one bounded FW 5.500.008 run returned normally and left etaHEN
websrv responsive, but the target remained zeroed
(`20260728T030535Z-tessellation-run1.log`). It was not retried. Correct
LS-to-HS LDS dataflow is retained, but the remaining failure is downstream in
HS offchip storage, tessellation ring state, or TES consumption; feature
advertisement remains disabled.

The next materially distinct qualification candidate adds a standard Vulkan
storage buffer to TCS. Each hull invocation writes an independent execution
marker and the VS position it read from LDS. After the existing bounded fence
wait, the sample invalidates and checks that mapped buffer before reporting the
image oracle. This separates LS-to-HS execution/LDS failures from downstream
HS-offchip/TES failures without exposing a private driver API. Host pipeline
creation, command recording, all seven ICD tests, and the Prospero link passed.
Its first and only FW 5.500.008 launch froze graphics until Shell UI restarted
(`20260728T031733Z-tessellation-run1.log`). The ps5debug-NG kernel stream
identified PID 129 with an SQC-data read protection fault at unmapped VA
`0x0000000200000000`; the faulting wave reported `XNACK_ERROR MEMVIOL`. No
test process remained after recovery, so no unrelated process was killed and
the candidate was not retried.

ACO inspection showed that the separately compiled HS uses its indirect
descriptor-set-table pointer in `s14`. openagc-psbc API v7 now reports that
indirect pointer distinctly from ordinary direct set pointers, and the ICD
allocates a GPU-visible array of bound-set low addresses and writes the array
address to the compiler-selected register before drawing. The ICD rejects the
table or any bound-set pointer outside gfx1013's `0x2_xxxxxxxx` aperture. The
command test requires the pointer and bound set-1 entry to be nonzero and
matches the exact compiler-selected register/value pair in PM4. Both seven-test
host configurations pass and the Prospero link
includes `-lunwind -lc++abi -lc++ -lm`. The corrected ELF has SHA-256
`9be96734ae5b1643d9b1f408101cc345bb0bb7291491ed8fbdc989ab974285cc` and
completed its first bounded FW 5.500.008 launch successfully
(`20260728T034030Z-tessellation-run1.log`). The storage-buffer oracle reported
all three hull invocation markers (`0x48530000`, `0x48530001`, and
`0x48530002`) and the exact three input control-point positions; the image
oracle reported exactly 7200 green pixels. The runner returned normally, and
no retry was attempted. A second independent fresh-console run reproduced all
hull markers, positions, and exactly 7200 green pixels
(`20260728T034211Z-tessellation-run1.log`); the console remained responsive.
The restored patch-output path is hardware-qualified at this scope. The ICD now
returns `tessellationShader = VK_TRUE` from both core feature-query forms and
accepts it from `pEnabledFeatures` or `VkPhysicalDeviceFeatures2`, while still
rejecting unadvertised core features. Lifecycle regressions cover the Features2
success path and an unsupported-feature rejection. The standalone sample now
queries and requests tessellation explicitly. Both host configurations pass all
seven tests, and its rebuilt Prospero ELF links with `-lunwind`, `-lc++abi`,
`-lc++`, and `-lm` and has SHA-256
`a1fce3414f4fadac09ff10148d76302bac504ac3befd119e059bd3330877d30d`.
Its one bounded hardware smoke passed every hull marker and copied-position
check but produced an all-zero image
(`20260728T034904Z-tessellation-run1.log`). The application returned and the
console remained responsive; no retry was attempted. Apart from the final
image oracle, this log is identical to the immediately preceding passing log.
The result exposes nondeterminism after TCS execution in the
offchip-to-TES/raster path, so public feature exposure is not considered stable
until that path is corrected and the repeated hardware gate passes again.

The next diagnostic keeps the interface application-neutral by extending the
existing storage-buffer binding to TES. The tessellated vertex with
`gl_TessCoord.x` equal to one writes marker `0x54455300` and copies all three
offchip control-point positions. A missing marker identifies TES launch or
factor-ring state, a marker with incorrect positions identifies offchip reads,
and a passing TES probe with a zero image identifies rasterization. Both host
configurations pass all seven tests, host execution reaches the expected
no-GPU oracle, and the Prospero ELF links with `-lunwind`, `-lc++abi`, `-lc++`,
and `-lm`. Its SHA-256 is
`b3e239c757996b7b8296719d461415913e9f1475b601553f28dd5aa06ac65c6e`.
Its one bounded FW 5.500.008 run returned normally and left the console
responsive (`20260728T035640Z-tessellation-run1.log`). The hull probe still
passed, and TES wrote marker `0x54455300`, so the evaluation stage and factor
ring are active. TES copied zero for every component of all three offchip
control points and the image remained black. This excludes downstream
rasterization and localizes the correction to HS offchip stores or the matching
TES offchip address/layout ABI. No retry was attempted.

OpenAGC commit `6406c9b` corrects the resulting ring-capacity mismatch. The old
public profile allocated one 8K-dword (32 KiB) offchip buffer and programmed
`VGT_HS_OFFCHIP_PARAM = 0`, despite gfx1013 being able to retain four offchip
workgroups per CU across four shader engines, two shader arrays per engine, and
five CUs per array. The new application-neutral profile provisions all 160
buffers in a 5 MiB offchip ring, programs the encoded buffer count as `159`,
and uses Mesa's 120 KiB (`0x1e000`) factor-ring size. OpenAGC now rejects a
ring whose storage is smaller than its encoded buffering/granularity profile.
Both Vulkan host configurations pass all seven tests, the Prospero build passes
and links with `-lunwind -lc++abi -lc++ -lm`, and the resulting tessellation ELF
has SHA-256
`316ee53df2a1b29d7dcd1c5f1c4adb3cfe0d0f07bb66f2ea41cb1d88eda9e09b`.
This candidate still requires a fresh-console, single bounded hardware run;
the driver does not treat the nondeterministic offchip path as qualified yet.

Two independent FW 5.500.008 runs of that full-ring candidate passed every
oracle (`20260728T043915Z-tessellation-run1.log` and
`20260728T044035Z-tessellation-run1.log`). In both runs all three hull markers
and copied LDS positions matched, TES wrote marker `0x54455300` and copied the
exact three offchip control-point positions, and the image contained exactly
7200 green pixels. Both processes returned normally, bounded post-run websrv
checks confirmed that the console remained responsive, and neither run was
retried. The repeated hardware gate is closed: the offchip correction and
public `tessellationShader` exposure are hardware-qualified at this scope.

Run the advanced stages one at a time. The default is one run so a new packet
path is never repeated automatically. After each first pass, invoke that stage
once more to collect the second independent qualification log. Console
reachability, FTP upload, and HTTP launch operations all use bounded timeouts.
Advanced-stage HTTP launches allow 60 seconds for runtime pipeline compilation
while retaining the one-run default:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh geometry
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh geometry
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh tessellation
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh tessellation
```

`vulkan_ps5_indexed_textured_example` binds an interleaved position/UV vertex
buffer, a UINT16 index buffer containing `{1,2,3}` after a decoy vertex, and a
bilinear clamp sampler over a 2x2 RGBA8 image. Its readback oracle requires
triangle coverage, fully opaque sampled pixels, at least 16 distinct colors,
an interior center sample, and untouched background corners.

`vulkan_ps5_depth_example` combines a mapped linear RGBA8 target with an
OpenAGC-laid-out optimal D32+S8 attachment. One near green triangle must occlude
an overlapping far red triangle while a separate far red triangle passes over
the initialized depth value. Passing fragments replace stencil with `0x5a`.
Its oracle checks exact interior colors, coverage, raw clear/near/far D32 words,
and requires the S8 write count to equal final green-plus-red coverage.
Generic-host execution intentionally reports unchanged memory because that
backend does not execute GPU commands.

`vulkan_ps5_mrt_example` draws one triangle to two linear RGBA8 attachments.
Its fragment shader exports green to location 0 and magenta to location 1;
the mapped-memory oracle independently checks coverage, exact color, center,
and untouched background for both targets.

`vulkan_ps5_query_example` is a query-enabled build of the deterministic
triangle workload. It brackets the draw with an occlusion query and requires
the returned 64-bit sample count and availability bit to match the mapped
green-pixel oracle exactly. It uses `VK_EXT_host_query_reset` so query begin/end
hardware can be qualified independently from command-reset PM4. The generic
backend intentionally reports an unavailable zero result because it records
but does not execute the stream.

Query hardware qualification is deliberately staged. The
`vulkan_ps5_query_lifecycle_probe` creates and host-resets a pool but emits no
query PM4. The `vulkan_ps5_query_reset_probe` adds only the corrected command
reset `WRITE_DATA`. The `vulkan_ps5_query_idle_probe` adds occlusion begin/end
snapshots around an empty render pass and requires an available zero result.
The full target then counts a live draw. Run one stage explicitly; the full
stage is now part of the repeated Milestone 3 gate after passing twice on FW
5.50. The narrower stages remain useful for packet-level regression diagnosis:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh lifecycle
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh reset
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh idle
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh full
```

When the console is online, deploy either Prospero ELF through the foreground
etaHEN websrv path so its stdout is returned to the terminal:

```sh
PS5_HOST=10.0.1.41 examples/deploy_websrv.sh \
  build-prospero-m2/vulkan_ps5_compute_example.elf vulkan_ps5_compute
PS5_HOST=10.0.1.41 examples/deploy_websrv.sh \
  build-prospero-m2/vulkan_ps5_triangle_example.elf vulkan_ps5_triangle
```

The Milestone 3 runner checks websrv reachability, performs
two foreground runs of each sample, requires the exact compute, triangle,
indexed-textured, depth, MRT, and query PASS oracles, and retains stdout under
`examples/qualification-logs/`:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_m3.sh
```

The 2026-07-27 UTC FW `0x05500008` qualification passed all six runs: both
compute runs verified 1,024 deterministic values, both triangle runs verified
exactly 18,432 green pixels, and both indexed-textured runs verified exactly
18,432 opaque sampled pixels with at least 64 distinct colors. See
`analysis/fw550_indexed_textured_qualification_20260727.md` for the retained
revision and artifact evidence.

The 2026-07-27 UTC `20260727T231245Z` run qualified depth and MRT twice, but its
first query submission hung the GPU. Packet-level recovery then qualified the
query lifecycle, corrected reset, idle begin/end, and live counting in stages.
The final expanded `20260728T003956Z` gate passed all six workloads twice:
query returned 18,432 samples matching 18,432 mapped pixels, combined D32+S8
produced identical `54145/12288/9830` raw-depth counts and 22,118 stencil writes
matching color coverage, and MRT produced 18,432 pixels in both targets. The
full investigation and retained-log map are recorded in
`analysis/fw550_depth_mrt_query_qualification_20260727.md`.

Set `VULKAN_PS5_PROSPERO_BUILD`, `VULKAN_PS5_FW550_RUNS`, or
`VULKAN_PS5_FW550_LOG_DIR` to override the build directory, repeat count, or
log destination.

The Khronos validation test is enabled automatically when the host Vulkan loader
and Validation Layers are installed. It exercises instance/device lifecycle,
property chains, memory, buffers, images, views, render-pass/framebuffer objects,
command buffers, submission, and fences while failing on any VVL warning/error.

Runtime shader compilation and host-verifiable OpenAGC DCB recording are now
integrated. Shader code and submission DCBs use OpenAGC GPU-visible flexible
memory, while `vkQueueSubmit` appends a monotonic EOP label, submits through
OpenAGC, and waits with a bounded timeout before host signaling. VideoOut WSI
and FW 5.50 qualification remain separate milestones. Graphics VS/PS and
compute CS pipeline creation
compile SPIR-V at runtime with complete vertex, descriptor/pipeline-layout,
push-constant, specialization-constant, entry-point, and render-pass color
context. Geometry and tessellation stage fusion are also wired through pipeline
creation, including independent specialization data for fused stages. Geometry
and tessellation draws now accept the fused compiler records and metadata;
tessellation uses device-owned OpenAGC rings registered through the FW driver
and restores typed depth/stencil state after binding. Both optional Vulkan
feature bits remain disabled until their standalone pipelines are repeatedly
qualified on hardware. Compute dispatch and a no-input triangle draw now emit real
gfx1013 `DISPATCH_DIRECT` and `DRAW_INDEX_AUTO` packet sequences.
Uniform/storage-buffer descriptor sets are stored through standard Vulkan
updates, encoded into GPU-visible OpenAGC tables, and patched into the compiler-
selected user-SGPR immediately before compute dispatch or graphics draws.
Graphics buffer descriptors are covered by a TCS command-recording test so the
tessellation qualification workload can use a standard storage-buffer oracle.
Indexed draws bind standard
Vulkan vertex/index buffers, build per-draw GPU-visible gfx1013 vertex tables,
and emit `DRAW_INDEX_2` for UINT16 or UINT32 indices. Combined and separate
sampled-image/sampler descriptors for linear RGBA8/BGRA8 images are encoded
through OpenAGC and bound to compiler-selected graphics SGPRs. Linear image
allocation and subresource layouts use the gfx1013-required 256-byte row pitch;
the sampled-image path is repeatedly hardware-qualified. Dynamic buffer
offsets and optional sparse, protected, external-handle, multiview, YCbCr, and
timeline features remain unavailable.

On Prospero, `vkCreateDevice` reaches OpenAGC initialization, which now keeps
the FW-specific GPU process-authorization preparation inside its `/dev/gc`
backend. Standalone Vulkan applications do not include or call the OpenAGC
hardware-test credential header.

The initial graphics render-pass path supports one single-sampled linear color
attachment or up to eight MRT color attachments, an optional OpenAGC-laid-out
optimal depth/stencil attachment, one
inline subpass, load/don't-care operations, fixed full-range viewport/scissor
state, fill rasterization without culling, disabled blending, and all-component
writes. MRT currently requires blending disabled and all components enabled on
every target. Static depth compare/write and front/back stencil state are translated
to typed OpenAGC draw state. Begin/end render pass translate layouts to OpenAGC
resource transitions, emit the qualified gfx1013 frame prologue, bind attachment
addresses, and restore host-readable cache state after drawing. Depth/stencil
clears remain unavailable; the first hardware gate initializes mapped direct
memory and uses `LOAD`.

Occlusion query pools use GPU-visible storage and OpenAGC-owned gfx1013 ZPASS
snapshots. Reset is command-ordered, end-query publishes a separate EOP
availability label, and `vkGetQueryPoolResults` supports 32/64-bit values,
availability, partial results, and bounded waits. Repeated FW 5.50 runs matched
the exact 18,432-sample result to independent mapped color coverage, so
`occlusionQueryPrecise` is advertised and the query sample requests it and
records `VK_QUERY_CONTROL_PRECISE_BIT`. Timestamps remain disabled pending
hardware qualification.
`VK_EXT_host_query_reset` is advertised with `hostQueryReset = VK_TRUE` and
flushes the selected query slots after a host-side reset.
