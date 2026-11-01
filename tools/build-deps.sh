#!/usr/bin/env bash
# Build the graphics dependencies WebKit needs, for the PS5.
#
# Everything lands in build/ps5 as static archives:
#   pixman -> cairo -> libwpe
#
# Cairo is the painter WebKit uses; pixman is its rasteriser; libwpe is the
# thin platform interface the WPE port (the one without GTK) links against.
# The remaining libraries (freetype, fontconfig, harfbuzz, libpng, zlib,
# libxml2, libjpeg, libwebp) come prebuilt from the pinned PacBrew ports tree,
# so this script only builds what PacBrew does not ship.
#
# Every stage is resumable: a stage whose archive already exists is skipped.
#
# Usage: tools/build-deps.sh
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${PS5_PAYLOAD_SDK:-/root/projects/PlaySurf/.deps/native/ps5-payload-sdk}
PREFIX=$ROOT/build/ps5
JOBS=${JOBS:-4}
TRIPLE=x86_64-sie-ps5
# autoconf config.sub does not know the console, so the stage triples are
# reported as FreeBSD; clang --target decides the real target.
HOST_TRIPLE=x86_64-unknown-freebsd

if [[ ! -d $SDK ]]; then
    echo "PS5 payload SDK not found at $SDK" >&2
    exit 1
fi

# The SCE clang finds prospero-lld through PATH.
export PATH="$SDK/bin:$PATH"

# PacBrew provides the prebuilt ports (freetype, fontconfig, harfbuzz, png,
# zlib, libxml2, ...). Its .pc files are written for /user/homebrew, so
# PKG_CONFIG_SYSROOT_DIR has to point at the extracted tree.
PACBREW=$("$ROOT/tools/setup-pacbrew-dependencies.sh" --all)
export PKG_CONFIG_DIR=
export PKG_CONFIG_SYSROOT_DIR=$PACBREW
export PKG_CONFIG_LIBDIR=$PACBREW/user/homebrew/lib/pkgconfig:$PACBREW/user/homebrew/libdata/pkgconfig:$PREFIX/lib/pkgconfig
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig

CC_FOR_PS5="clang-18 --target=$TRIPLE --sysroot=$SDK -isystem $SDK/target/include -fno-stack-protector -fno-plt -femulated-tls"
CXX_FOR_PS5="clang++-18 --target=$TRIPLE --sysroot=$SDK -isystem $SDK/target/include/c++/v1 -isystem $SDK/target/include -fno-stack-protector -fno-plt -femulated-tls"
# -L order matters: the ports tree before the SDK, so -lfreetype finds the real
# library instead of nothing.
LD_FOR_PS5="-L$PACBREW/user/homebrew/lib -L$SDK/target/lib"

export CC="$CC_FOR_PS5" CXX="$CXX_FOR_PS5" LD="$LD_FOR_PS5"
export CFLAGS="-O2 -fPIC"
export CXXFLAGS="-O2 -fPIC -frtti"
mkdir -p "$PREFIX"

# Meson needs its compiler and pkg-config paths in a cross file. It is written
# here because every path is absolute and machine specific.
cat > "$ROOT/build/ps5-cross.ini" <<EOF
[binaries]
c = 'clang-18'
cpp = 'clang++-18'
ar = 'llvm-ar'
strip = 'llvm-strip'
pkg-config = 'pkg-config'

[properties]
needs_exe_wrapper = true
sys_root = '$SDK'
pkg_config_libdir = '$PACBREW/user/homebrew/lib/pkgconfig:$PREFIX/lib/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[built-in options]
c_args = ['--target=$TRIPLE', '--sysroot=$SDK', '-isystem', '$SDK/target/include', '-fno-stack-protector', '-fno-plt', '-femulated-tls', '-O2', '-fPIC']
cpp_args = ['--target=$TRIPLE', '--sysroot=$SDK', '-isystem', '$SDK/target/include/c++/v1', '-isystem', '$SDK/target/include', '-fno-stack-protector', '-fno-plt', '-femulated-tls', '-frtti', '-O2', '-fPIC']
c_link_args = ['-L$PACBREW/user/homebrew/lib', '-L$SDK/target/lib']
cpp_link_args = ['-L$PACBREW/user/homebrew/lib', '-L$SDK/target/lib']
EOF

echo "==> [deps] prefix $PREFIX, pkg-config sysroot $PACBREW"

pixman() {
    [[ -f $PREFIX/lib/libpixman-1.a ]] && { echo "==> [pixman] already built"; return; }
    echo "==> [pixman] configure and build"
    local src=$ROOT/third_party/pixman-0.42.2
    local build=$ROOT/build/pixman
    if [[ ! -f $build/Makefile ]]; then
        mkdir -p "$build"
        (cd "$build" && "$src/configure" \
            --host=$HOST_TRIPLE --prefix="$PREFIX" \
            --disable-shared --enable-static --disable-dependency-tracking \
            --disable-gtk --disable-tests --disable-libpng \
            --disable-arm-a64-neon --disable-arm-neon)
    fi
    make -C "$build" -j"$JOBS"
    make -C "$build" install
}

cairo() {
    [[ -f $PREFIX/lib/libcairo.a ]] && { echo "==> [cairo] already built"; return; }
    echo "==> [cairo] configure and build"
    local src=$ROOT/third_party/cairo-1.16.0
    local build=$ROOT/build/cairo
    if [[ ! -f $build/Makefile ]]; then
        mkdir -p "$build"
        (cd "$build" && "$src/configure" \
            --host=$HOST_TRIPLE --prefix="$PREFIX" \
            --disable-shared --enable-static --disable-dependency-tracking \
            --disable-xlib --disable-xcb --disable-xlib-xrender \
            --disable-quartz --disable-quartz-font --disable-quartz-image \
            --disable-win32 --disable-win32-font \
            --disable-pdf --disable-ps --disable-svg --disable-script \
            --disable-egl --disable-glx --disable-glesv2 --disable-gallium \
            --disable-cogl --disable-gobject --disable-valgrind \
            --disable-trace --disable-symbol-lookup \
            --enable-ft --enable-fc --enable-png)
    fi
    make -C "$build" -j"$JOBS"
    make -C "$build" install
}

libwpe() {
    [[ -f $PREFIX/lib/libwpe-1.0.a ]] && { echo "==> [libwpe] already built"; return; }
    echo "==> [libwpe] configure and build"
    local src=$ROOT/third_party/libwpe-1.16.2
    local build=$ROOT/build/libwpe
    if [[ ! -f $build/build.ninja ]]; then
        mkdir -p "$build"
        meson setup "$build" "$src" \
            --prefix="$PREFIX" \
            --libdir=lib \
            --buildtype=release \
            --default-library=static \
            --cross-file "$ROOT/build/ps5-cross.ini"
    fi
    ninja -C "$build"
    ninja -C "$build" install
}

pixman
cairo
libwpe

echo "==> [deps] done:"
ls -l "$PREFIX"/lib/*.a
