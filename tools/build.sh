#!/usr/bin/env bash
# PlayKit - Native PS5 application build.
# Compiles, links, signs, validates, and assembles the app folder.
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:-Folder}
[[ ${output,,} == folder ]] || {
    echo "usage: tools/build.sh [Folder]" >&2
    exit 2
}

sdk_root="${PS5_PAYLOAD_SDK:-/root/projects/PlaySurf/.deps/native/ps5-payload-sdk}"
[[ -d $sdk_root ]] || {
    echo "PS5 payload SDK not found at $sdk_root; set PS5_PAYLOAD_SDK." >&2
    exit 2
}

title_id="${TITLE_ID:-PPSA90002}"

build="$root/build"
dist="$root/dist"
app="$dist/$title_id"
native="$root/tooling/native"
target_compiler="${PS5_CLANG:-clang-18}"

ps5_opengl_prefix="${PS5_OPENGL_PREFIX:-$root/build/sdk/ps5-opengl-gl46}"
ps5_lib_dir="$sdk_root/target/lib"

mkdir -p "$build/obj" "$dist"

# Collect app sources.
sources=()
for source in "$root"/src/*.c; do
    [[ -f $source ]] || continue
    sources+=("${source#$root/}")
done
(( ${#sources[@]} > 0 )) || { echo "src/ has no C sources" >&2; exit 2; }

definitions=()
includes=("src")
archives=()
import_stubs=()
[[ -z ${APP_DEFINITIONS:-} ]] || read -r -a definitions <<< "$APP_DEFINITIONS"
[[ -z ${APP_INCLUDE_PATHS:-} ]] || read -r -a includes <<< "$APP_INCLUDE_PATHS"
[[ -z ${APP_IMPORT_STUBS:-} ]] || read -r -a import_stubs <<< "$APP_IMPORT_STUBS"
[[ -z ${PACBREW_PACKAGES:-} ]] || read -r -a pacbrew_packages <<< "$PACBREW_PACKAGES"

# PacBrew ports (libcurl and its TLS/zlib dependencies) come from the pinned
# sysroot.  pkg-config is resolved once here; compile flags and link flags are
# kept separate because the raw linker refuses -D entries.
pacbrew_cflags=()
pacbrew_libs=()
if (( ${#pacbrew_packages[@]} > 0 )); then
    pacbrew_resolution=$(bash "$root/tools/setup-pacbrew-dependencies.sh" \
        --resolve "${pacbrew_packages[@]}")
    mapfile -d '' -t pacbrew_cflags < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["cflags"]]' \
        "$pacbrew_resolution")
    mapfile -d '' -t pacbrew_libs < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["libs"] if not v.startswith("-D") and v != "-lpthread"]' \
        "$pacbrew_resolution")
    printf 'PacBrew dependencies: %s\n' "${pacbrew_packages[*]}"
fi

# ps5-opengl public headers (optional).
if [[ -d $ps5_opengl_prefix/include ]]; then
    includes+=("$ps5_opengl_prefix/include")
fi

# Enable EGL/OpenGL code path if ps5-opengl SDK is available.
if [[ -d $ps5_opengl_prefix/include ]]; then
    definitions+=("HAVE_EGL")
fi

definitions+=("_POSIX_C_SOURCE=200809L")

# Wrap symbols for heap interposition (ps5_heap.c wraps malloc/free/etc.).
wrap_options=()
for symbol in ${APP_WRAP_SYMBOLS:-malloc calloc realloc free posix_memalign memalign aligned_alloc malloc_usable_size}; do
    wrap_options+=("--wrap=$symbol")
done

# Compile app sources.
objects=()
for source in "${sources[@]}"; do
    object="$build/obj/$source.o"
    mkdir -p "$(dirname "$object")"
    args=(-std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
    for definition in "${definitions[@]}"; do
        args+=("-D$definition")
    done
    for include in "${includes[@]}"; do
        args+=("-I$root/$include")
    done
    args+=("${pacbrew_cflags[@]}")
    PS5_PAYLOAD_SDK="$sdk_root" PS5_CLANG="$target_compiler" \
        sh "$root/tools/prospero-clang18" "${args[@]}" \
        -MD -MF "$object.d" -c "$root/$source" -o "$object"
    objects+=("$object")
done

# Compile startup object and C++ runtime.
startup_objects=()
for name in app_crt app_cpp_runtime; do
    object="$build/obj/$name.o"
    PS5_PAYLOAD_SDK="$sdk_root" PS5_CLANG="$target_compiler" \
        sh "$root/tools/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffunction-sections -fdata-sections -MD -MF "$object.d" \
        -c "$native/$name.cpp" -o "$object"
    startup_objects+=("$object")
done

# Extension SDK imports must reach both the linker and the metadata converter.
# A stub is a link-only shared object built with prospero-lld --shared and a
# PS5 soname; the converter records the matching system module import, and the
# console binds the call to the real function at launch.
stub_paths=()
stub_options=()
for stub in "${import_stubs[@]}"; do
    [[ $stub =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.so$ && -f $root/$stub ]] || {
        echo "invalid import stub: $stub" >&2
        exit 2
    }
    stub_paths+=("$root/$stub")
    stub_options+=(--stub "$root/$stub")
done

# Link.
link_inputs=("${startup_objects[@]}" "${objects[@]}" "${wrap_options[@]}" "${stub_paths[@]}")
link_inputs+=("$ps5_lib_dir/libScePad.so" "$ps5_lib_dir/libSceVideoOut.so")
link_inputs+=("$ps5_lib_dir/libSceSystemService.so" "$ps5_lib_dir/libSceUserService.so")
link_inputs+=("$ps5_lib_dir/libkernel.so")
# Native titles use the console's libc module. Do not whole-archive the
# payload SDK's libc.a: it drags in dlfcn.o, whose weak __dlopen/__dlsym
# references the native converter will not import.
link_inputs+=(-lkernel -lSceLibcInternal -lunwind -lc++ -lc++abi)
link_inputs+=("${pacbrew_libs[@]}")

echo "==> [link] $title_id"
"$sdk_root/bin/prospero-lld" -T "$native/ps5-pie.ld" --eh-frame-hdr \
    "${wrap_options[@]}" --version-script "$native/app-symbols.map" \
    -e _start -o "$build/llvm-pie.elf" "${link_inputs[@]}" \
    -L"$ps5_lib_dir" --as-needed "$ps5_lib_dir"/*.so

# Assemble app folder first (signing writes directly into it).
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
cp "$root/sce_sys/param.json" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $root/sce_sys/$asset ]] && cp "$root/sce_sys/$asset" "$app/sce_sys/$asset"
done
[[ -d $root/assets ]] && cp -a "$root/assets" "$app/assets"

# Host-side converter/signer. Build it on demand from the sources in
# tooling/native so a clean checkout does not depend on a prebuilt binary.
host_tool="$build/host/ps5-native-tool"
if [[ ! -x $host_tool ]]; then
    echo "==> [host] Building ps5-native-tool"
    mkdir -p "$build/host"
    zlib_archive="${PS5_HOST_ZLIB:-}"
    if [[ -z $zlib_archive ]]; then
        zlib_archive=$(ls /usr/lib/*/libz.a 2>/dev/null | head -1 || true)
    fi
    if [[ -z $zlib_archive ]]; then
        zlib_archive=/root/projects/PlaySurf/.deps/native/zlib/root/usr/lib/libz.a
    fi
    [[ -f $zlib_archive ]] || {
        echo "zlib static library not found; set PS5_HOST_ZLIB to libz.a" >&2
        exit 2
    }
    ${HOST_CXX:-clang++} -std=c++20 -O2 -Wall -Wextra -Werror \
        "$native/native_app_builder.cpp" "$native/self_container.cpp" \
        "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
        "$zlib_archive" -o "$host_tool"
fi

# Convert to PS5 ELF format, then sign as SELF directly into the app folder.
# Both steps are mandatory: an unsigned ELF named eboot.bin cannot launch.
echo "==> [sign] $title_id"
"$host_tool" link --in "$build/llvm-pie.elf" \
    --out "$build/eboot.elf" "${stub_options[@]}" \
    --stub "$ps5_lib_dir/libkernel.so" \
    --stub-dir "$ps5_lib_dir" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
"$host_tool" self --sign --in "$build/eboot.elf" \
    --out "$app/eboot.bin" --magic 0x1D3D154F

# Runtime module: required by the loader for the libc contract.
cp "$root/runtime/libc.prx" "$app/sce_module/libc.prx"

printf 'Build complete.\nApp folder: %s\n' "$app"
