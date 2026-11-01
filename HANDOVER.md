# PlayKit handover

Last updated: 2026-10-31 20:10 UTC, by the current agent. Read this file fully before
touching anything. It contains the current state, the exact commands, and every
failure already diagnosed so you do not repeat them.

## 1. What this project is

`/root/projects/PlayKit` is a port of **WebKit 2.48.0** to the **PlayStation 5**
as a **standalone native title** (not an ELF payload, not a Linux emulator).

The goal is a real browser on the console: HTML5, CSS, JavaScriptCore, DOM,
HTTPS/TLS, images and fonts, video where practical, downloads, cookies, storage,
history. Engine code stays separate from PS5 platform code. The graphics backend
must stay modular: the current presenter is a CPU tiler into VideoOut; an
OpenGL 4.6 backend via EGL (`ps5-opengl`) is the intended fast path later.

The project is **not a git repository**. There is no undo. Read a file before
replacing it. Preserve existing work in `third_party/`, `platform/`, and the
WebKit PlayStation-specific sources.

The status page must stay current, must show real results only (no invented
success), and must be served on port 80 with a working progress bar and renders.

## 2. User constraints (important)

- **There are no LTE or data limits.** The user confirmed on 2026-10-31 that
  mobile-data limits no longer apply. Transfers, screenshots and Remote Play are
  not restricted by bandwidth or cost.
- The user said they are heading home, so the console should also answer on the
  LAN at `192.168.2.15`. Prefer the LAN address when it responds; it is faster
  and avoids the public path.
- A "stop console traffic" choice was made earlier the same day while the user
  was away. Treat it as lifted now that the user has said there are no limits and
  is going home. If the console does not answer on either address, stop and ask
  rather than retrying.
- Normal transfer rules still apply: verify hashes, and never publish an
  unsigned `eboot.bin`.
- Do not claim a hardware success that was not observed. Screenshots and the
  console log are evidence; guesses are not.
- The user launches the app manually from the PS5 home screen. Remote Play input
  has been view-only ("hold Options to take control" blocks Cross/D-pad).
- The user speaks English and sometimes French (console error text). Keep replies
  short; the caveman writing style is active in chat, but **files and code stay in
  normal prose**.

## 3. Environment

- Dev box: Linux, 7 GB RAM, about 13 GB free disk, no git repo.
- Compiler: `clang-18`, target `x86_64-sie-ps5`, linker `prospero-lld`.
- PS5 payload SDK: `/root/projects/PlaySurf/.deps/native/ps5-payload-sdk`
  (`PS5_PAYLOAD_SDK`).
- ps5-opengl source (unbuilt): `/tmp/opencode/ps5-opengl`.
- PS5_Vulkan reference tree: `/tmp/opencode/PS5_Vulkan` (contains
  `runtime/libc.prx`, `ps5-native-tool`, docs).
- Homebrew starter reference: `/tmp/opencode/ps5-homebrew-starter/kit`.
- PS5 agent MCP: `/root/projects/he` with venv `/root/projects/he/.venv`.
  Config: `/root/.config/opencode/opencode.jsonc`, `PS5_HOST=184.145.228.235`.
- Console: firmware `12020000` (12.02), jailbroken.
  - Public IP: `184.145.228.235`
  - LAN IP (when home): `192.168.2.15`
  - FTP: port `2121`, user `anonymous`, password `codex`
  - Remote Play: port `9295`
  - ps5debug / elfldr: port `9021`
- Title identity: `PPSA90002`, concept id `90002`, content id
  `UP9000-PPSA90002_00-PLAYKIT000000000`.
- Installed app folder on console: `/data/homebrew/PPSA90002/`
  containing `eboot.bin`, `sce_module/libc.prx`, `sce_sys/param.json`,
  `sce_sys/icon0.png`.
- Console log file: `/data/playkit.log` (fallback `/app0/playkit.log`).

## 4. Current state (what actually works)

- A valid **signed FSELF** builds and launches on the console.
- The title initialises heap, VideoOut, pad, state directory, and the diagnostic
  renderer, then enters the run loop.
- The console shows the **diagnostic frame**: dark slate background, blue border,
  centre crosshair.
- Console log from the successful run:

```
start
heap: 384 MiB mapped, 0 KiB live, 0 KiB peak, 0 failed
video: built without EGL; using software blit
video ok
pad: user 518282432
pad: opened handle 55707392 on attempt 1
input ok
state ok
webkit: initialized (diagnostic renderer)
webkit ok
entering run loop
```

  Note: the "software blit" line is from the older build. The current build logs
  `video: ... byte buffers registered ...` after the VideoOut rewrite.
- The **PlaySurf browser shell** is integrated (`src/ui.c`, `src/browser_ui.c`).
  Home screen follows the Firefox new-tab layout: search on top, one row of
  site tiles with labels, bookmarks row below, controller hint row. The
  controller keyboard, menu, tabs, bookmarks and downloads panels all render
  and are host-tested. It uses the same pad ABI and RGBA frame; no OpenGL.
  Fonts are baked Google Sans and DejaVu `.huifont` atlases in `assets/fonts`.
  Rendering is verified on the host (`tests` harness copied from PlaySurf),
  not yet on the console.
- Current signed artifact on the build machine:
  - `dist/PPSA90002/eboot.bin`, **63,305 bytes**
  - SHA-256 `34eff555415f9d2ac65be25b7e94dd0788d1b313646dd0fca0c09fb606b8dceb`
  - signed, plaintext, 12 segments, integrity valid, authority
    `0x3100000000000002`
  - convenient copies: `/root/projects/PlayKit/PLAYKIT-SIGNED-EBOOT.bin`
    and `/root/projects/PlayKit/status/PLAYKIT-SIGNED-EBOOT.bin`

## 5. What is running right now

A JavaScriptCore static-library build is running in the background:

- Directory: `/root/projects/PlayKit/build/jsc`
- Command: `ninja -j1`
- Log: `/tmp/opencode/jsc-build22.log`
- Progress: 112/963 objects were built, then the build stopped on
  `JSCBytecodeCacheVersion.cpp`: `unknown type name 'ElfW'`. Root cause: the
  JSCOnly CMake port did not define `WTF_PLATFORM_PLAYSTATION`, so the
  Unix-only branch using `dl_iterate_phdr` and `ElfW` was compiled. The console
  libc declares `dl_iterate_phdr` but exports neither it nor `ElfW`.
- Fix applied: `platform/ps5_compat.h` now defines
  `WTF_PLATFORM_PLAYSTATION=1`, selecting the PlayStation branch (precomputed
  cache version). The build was restarted and is recompiling from the start
  because the forced-include header changed.
- `-j1` is deliberate: the machine has 7 GB RAM and WebKit C++ translation
  units are large. Earlier runs died with no compiler error (host/session kill).
- Warnings about `GigacageConfig.h` (`extern "C" Slot g_config[]`) are benign.

Do not kill this build without reason. If it dies, rerun `ninja -j1` in
`build/jsc`; ninja resumes.

## 6. Repo layout and ownership

```
src/main.c              app entry, log, init order, run loop, app_exit()
src/ps5_video.c         VideoOut init, tiled buffers, present, stop
src/ps5_tiling.h        pure tiling math (16-byte pixel groups, 128x128 blocks)
src/ps5_pad.c           scePad init via user service, polling, button mapping
src/ps5_heap.c/h        sceLibcMspace heap (~384 MiB), malloc wrappers
src/ps5.h               console imports (VideoOut, Pad, UserService, kernel)
src/ps5_paths.h         /download0 state paths
src/webkit_glue.c       diagnostic painter; later real WebKit paint bridge
src/webkit_glue.h       browser API surface used by main
Makefile                `make app`, `make deploy`, `make undeploy`
tools/build.sh          compile, link, convert, sign, assemble dist folder
tools/deploy.sh         atomic FTP deploy/undeploy
tools/prospero-clang18  compiler wrapper
tooling/native/         app_crt.cpp, app_cpp_runtime.cpp, app-symbols.map,
                        ps5-pie.ld, ps5-native-tool sources
runtime/libc.prx        clean-room signed libc module copied into dist
sce_sys/param.json      title metadata
docs/architecture.md    SDK audit, every launch failure and fix
status/index.html       live status page
status/renders/         render images used by the page
third_party/webkit/     WebKit 2.48.0 sparse checkout
build/jsc/              JavaScriptCore CMake build tree
dist/PPSA90002/         deployable app folder (eboot.bin is SIGNED)
build/eboot.elf         intermediate unsigned ELF — NEVER copy to console
```

## 7. Build and verify

```sh
cd /root/projects/PlayKit
make app
```

This compiles `src/*.c`, links with `prospero-lld`, converts the PIE to a PS5
native image, signs it with `build/host/ps5-native-tool`, and assembles
`dist/PPSA90002/`.

Verify before giving the file to anyone:

```sh
./build/host/ps5-native-tool self --inspect --file dist/PPSA90002/eboot.bin
```

Expected: `container: signed, plaintext`, 12 segments, `integrity: valid`,
authority `0x3100000000000002`.

Check the file identity:

```sh
stat -c %s dist/PPSA90002/eboot.bin     # must be ~18 KB, not ~75 KB
od -An -tx1 -N8 dist/PPSA90002/eboot.bin
# signed FSELF: 4f 15 3d 1d 00 01 01 12
# unsigned ELF: 7f 45 4c 46 02 01 01 09   <-- wrong file
```

Never hand over `build/eboot.elf`. It is the pre-signing image and the console
rejects it. This mistake happened twice and produced `CE-100008-9` and
`CE-105773-3`.

## 8. Deploy (only with user approval)

```sh
make deploy PS5_HOST=184.145.228.235
```

`tools/deploy.sh` uploads under temporary names, promotes `eboot.bin` and
`param.json` last, then verifies both. `make undeploy PS5_HOST=...` removes only
this title.

Reality check: FTP deploys to the public IP have repeatedly timed out mid-upload
(1.28 MB `libc.prx` plus assets). If a deploy fails, do not retry blindly — it
costs the user data. Check what is already there with metadata only:

```sh
curl -s -I -u anonymous:codex \
  "ftp://184.145.228.235:2121/data/homebrew/PPSA90002/eboot.bin"
```

`Content-Length` is enough to tell whether the file is the signed 18,153-byte
image or a ~75 KB ELF. Do not download the file unless necessary.

To fetch the tiny log (about 300 bytes, acceptable):

```sh
curl -s -u anonymous:codex \
  "ftp://184.145.228.235:2121/data/playkit.log"
```

## 9. Launch failures already diagnosed — do not repeat

### CE-100008-9 (unsigned app)

The deployed `eboot.bin` was a 164,536-byte raw ELF. Root cause:
`tooling/native/app-symbols.map` exported every symbol (`global: *;`), the
converter refused the image, and the build silently fell back to the unsigned
PIE.

Fixes, all still in place:
- `app-symbols.map` contains only `local: *;`.
- The build aborts if conversion or signing fails.
- The link does **not** whole-archive the payload SDK's `libc.a`; it links the
  console module `-lSceLibcInternal`. Whole-archiving pulled in `dlfcn.o`, whose
  weak `__dlopen`/`__dlsym`/`__dlerror` the converter refuses.

### CE-108255-1 / signal 12 SIGSYS

`RIP 0x00000008000003ac`, `backtrace[0] 0x000000000040017c`. The crash was the
libc `exit()` called by the startup object after `main` returned. `main` returned
because pad init failed:

```
pad: scePadOpen failed: -2137325439   (0x809B0081)
```

Fixes:
- `ps5_pad_start()` calls `sceUserServiceInitialize(NULL)`, then
  `sceUserServiceGetInitialUser(&user)`, then `scePadOpen(user, 0, 0, NULL)`,
  retrying 10 times at 100 ms.
- `ps5_pad.c` logs the user id and the handle.
- `src/main.c` has a `_Noreturn app_exit()` that calls
  `sceSystemServiceLoadExec("exit", NULL)` and then idles forever. All fatal
  paths and normal shutdown use it. **Never `return` from `main`.** Never call
  libc `exit()` in a native title.

### CE-105773-3 (wrong file)

The console had `build/eboot.elf` (74,484 bytes, magic `7f 45 4c 46`) copied as
`eboot.bin`. The system refuses to start it. Replace with the signed 18,153-byte
FSELF, magic `4f 15 3d 1d`.

### Black screen with no error code

The app ran, the log reached `entering run loop`, but the screen stayed black.
The old `ps5_video.c` copied frames into a private buffer that was never
registered with VideoOut and submitted a flip on an unregistered buffer with
flip mode 0.

Fix (current implementation, ported from PlaySurf's hardware-validated path):
- allocate two 16 MiB frame buffers with `sceKernelAllocateDirectMemory`
  (memory type 3, write-combined) and map with `sceKernelMapDirectMemory`;
- describe with `sceVideoOutSetBufferAttribute2` (`PIXEL_FORMAT_RGBA8_SRGB`,
  1920x1080, tiling 0) and register with `sceVideoOutRegisterBuffers2`
  (2 buffers);
- submit the black buffer first with flip mode 1, wait vblank;
- per present: tile the linear frame with `ps5_tiled_group_offset` in 16-byte
  groups, `clflush` the 16 MiB range, submit flip with an increasing frame index,
  wait vblank, swap front/back.

Host check of the tiling: 518,400 groups, all unique, maximum offset `0x86bff0`,
inside the 16 MiB allocation.

Pixel order: VideoOut reads RGBA bytes, so a little-endian `uint32_t` frame value
is `0xAABBGGRR`, not `0xAARRGGBB`. The diagnostic painter uses corrected values.
If a future renderer writes Cairo ARGB32, the bytes are already B,G,R,A and need
no swap.

## 10. What has been done, in order

1. Audited PlayKit, PlaySurf, ps5-payload-sdk, ps5-native-app-boilerplate,
   ps5-opengl, PS5_Vulkan.
2. Chose the boilerplate native-app model: `prospero-lld` + `ps5-pie.ld`,
   `app_crt.cpp` + `app_cpp_runtime.cpp`, `ps5-native-tool self --sign`,
   `libc.prx`, folder deploy under `/data/homebrew/<TITLE_ID>/`.
3. Built host dependencies and static libs for PS5: ICU 76.1, Pixman 0.42.2,
   Cairo 1.16.0, libwpe 1.16.2. All installed under `build/`.
4. Wrote the native app: heap, video, pad, paths, WebKit glue, run loop.
5. Wrote the build system and fixed conversion/signing:
   - `app-symbols.map` = `local: *;`
   - link `libSceLibcInternal`, not `libc.a`
   - abort on converter/signing failure
   - `APP_IMPORT_STUBS` supported (link-only `.so` stubs, e.g.
     `libSceCommonDialog.so`, for system modules missing from the SDK)
   - deploy refuses to publish an unsigned eboot
6. Ported the safe FTP deploy script from `MiladRTX/ps5-homebrew-starter`.
7. Got the title running on the console and fixed the three launch failures and
   the black scanout described above.
8. Wrote `docs/architecture.md` with the SDK audit and each failure.
9. Wrote and updated `status/index.html` with a current-state banner, a progress
   bar computed from item tags, and a render.

## 11. Status page rules

File: `/root/projects/PlayKit/status/index.html`.

Serve it with:

```sh
cd /root/projects/PlayKit/status
python3 -m http.server 80 --bind 0.0.0.0
```

Check: `curl -s -o /dev/null -w '%{http_code}\n' http://localhost:80/` → `200`.

The progress bar is computed in the page script from the tags:

- `<span class="tag ok">` → done, weight 1
- `<span class="tag work">` → active, weight 0.5
- `<span class="tag todo">` → not started, weight 0

Percent = `round((done + active*0.5) / total * 100)`.
At handover: 26 done, 3 active, 3 todo, **86 %**.

Only put an item under "Done and verified" when it is actually built and, where
applicable, observed on the console. Keep the note text specific: commands,
error codes, byte sizes, hashes. Update the timestamp in `.sub` and the banner
when state changes. Renders live in `status/renders/` and are referenced with
relative paths.

## 12. Next steps (in order)

1. **Finish JavaScriptCore.** Let `ninja -j1` finish in `build/jsc` after the
   `WTF_PLATFORM_PLAYSTATION` fix. Confirm
   `libJavaScriptCore.a` exists, then link it into a host-side test or the app
   and check symbols. Fix build errors as they appear; do not restart from
   scratch unless the tree is corrupted.
2. **Build WebKit static libraries.** WebCore, WTF, and the WPE/WebKit layer
   against the PlayStation toolchain, reusing the PlayStation-specific code in
   `third_party/webkit` and `platform/ps5_compat.*`. Expect missing Sony private
   APIs; see `docs/architecture.md` for replacements.
3. **Replace the diagnostic painter with real page paint.** Expected path:
   `WKPagePaint` into a Cairo ARGB32 image surface, then copy into the frame
   buffer that `ps5_video_present()` tiles. Sony's MiniBrowser does this without
   GL.
4. **Browser shell.** Integrated from PlaySurf: address bar, home screen,
   tabs, on-screen keyboard, bookmarks, downloads, menu. Remaining: wire its
   navigation actions to real WebKit history and page loads, and decide whether
   to keep the shell keyboard or use `sceCommonDialog` for page fields.
5. **Downloads, cookies, storage.** Data stores under `/download0`.
6. **ps5-opengl backend (optional fast path).** Build the SDK in
   `/tmp/opencode/ps5-opengl` (`make sdk-gl46`), link `libPS5OpenGL.a` and the
   AGC modules, and swap the CPU tiler for EGL + `eglSwapBuffers` behind the same
   `ps5_video_present()` API. Do not break the working CPU path until the GL path
   is verified on the console.
7. **WebKit2 multiprocess (deferred).** The current app is single-process. A
   real WPE UI process needs a PS5 process launcher; see the research notes in the
   status page.
8. **Video playback.** Practical only after the engine and shell work. Test
   H.264/AAC in a page; remote streaming services will likely block the user
   agent or DRM.

## 13. Build-system gotchas

- `prospero-lld` rejects `-Wl,` prefixes. Pass raw linker flags.
- Do not wrap `fcntl` unless a `__wrap_fcntl` exists.
- `ps5-native-tool` must be built from local `tooling/native` sources plus a
  host `libz.a`.
- `tools/deploy.sh` publishes `eboot.bin` and `param.json` last and verifies
  both. Keep that property; a half-updated folder can fail to launch.
- `APP_IMPORT_STUBS` was tested end to end with a throwaway
  `libSceCommonDialog.so` and then removed. Rebuild stubs when needed with:
  `prospero-lld --shared -soname libSceCommonDialog.sprx`.
- The link must stay PIE with the layout in `tooling/native/ps5-pie.ld`; the
  converter expects it.
- `sceVideoOutWaitVblank` throttles the loop to 60 fps. Keep it, or the app
  spins and floods the flip queue.
- Direct memory for video is 32 MiB (2 × 16 MiB). The libc heap is separate
  (~384 MiB via `sceLibcMspace`). Do not allocate direct memory from the heap.
- `scePadOpen(0, ...)` fails with `0x809B0081`; use the logged-in user id.
- Pad button bits are defined in `src/ps5.h`. Circle is `0x002000`; holding L3+R3
  and pressing Circle both quit the run loop.

## 14. Console testing procedure

1. Confirm the console is reachable (TCP connect only, no transfer):
   ports `9295`, `9021`, `2121` at `184.145.228.235`.
2. Check the remote eboot size via FTP metadata; it must be 18,153 bytes.
3. Ask the user to launch PlayKit from the home screen. They do this manually;
   Remote Play input is view-only.
4. After launch, fetch `/data/playkit.log` (small) and read the last line.
   `entering run loop` means the app is alive. `fatal: ...` means an init failed
   and the app called `app_exit()`.
5. If the screen is wrong, take a Remote Play snapshot only with the user's
   agreement (it uses their data). Otherwise ask what they see.
6. Record the result on the status page, including the log line and any error
   code.

## 15. Files and facts worth quoting

- Signed eboot: 63,305 bytes,
  SHA-256 `34eff555415f9d2ac65be25b7e94dd0788d1b313646dd0fca0c09fb606b8dceb`.
- Build folder: `dist/PPSA90002/` — `eboot.bin`, `sce_module/libc.prx`,
  `sce_sys/param.json`, `sce_sys/icon0.png`.
- `sce_sys/param.json`: title id `PPSA90002`, concept id `90002`, content id
  `UP9000-PPSA90002_00-PLAYKIT000000000`, title name `PlayKit`, category game.
- `runtime/libc.prx`: clean-room signed module from PS5_Vulkan, validated on
  firmware 6.02 and 12.70 (SHA-256 starts `e6ff45d1`).
- `tools/build.sh` exports: `PS5_PAYLOAD_SDK`, `PS5_CLANG`, `APP_IMPORT_STUBS`,
  `APP_STATIC_ARCHIVES`, `APP_WRAP_SYMBOLS`.
- `APP_WRAP_SYMBOLS` defaults to
  `malloc calloc realloc free posix_memalign memalign aligned_alloc malloc_usable_size`
  for the heap interposition.

## 16. Style and safety rules for the next agent

- Read `docs/architecture.md` and this file before changing code.
- Investigate before deleting or overwriting. No VCS means deletions are final.
- Keep changes small and consistent with the existing C style: 4 spaces, braces
  on the same line, `ps5_log()` for diagnostics, no comments that restate code.
- Every new console workaround gets a paragraph in `docs/architecture.md` with
  the exact error code and the reason.
- Do not upload anything to the console without asking the user. Do not stream
  Remote Play for long periods; it costs them data.
- Do not publish an unsigned eboot. Do not claim success without the log or a
  visible result.
