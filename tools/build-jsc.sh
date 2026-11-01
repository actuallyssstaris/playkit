#!/usr/bin/env bash
# Build JavaScriptCore for the PS5 payload SDK.
#
# Two stages, both resumable:
#   1. ICU for the console (host ICU first, because ICU's build tools run on
#      this machine even when the libraries target the PS5).
#   2. JSCOnly with the PlayKit toolchain, single job: this box has 7 GB RAM
#      and one clang++ at a time is what fits.
#
# Usage: tools/build-jsc.sh [--configure-only]
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${PS5_PAYLOAD_SDK:-/root/projects/PlaySurf/.deps/native/ps5-payload-sdk}
ICU_SRC=$ROOT/third_party/icu/source
ICU_HOST=$ROOT/build/icu-host
ICU_PS5=$ROOT/build/icu-ps5
JSC_BUILD=$ROOT/build/jsc

if [[ ! -d $SDK ]]; then
    echo "PS5 payload SDK not found at $SDK" >&2
    exit 1
fi

# The SCE clang finds prospero-lld through PATH.
export PATH="$SDK/bin:$PATH"

icu() {
    if [[ -f $ICU_PS5/lib/libicuuc.a && -f $ICU_PS5/lib/libicui18n.a && -f $ICU_PS5/lib/libicudata.a ]]; then
        echo "==> [icu] already built in $ICU_PS5"
        return
    fi

    if [[ ! -f $ICU_HOST/config/Makefile ]]; then
        echo "==> [icu] host build (provides the cross-build tools)"
        mkdir -p "$ICU_HOST"
        (cd "$ICU_HOST" && "$ICU_SRC/runConfigureICU" Linux \
            --disable-samples --disable-tests --disable-extras)
        make -C "$ICU_HOST" -j4
    fi

    echo "==> [icu] cross build for x86_64-sie-ps5"
    mkdir -p "$ICU_PS5"
    (cd "$ICU_PS5" && "$ICU_SRC/configure" \
        --host=x86_64-unknown-freebsd \
        --with-cross-build="$ICU_HOST" \
        --disable-samples --disable-tests --disable-extras --disable-tools \
        --prefix="$ICU_PS5" \
        CC=clang-18 CXX=clang++-18 \
        CFLAGS="--target=x86_64-sie-ps5 --sysroot=$SDK -isystem $SDK/target/include -fno-stack-protector -fno-plt -femulated-tls" \
        CXXFLAGS="--target=x86_64-sie-ps5 --sysroot=$SDK -isystem $SDK/target/include/c++/v1 -isystem $SDK/target/include -fno-stack-protector -fno-plt -femulated-tls -fno-char8_t -frtti" \
        LDFLAGS="-L$SDK/target/lib")
    make -C "$ICU_PS5" -j4
    make -C "$ICU_PS5" install
}

configure_jsc() {
    if [[ -f $JSC_BUILD/build.ninja ]]; then
        echo "==> [jsc] build tree already configured"
        return
    fi

    echo "==> [jsc] configuring JSCOnly"
    cmake -G Ninja \
        -S "$ROOT/third_party/webkit" \
        -B "$JSC_BUILD" \
        -DPORT=JSCOnly \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/ps5-toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DICU_ROOT="$ICU_PS5" \
        -DENABLE_JIT=OFF \
        -DENABLE_SAMPLES=OFF \
        -DENABLE_TOOLS=OFF \
        -DENABLE_STATIC_JSC=ON \
        -DUSE_SYSTEM_MALLOC=OFF
}

build_jsc() {
    echo "==> [jsc] ninja -j1 (resumable; rerun this script after a failure)"
    ninja -C "$JSC_BUILD" -j1
    echo "==> [jsc] done: $JSC_BUILD/lib/libJavaScriptCore.a"
}

icu
configure_jsc
[[ ${1:-} == --configure-only ]] && exit 0
build_jsc
