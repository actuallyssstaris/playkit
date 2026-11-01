# PlayKit Architecture Assessment

## Current state (native title)

PlayKit is a WebKit 2.48.0 port configured as a **PS5 native title** (standalone
eboot, not a payload).
- Toolchain: `clang-18` targeting `x86_64-sie-ps5`, linker `prospero-lld`, SDK `PS5_PAYLOAD_SDK`
- Build: Makefile + `tools/build.sh`, compiles C sources, links with `prospero-lld`,
  converts via `ps5-native-tool`, signs as FSELF, packages `dist/PPSA90002/`
- JSC build: paused (memmem conflict with PS5 SDK string.h, fixed; may hit
  further errors). `libJavaScriptCore.a` not yet produced.
- Dependencies built: Cairo 1.16.0, Pixman 0.42.2, ICU 76.1, libwpe 1.16.2
  (all static, under `build/ps5`)
- Platform shims: `platform/ps5_compat.{h,c}` (pthread_cond_clockwait,
  timespec_get, pthread_setname_np, clock_gettime_np)
- WebKit has extensive PlayStation-specific code: WTF/playstation,
  JSC/shell/playstation, WebCore/platform/playstation, etc.
- Graphics plan: software rendering via diagnostic painter → VideoOut (no EGL/GLES
  yet; ps5-opengl is the intended fast path)
- Process model: single-process native title; WebKit2 multiprocess deferred
- Console: online at `192.168.2.15` (ports 9295, 9021 open); Remote Play works
- GitHub repo: `github.com/actuallyssstaris/playkit` (public). Actions builds
  on push and workflow_dispatch.

## SDK research (thoroughly read)

### ps5-payload-sdk (ps5-payload-dev/sdk, in .deps/native/)

Base cross-compilation SDK: `prospero-clang`, `prospero-lld`, `prospero-ar`, etc. in `bin/`.
- `target/include/`: FreeBSD-derived headers (pthread.h, unistd.h, sys/mman.h, ps5/kernel.h, etc.)
- `target/include/ps5/kernel.h`: kernel memory ops (kernel_copyin/copyout, kernel_mprotect, kernel_dynlib_*, KERNEL_OFFSET_* struct offsets)
- `target/lib/`: `libc.a`, `libc++.a`, `libc++abi.a`, `libdl.a` (8-byte stub), `libunwind.a`, `libkernel.so`, `libSce*.so` stubs (libScePad.so, libSceVideoOut.so, libSceSystemService.so, libkernel_web.so, etc.), `crt*.o`
- `toolchain/prospero.mk`, `prospero.sh`, `prospero.ini`, `prospero.cmake`: make/CMake wrappers setting CC/CXX/LD to prospero-* tools
- `samples/`: hello_world, hello_cxx23, hello_stdio, browser, etc.

### ps5-native-app-boilerplate (blackbearreloaded/ps5-native-app-boilerplate, in PlaySurf-boilerplate/)

Complete native app build system — the proven pattern PlayKit adapts.

**Build system (`Makefile`, `tools/build.sh`):**
- Targets: `app`, `init`, `doctor`, `test`, `deps`, `pacbrew`, `libc`, `format`, `tidy`, `lint`, `deploy`, `undeploy`, `clean`, `distclean`
- `make app` → `tools/setup-native-dependencies.sh` (SDK + zlib) → `tools/build.sh Folder` → compile C/C++ with `prospero-clang18` wrapper → link with `prospero-lld` + `ps5-pie.ld` → convert via `ps5-native-tool` → sign → package `dist/<TITLE_ID>/` + `.zip`
- `APP_WRAP_SYMBOLS`: `--wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=posix_memalign --wrap=malloc_usable_size --wrap=fcntl`
- PacBrew: `PACBREW_PACKAGES="libcurl openssl ..."` for prebuilt PS5 ports
- Filesystem: `/app0` read-only (title data), `/download0` read/write persistent, `/temp0` temporary (often unavailable). PlayKit keeps everything it creates in one folder — the game folder `/data/homebrew/PPSA90002` when writable, else `/download0` — resolved once by `ps5_state_dir()` in `src/ps5_paths.c`.

**Native app runtime (`tooling/native/`):**
- `app_crt.cpp`: `_start(void *process_parameters, Destructor loader_teardown)` → `_init_env`, atexit(_fini), static ctors, `application_main(argc, argv, envp)` (asm "main"), `exit(status)`
- `app_cpp_runtime.cpp`: `operator new/delete` bridge to malloc/free/posix_memalign, exceptions+RTTI off, `ps5ObserveOwnedAllocation()`
- `ps5-pie.ld`: page-separated PIE layout (text/ro/relro/data/dynamic phdrs)
- `app-symbols.map`: `local: *;` (all symbols internal)
- `libc_builder.cpp`: deterministic clean-room `libc.prx` emitter from `runtime/api-surface.txt` (2,566 exports) + `runtime/imports.txt` (102 imports). SHA-256: `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036`
- `self_container.cpp/hpp`: FSELF container sign/extract/inspect (magic 0xEEF51454 or 0x1D3D154F, developer authority 0x3100000000000002)
- `native_app_builder.cpp`, `sce_module_writer.cpp`, `elf_object.cpp`, `hash.hpp`: PS5 ELF/FSELF converter

**Demo app (`src/`):**
- `main.cpp`: C++20, `int main()`, reads `/app0/assets/banner.txt`, calls `ps5::demo::run(draw_scene, ...)`
- `demo_renderer.{hpp,cpp}`: CPU VideoOut — `sceVideoOutOpen(0xff, 0, 0, NULL)`, `sceKernelAllocateDirectMemory` + `sceKernelMapDirectMemory` (memory_type 3 = WC garlic, prot 0x33), `sceVideoOutSetBufferAttribute2` (pixel_format 0x8000000022000000, tiling 0), `sceVideoOutRegisterBuffers2`, `sceVideoOutSubmitFlip`, `sceVideoOutWaitVblank`. Tiled byte offset: `((y<<4)&0x70)^((y<<5)&0xf00)^...` XOR pixel map, 128-byte tiles.
- `sce_systemServiceHideSplashScreen()`, `sceSystemServiceLoadExec("exit", NULL)` on quit

**Packaging (`sce_sys/`):**
- `param.json`: titleId PPSA99999, conceptId 99999, contentId `UP9000-PPSA99999_00-HELLOWORLD000001`, contentVersion NN.NNN.NNN, masterVersion NN.NN, applicationCategoryType 0 (game) or 65536 (media), contentBadgeType 1, gameIntent.launchActivity
- `icon0.png` (512x512 PNG), `pic0.dds`/`pic1.dds` (3840x2160 BC7 DDS), `snd0.at9` (ATRAC9, ≤2 MiB)
- `sce_module/libc.prx`: signed runtime module

### ps5-opengl (darkxex/ps5-opengl / blackbearreloaded/ps5-opengl)

**OpenGL 4.6 Core / GLSL 4.60 native SDK for PS5.** Mesa/Gallium with PS5-specific Gallium driver. EGL for context/surface. Fullscreen EGL at 1080p/1440p/4K, 60/120 Hz. Passes full GL 4.6 CTS (98,590 pass, 0 fail). GPL-3.0-or-later.

**Architecture (`docs/architecture.md`):**
- Application → EGL facade (`src/egl/ps5_egl.c`) → Mesa frontend (pinned Mesa + patches) → PS5 Gallium driver (`src/gallium/ps5/ps5_screen.c`) → Shader compiler (patched OpenGNM PSBC) → Native backend (`src/platform/`) → AGC + VideoOut
- Mesa 26.2.0, PSBC over public commit `a92a1228`, compiler patch

**EGL API (`src/egl/ps5_egl.c`):**
- `eglGetDisplay(EGL_DEFAULT_DISPLAY)` → only NULL display accepted
- `eglInitialize(display, &major, &minor)` → creates Gallium screen + 2 scanout buffers (PIPE_FORMAT_B8G8R8A8_UNORM, PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET)
- `eglBindAPI(EGL_OPENGL_API)` → only OpenGL supported
- `eglChooseConfig` → config 1 (window+pbuffer, non-conformant), config 2 (pbuffer only, conformant)
- `eglCreateWindowSurface(display, config, NULL, NULL)` → window must be NULL; binds scanout[0]/scanout[1], creates depth-stencil Z32_FLOAT_S8X24_UINT
- `eglCreateContext(display, config, NULL, attribs)` → OpenGL Core/Compat context via Mesa state tracker
- `eglMakeCurrent(display, draw, read, context)`, `eglSwapBuffers(display, surface)` → glthread finish, HUD, st_context_flush, then weak `ps5_agc_gate2_present(buffer_index, swap_interval)` under screen submit lock
- `eglSwapInterval(display, interval)` → 0 or 1
- `eglTerminate(display)` → weak `ps5_agc_gate2_shutdown_present()`
- Extensions: `eglSetDisplayModePS5`, `eglSetDisplayRefreshPS5`, `eglGetDisplayModePS5` (1080p/1440p/4K, 60/120 Hz; set only while EGL terminated)
- `eglQueryString` → vendor "PS5 Mesa experimental", version "1.4 PS5 experimental", client APIs "OpenGL"
- **`ps5_agc_gate2_present` and `ps5_agc_gate2_shutdown_present` are weak symbols** — application/linker must provide them (resolved via `-u,ps5_agc_gate2_run`)

**Scanout (`src/platform/ps5_scanout.h`):**
- `PS5_SCANOUT_WIDTH`/`PS5_SCANOUT_HEIGHT` — 1920x1080 default, 2560x1440, 3840x2160 (AGC_4K)
- `PS5_SCANOUT_BYTES` — 0xa00000 (1080p), 0x1000000 (1440p), 0x2000000 (4K)
- `PS5_SCANOUT_ALIGNMENT` — 0x200000
- `PS5_SCANOUT_FPS` — 60 or 120
- `PS5_DYNAMIC_SCANOUT` — runtime display modes, requires 2160p120 capacity profile

**Gallium screen (`src/gallium/ps5/ps5_screen.h`):**
- `ps5_screen_create()`, `ps5_screen_submit_lock/unlock()`, `ps5_screen_present_lock()`
- `ps5_screen_prepare_present()`, `ps5_context_queue_present()`
- Resource query: `ps5_resource_info`, `ps5_resource_gpu_info`, descriptor functions
- `ps5_display_target_alias()`

**AGC package (`src/platform/ps5_agc_package.h`):**
- `ps5_agc_package_build(shader, esgs_ring_itemsize, &package, &package_size)` — build AGC shader package from PSBC output
- `ps5_agc_compute_plan_memory/execute/dispatch/queue()`

**Mesa shims (`src/platform/ps5_mesa_shims.c`):**
- `open_memstream()` stub → ENOSYS (ACO uses it only for optional diagnostics)
- `util_set_thread_affinity()` stub → false

**pkg-config (`toolchain/ps5-opengl.pc`):**
```
Cflags: -DGL_GLEXT_PROTOTYPES=1 -I${includedir}
Libs: -Wl,-u,ps5_agc_gate2_run -L${libdir} -Wl,--start-group -lPS5OpenGL -Wl,--end-group ${libdir}/libSceAgc.so ${libdir}/libSceAgcDriver.so -lSceVideoOut -lkernel_web -lSceSystemService
```

**CMake config (`toolchain/PS5OpenGLConfig.cmake`):**
- `PS5OpenGL::OpenGL` imported interface library
- Include: `${PS5OpenGL_PREFIX}/include`, defines `GL_GLEXT_PROTOTYPES=1`
- Libs: `libPS5OpenGL.a`, `libSceAgc.so`, `libSceAgcDriver.so`, `SceVideoOut`, `kernel_web`, `SceSystemService`
- Link option: `LINKER:-u,ps5_agc_gate2_run`

**Native-app template (`native-app/`):**
- `app_heap.c` — sceLibcMspace heap (128 MiB default, weak-overridable `ps5_opengl_heap_size`), `__wrap_*` for malloc/calloc/realloc/free/posix_memalign/malloc_usable_size; foreign pointers go to libc heap
- `runtime_shims.c` — log redirect to `/download0/ps5-opengl.log`, `catchReturnFromMain`, `__assert`, stubs for `mkstemps`, `openlog`, `popen`, `pclose`
- `agc_link_stub.c` / `agc_driver_link_stub.c` — stubs for sceAgc*/sceAgcDriver* functions
- `gpu_memory.c` — diagnostic tracking for direct memory allocations
- `param.json` — titleId PPSA99005, conceptId 99005, AMM/kernel VA settings in attribute3
- `ps5-pie.ld` — includes `tooling/native/ps5-pie-base.ld`
- `app-symbols.map` — `local: *;`

**Build (`docs/building.md`, `Makefile`):**
- Prerequisites: Git, Make, GCC/G++, binutils, CMake, Bison, Flex, Clang/LLD 18, Python 3.12+, Meson, Ninja, Mako, PyYAML, packaging, glslangValidator, SPIR-V Tools, host EGL/OpenGL headers
- `make sdk-gl46` builds: OpenGNM PSBC compiler (host + PS5), Mesa (with ps5 patch), native backend, installed SDK
- Pins: Mesa 26.2.0, PSBC over `a92a1228`, compiler patch over `a92a1228`
- Native app boilerplate alongside for payload SDK
- Output: `build/sdk/ps5-opengl-gl46/` (relocatable SDK), `build/native-app/PPSA99005/dist/PPSA99005/` (native folder app)

**Limitations (`docs/limitations.md`):**
- Experimental, not Khronos-certified
- Fullscreen EGL + static SDK in already-configured native homebrew environment
- GLX, WGL, GLFW absent; Compatibility profile via EGL + SDL2 bridge (best effort)
- Fail-stop on GPU submission/completion errors (no recovery)
- Hardware results cover one firmware-6.02 console and HDMI4 setup

### PS5_Vulkan (darkxex/PS5_Vulkan, fork of mihawk-99/PS5_Vulkan)

**Vulkan 1.4+ PS5 driver** (Mesa RADV + PS5 winsys), CTS-validated (1.5M+ cases, 0 fail on second full run). vkQuake at 120 FPS 4K, RetroArch with 6 Vulkan cores. GPL-3.0-or-later.

**Key assets for PlayKit:**
- `runtime/libc.prx` — clean-room signed runtime module (SHA-256: `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036`), hardware-validated on firmware 6.02 and 12.70. Generated by `tools/rebuild-libc.sh` from `tooling/native/libc_builder.cpp` + manifests (`runtime/api-surface.txt`, `runtime/imports.txt`). 2,566 loader-visible exports, 102 named system imports, 100 jump-slot relocations. Not copied from any Sony module.
- `tools/adapt-opengl-sdk.sh` — reconstructs the ps5-opengl SDK tree that PS5_Vulkan tools read
- `tools/setup-native-dependencies.sh` — installs PS5 payload SDK fork + static zlib 1.3.2 into `.deps/native/`
- `tools/rebuild-libc.sh` — deterministic build of libc.prx (builds twice, requires byte-identical, checks digest)
- `tooling/native/` — `app_crt.cpp`, `app_cpp_runtime.cpp`, `app-symbols.map`, `ps5-pie.ld`, `elf_object.*`, `sce_module_writer.*`, `self_container.*`, `libc_builder.cpp`, `hash.hpp`
- `ps5-native-tool` — host utility: `link`, `self --sign`, `self --extract`, `self --inspect`
- `sce_sys/` — `param.json`, `icon0.png`, `pic0.dds`, `pic1.dds`, `snd0.at9`
- No C# or .NET dependency. C++20 host tools, exceptions+RTTI off for targets.
- Hardware validated on firmware 6.02 and 12.70 with ShadowMountPlus

**Comparison with ps5link-sdk (Rufidj):**
- ps5link-sdk: Custom C linker, `crt1_ps5.o`, SharpProspero signing (.NET), C++ untested, no TLS — unsuitable for WebKit (C++ heavy)
- ps5-native-app-boilerplate (PlaySurf): `prospero-lld` + `ps5-pie.ld`, `app_crt.cpp` + `app_cpp_runtime.cpp`, `ps5-native-tool` signing, working C++20, proven browser (PlaySurf/NetSurf on PS5) — chosen path

## Recommended architecture

**Graphics: ps5-opengl (OpenGL 4.6 via EGL), not software rendering.**

Replace the software-rendering plan (WKPagePaint → Cairo → VideoOut) with ps5-opengl:
- EGL context + OpenGL 4.6 Core for GPU-accelerated compositing
- WebKit TextureMapper/Coordinated Graphics targets EGL+GLES; OpenGL 4.6 runs GLES backends unchanged
- `eglSwapBuffers` presents via VideoOut at vblank (60/120 Hz)
- `ps5-opengl` SDK provides `libPS5OpenGL.a`, `libSceAgc.so`, `libSceAgcDriver.so`, EGL/GLES headers
- Application must provide `ps5_agc_gate2_present()` and `ps5_agc_gate2_shutdown_present()` (weak symbols resolved via `-u,ps5_agc_gate2_run`)

**Native app build: ps5-native-app-boilerplate pattern (as proven by PlaySurf).**

- `prospero-lld` + `ps5-pie.ld` linker script
- `app_crt.cpp` + `app_cpp_runtime.cpp` startup
- `ps5-native-tool self --sign` for eboot.bin
- `sce_module/libc.prx` from PS5_Vulkan (clean-room, signed)
- `sce_sys/param.json` + `icon0.png`
- Deploy to `/data/homebrew/<TITLE_ID>/`, launch from home screen

**Heap: sceLibcMspace (ps5-opengl's `app_heap.c` pattern).**
- 128–384 MiB flexible memory, wraps malloc/free/etc., foreign pointers to libc heap
- Weak-overridable size via `ps5_opengl_heap_size`

**Process model: single-process WebKit initially.**
- WebKit2 multiprocess (rfork_thread + ptrace + execve) is payload-oriented; on a native title, helper processes are complex
- Start single-process; revisit multiprocess after first render

## Files to create / modify

**Create:**
- `src/main.c` ✅ — native `int main(void)`, sceSystemServiceHideSplashScreen, run loop, LoadExec("exit")
- `src/ps5_heap.c` / `src/ps5_heap.h` ✅ — sceLibcMspace heap (~384 MiB)
- `src/ps5_video.c` / `src/ps5_video.h` ✅ — VideoOut flip (to be replaced with EGL+OpenGL)
- `src/ps5_pad.c` / `src/ps5_pad.h` ✅ — scePad DualSense input
- `src/webkit_glue.c` / `src/webkit_glue.h` ✅ — WebKit integration (stub paint)
- `tools/build.sh` ✅ — native app build/link/package (needs ps5-opengl integration)
- `tooling/native/ps5-pie.ld` ✅ — linker script
- `tooling/native/app-symbols.map` ✅ — symbol version script
- `tooling/native/app_crt.cpp` ✅ — CRT startup
- `tooling/native/app_cpp_runtime.cpp` ✅ — C++ new/delete bridge
- `sce_sys/param.json` ✅ — title metadata (PPSA90002)
- `runtime/libc.prx` — from PS5_Vulkan (clean-room, signed)
- `sce_sys/icon0.png` — from PS5_Vulkan

**Modify:**
- `src/ps5_video.c` — replace software retile with EGL+OpenGL context + eglSwapBuffers
- `tools/build.sh` — link against ps5-opengl SDK (`libPS5OpenGL.a`, `libSceAgc.so`, `libSceAgcDriver.so`, `SceVideoOut`, `kernel_web`, `SceSystemService`), add wrap symbols, add `-u,ps5_agc_gate2_run`
- `cmake/ps5-toolchain.cmake` — keep for reference; native build uses Makefile
- `docs/process-launch.md` — update for native app process model (single-process)
- `status/index.html` — update checklist

**Preserve:**
- All existing WebKit PlayStation-specific code (WTF/playstation, JSC/shell/playstation, WebCore/platform/playstation)
- `platform/ps5_compat.{h,c}` — still needed for libc++ shims
- Built static libs (Cairo, Pixman, ICU, libwpe) — may be replaced by ps5-opengl for graphics
- Partially built JSC objects (reusable when WebKit is rebuilt as static libs)

**Sony private library replacements:**

| Sony private | Public replacement | Feasibility |
|-------------|-------------------|-------------|
| SceCairoForWebKit | Cairo upstream + libwpe | High — Cairo 1.16.0 and libwpe 1.16.2 already built |
| libpsl | Already in PS5 payload SDK | Resolved |
| WebKitRequirements | Build WebKit dependencies from source | High — CMake builds WTF, JSC, WebCore targets |
| ProcessLauncher | WebKit Unix ProcessLauncher or custom PS5 launcher | High — `ProcessLauncherPlayStation.cpp` already builds argv/socket options; replace only the private `launchProcess` call |
| SceWPE | Open libwpe 1.16.2 (already built) | Resolved |
| SceVshPsl | Custom PS5 launcher / shell integration | Medium — needed for VideoOut/pad integration |

## Native launch fixes (2026-10-31)

The first console launch failed with `CE-100008-9`. The deployed `eboot.bin`
was an unsigned 164,536-byte ELF, not a signed FSELF.

Root cause: `tooling/native/app-symbols.map` contained `global: *;`, so every
symbol was exported. The native converter rejects application exports
(`native converter does not yet publish application exports`), and
`tools/build.sh` hid the failure with `2>/dev/null || cp`, leaving the unsigned
PIE in the app folder. Three fixes:

- `app-symbols.map` now contains only `local: *;`, matching the boilerplate.
- The build aborts on converter or signing failure. It never publishes an
  unsigned `eboot.bin`.
- The link no longer whole-archives the payload SDK's `libc.a`. That archive
  pulls in `dlfcn.o`, whose weak `__dlopen`/`__dlsym`/`__dlerror` references
  the converter refuses to import. Native titles link the console's
  `libSceLibcInternal` module instead, as the boilerplate does.

Verified output: 15,577-byte signed FSELF, 12 segments, authority
`0x3100000000000002`, FreeBSD ABI 2, ELF type `0xFE10`, integrity valid,
NEEDED modules `libScePad`, `libSceVideoOut`, `libSceSystemService`,
`libSceUserService`, `libkernel`, `libSceLibcInternal`.

### Homebrew starter review

`MiladRTX/ps5-homebrew-starter` packages the same boilerplate (`kit/`, commit
`44ae1da`) plus two additions ported here:

- `APP_IMPORT_STUBS`: link-only `.so` stubs for system modules the payload SDK
  does not ship. A stub exports the function name; the converter records the
  matching system import and the console binds the real function at launch.
  This is how `sceCommonDialogInitialize` (on-screen keyboard) is reached.
  Build the stub with `prospero-lld --shared -soname libSceCommonDialog.sprx`.
- `tools/deploy.sh`: atomic FTP deployment, `eboot.bin` and `param.json`
  published last, then verified. Also supports `undeploy` scoped to the title.

The starter's PC simulator is not reusable directly: it depends on the
starter's CPU canvas and pad abstraction. The WebKit app needs its own host
test harness once WebCore links.

## Console crash: SIGSYS at exit (2026-10-31)

Second launch of the signed build crashed with `CE-108255-1`:

```
signal: 12 (SIGSYS)
thread name: eboot.bin
RIP: 0x00000008000003ac
backtrace[0]: 0x000000000040017c
```

The console log `/data/playkit.log` gave the sequence:

```
start
heap: 384 MiB mapped, 0 KiB live, 0 KiB peak, 0 failed
video: built without EGL; using software blit
video ok
pad: scePadOpen failed: -2137325439
fatal: controller initialisation failed
```

`-2137325439` is `0x809B0081`. `scePadOpen(0, 0, 0, NULL)` fails because the
pad service needs the id of a logged-in user, and the user service must be
initialised first. Returning `1` from `main` then routed through the console
libc's `exit()`, which raises SIGSYS in this launch context. The `RIP` sits in
the import trampoline region, and `backtrace[0]` is the `ud2` after `_start`'s
call to `exit`.

Fixes:

- `ps5_pad_start` now calls `sceUserServiceInitialize(NULL)`,
  `sceUserServiceGetInitialUser(&user)`, then `scePadOpen(user, 0, 0, NULL)`,
  retrying 10 times at 100 ms because the pad can appear after title start.
- Every fatal path and normal shutdown goes through `app_exit()`:
  `sceSystemServiceLoadExec("exit", NULL)` followed by an idle loop. `main`
  never returns, so the libc `exit()` is never called.

New artifact: 16,457-byte signed FSELF, SHA-256
`25f7a74b7c0dfed3afae9c302c11b72c3cab9391ce0065ab8fa0a5389f22f482`.

## Black screen: unregistered VideoOut buffers (2026-10-31)

Third launch reached `entering run loop` with no error code, but the display
stayed black. The log showed every stage complete:

```
pad: user 518282432
pad: opened handle 55707392 on attempt 1
input ok
state ok
webkit: initialized (diagnostic renderer)
webkit ok
entering run loop
```

The software path copied the linear frame into a static `frame_buffer` that was
never registered with VideoOut, then called
`sceVideoOutSubmitFlip(handle, 0, 0, 0)` on buffer 0. No buffer was registered,
the flip mode was 0, and the data written was not the tiled layout the display
controller reads. The result is a valid black scanout.

`src/ps5_video.c` now follows PlaySurf's hardware-validated path:

- allocate two 16 MiB frame buffers with `sceKernelAllocateDirectMemory`
  (type 3, write-combined) and map them with `sceKernelMapDirectMemory`;
- describe them with `sceVideoOutSetBufferAttribute2` (RGBA8_SRGB, 1920x1080)
  and register with `sceVideoOutRegisterBuffers2`;
- present the black buffer first, then flip buffer 1 with flip mode 1;
- convert each linear frame through `ps5_tiled_group_offset` (16-byte groups),
  `clflush` the range, submit the flip with an increasing frame index, and wait
  for vblank.

`src/ps5_tiling.h` holds the pure tiling math. Host check: 518,400 groups,
all unique, maximum offset `0x86bff0`, inside the 16 MiB allocation.

New artifact: 18,153-byte signed FSELF, SHA-256
`cdb8428f2ca30419da6c3110b1d4a127f3260eff475dc0afa65d1f114a03df04`.

## Handover

`HANDOVER.md` in the repository root records the current state, every diagnosed launch failure, the exact build and deploy commands, and the next steps. Read it before continuing.

## Browser shell (2026-11-01)

The browser chrome comes from PlaySurf (`src/ui.c`, `src/browser_ui.c`,
GPL-3.0-or-later, BlackBearReloaded). It paints into the same linear RGBA frame
as the diagnostic renderer, so it needs no GL context and no new ABI: the pad
struct and button bits are identical.

**Home screen** follows the Firefox new-tab layout: search field on top, one
row of site tiles with labels, a bookmarks row below. "PlayKit" brand appears
above the search bar.

**Tab strip** (new): Firefox-style strip above the chrome bar. Group chip
("Browsing") with a colored dot, tabs showing favicon + title + close button,
"+" new-tab button, and a colored underline under the group's tabs. Tab groups
toggle via the menu ("Tab group"). D-pad UP from chrome enters tabs; DOWN
returns. The virtual cursor can click tabs, close, or new tab.

**Dark / light theme** (new): `ui_light` and `ui_dark` themes in `src/ui.c`;
menu item "Dark / light mode" toggles. Chrome, home, menu, panels, tab strip
and status pill all re-theme.

**Favicon fetcher** (new, `src/favicons.c` / `.h`): async libcurl worker fetches
`/favicon.ico` per site (https then http, 10s timeout), decodes ICO/PNG/DIB,
scales to 64x64, caches RGBA under `<state dir>/favicons`. The worker stays alive
on a condition variable so prefetches queued after `browser_ui_init` are served.
Home tiles draw the fetched icon with a globe fallback.

**Virtual cursor / crosshair** (new): left stick moves the crosshair over the
UI; the touchpad button or Cross clicks at the cursor position. Works on home
tiles, search bar, tab strip and chrome buttons. Always drawn on top.

Fonts are the baked `.huifont` SDF atlases, packaged under `assets/fonts` and
read from `/app0/assets/fonts` on the console. `make test-unit` builds the shell
on the host (`tests/host/`) against console stubs and drives the pad; console
rendering verified via the deployed signed eboot.

**Two pointers** The shell crosshair is the shell's pointer and the page cursor
belongs to the webpage. On a page the crosshair is clamped to the chrome; a
downward push at its bottom edge hands the stick to the page cursor, a ring that
is clamped to the page surface, and an upward push at the page's top hands back.
Clicks are routed by pointer: the page cursor produces
`browser_ui_take_click()` coordinates that `main.c` forwards to
`webkit_mouse_click()`, and the crosshair activates the chrome control under it
(back, forward, reload, address, menu, tab strip). The page cursor can therefore
never reach a shell control, and the crosshair can never click the page.

**JavaScriptCore** JSCOnly build stopped at `JSCBytecodeCacheVersion.cpp`:

```
error: unknown type name 'ElfW'
```

`PLATFORM(PLAYSTATION)` was false because the JSCOnly port does not define
`WTF_PLATFORM_PLAYSTATION`, so WebKit compiled the Unix branch that reads the
GNU build-id through `dl_iterate_phdr` and `ElfW`. The console libc declares
`dl_iterate_phdr` in `sys/link_elf.h` but exports neither it nor `ElfW`.
`platform/ps5_compat.h` now defines `WTF_PLATFORM_PLAYSTATION=1`, which selects
the PlayStation branch: a cache version hashed from `__TIMESTAMP__`. Because
that header is force-included in every translation unit, the whole build
recompiles once after the change.

A second JSC blocker: WTF defines its own `memmem` but the PS5 SDK's
`string.h` already declares one (differ only in return type → compiler error).
Fixed by not defining `HAVE_MEMMEM` on PlayStation in
`third_party/webkit/Source/WTF/wtf/PlatformHave.h`.

Resume with: `cd build/jsc && ninja -j1` (log at
`/tmp/opencode/jsc-build23.log`). `-j1` is deliberate: 7 GB RAM.

## JSC build configuration (2026-11-01)

Three defects stopped the JSCOnly configure and the first WTF compiles, all in
the build scripts rather than in WebKit:

- `tools/build-jsc.sh` computed `SDK` but never exported `PS5_PAYLOAD_SDK`, so
  the toolchain failed with `PS5_PAYLOAD_SDK is not set`.
- ICU was configured with `--prefix` pointing at its own build tree, which made
  `install(1)` copy files onto themselves. It now installs into
  `build/icu-ps5/install` and is built static only.
- `cmake/ps5-toolchain.cmake` sets `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY` with
  the payload SDK as the root, so `FindICU` could not see a prefix outside it:
  `Failed to find all ICU components (missing: ICU_INCLUDE_DIR ICU_LIBRARY ...)`.
  `ICU_ROOT` is now appended to `CMAKE_FIND_ROOT_PATH`.

The `char8_t` error was misdiagnosed earlier. WebKit 2.48 uses the C++20
`char8_t` type and `std::u8string` directly (`FileSystem.cpp`,
`UTF8Conversion.h`), and the SDK's libc++ provides both. A PlayStation
`HAVE_MISSING_U8STRING` override in `PlatformHave.h` plus `-fno-char8_t` made
those types disappear, which produced both the original
`no matching function for call to 'unsafeSpan8'` and the later
`use of undeclared identifier 'char8_t'`. Both were removed; the code now takes
the ordinary `std::u8string` path. The ICU flags lost `-fno-char8_t` too, for
consistency.

The build is resumable at any point: rerun `tools/build-jsc.sh`; it skips ICU
once the libraries exist and leaves the ninja tree in place.
