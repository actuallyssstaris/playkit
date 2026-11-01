#!/usr/bin/env bash
# Fetch the JavaScriptCore build inputs that are not in this repository.
#
# third_party/webkit and third_party/icu are huge and stay untracked. Both are
# pinned here so a clean CI runner (or a new machine) can rebuild them, and the
# PlayStation fixes we carry for WebKit are applied from a patch file.
#
# Run from anywhere; it only fills in directories that are missing.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WEBKIT_REMOTE=${WEBKIT_REMOTE:-https://github.com/WebKit/WebKit.git}
WEBKIT_REF=${WEBKIT_REF:-wpewebkit-2.48.0}
ICU_VERSION=${ICU_VERSION:-76.1}

if [[ ! -d $ROOT/third_party/webkit/Source/JavaScriptCore ]]; then
    echo "==> [sources] cloning WebKit $WEBKIT_REF"
    rm -rf "$ROOT/third_party/webkit"
    git clone --depth 1 --branch "$WEBKIT_REF" "$WEBKIT_REMOTE" \
        "$ROOT/third_party/webkit"
    echo "==> [sources] applying PlayStation fixes"
    git -C "$ROOT/third_party/webkit" apply \
        "$ROOT/tools/patches/webkit-2.48-playstation.patch"
fi

if [[ ! -f $ROOT/third_party/icu/source/configure ]]; then
    echo "==> [sources] fetching ICU $ICU_VERSION"
    tarball="icu4c-${ICU_VERSION//./_}-src.tgz"
    tag="release-${ICU_VERSION//./-}"
    tmp=$(mktemp -d)
    curl -fsSL \
        "https://github.com/unicode-org/icu/releases/download/$tag/$tarball" \
        -o "$tmp/icu.tgz"
    mkdir -p "$ROOT/third_party"
    tar -xzf "$tmp/icu.tgz" -C "$ROOT/third_party"
    rm -rf "$tmp"
fi

# Graphics dependencies. Pinned versions match the WebKit 2.48 requirements.
fetch_tarball() {
    local directory=$1 url=$2
    [[ -d $ROOT/third_party/$directory ]] && return
    echo "==> [sources] fetching $directory"
    local tmp
    tmp=$(mktemp -d)
    curl -fsSL "$url" -o "$tmp/archive"
    mkdir -p "$ROOT/third_party"
    tar -xf "$tmp/archive" -C "$ROOT/third_party"
    rm -rf "$tmp"
}

fetch_tarball pixman-0.42.2 \
    https://cairographics.org/releases/pixman-0.42.2.tar.gz
fetch_tarball cairo-1.16.0 \
    https://cairographics.org/releases/cairo-1.16.0.tar.xz
fetch_tarball libwpe-1.16.2 \
    https://github.com/WebPlatformForEmbedded/libwpe/releases/download/1.16.2/libwpe-1.16.2.tar.xz
