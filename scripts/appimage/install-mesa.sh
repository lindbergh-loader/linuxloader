#!/usr/bin/env bash
# Install the pinned 32-bit mesa/vulkan stack that the AppImage bundles.
#
# Replaces install-kisak.sh, which added ppa:kisak/turtle and ran a full
# `apt upgrade` - 107 MB of downloads and several minutes - to obtain these
# packages. The upstream versions are frozen, so they are vendored in
# libs/assets and unpacked straight over the paths make-appimage.sh reads.
#
# The pinned package set, why each is needed, and how to regenerate the
# tarball are documented in libs/assets/mesa-kisak-pinned.txt.
set -euo pipefail

TARBALL="libs/assets/mesa-kisak-pinned.tar"
EXPECTED_ARCH="i386"

if [[ ! -f "$TARBALL" ]]; then
    echo "Error: $TARBALL not found. See libs/assets/mesa-kisak-pinned.txt." >&2
    exit 1
fi

# Works both on a GitHub runner (non-root, sudo present) and inside a
# container running as root, where sudo may not be installed at all.
SUDO=""
if [[ $EUID -ne 0 ]]; then
    SUDO="sudo"
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

tar -xf "$TARBALL" -C "$STAGE"

shopt -s nullglob
DEBS=("$STAGE"/*.deb)
if (( ${#DEBS[@]} == 0 )); then
    echo "Error: $TARBALL contains no .deb packages." >&2
    exit 1
fi

# The AppImage is an entirely 32-bit stack: make-appimage.sh reads only from
# /usr/lib/i386-linux-gnu. amd64 packages unpack to the x86_64 triplet, which
# would build a clean-looking AppImage with no working DRI or Vulkan drivers -
# a failure that surfaces only when a game refuses to launch. Fail here.
# 'all' is permitted for libdrm-common, which ships /usr/share/libdrm/*.ids.
for deb in "${DEBS[@]}"; do
    deb_arch=$(dpkg-deb -f "$deb" Architecture)
    if [[ "$deb_arch" != "$EXPECTED_ARCH" && "$deb_arch" != "all" ]]; then
        echo "Error: $(basename "$deb") is '$deb_arch', expected '$EXPECTED_ARCH' or 'all'." >&2
        echo "See libs/assets/mesa-kisak-pinned.txt to regenerate it." >&2
        exit 1
    fi
done

# mesa 25 keeps the gallium driver in its own package; libgl1-mesa-dri is only
# the libdril_dri.so loader stub. Without gallium the AppImage builds fine and
# renders nothing, so check explicitly rather than discovering it downstream.
if ! printf '%s\n' "${DEBS[@]}" | grep -q 'mesa-libgallium'; then
    echo "Error: $TARBALL has no mesa-libgallium package." >&2
    echo "libgl1-mesa-dri alone is only the DRI loader stub." >&2
    echo "See libs/assets/mesa-kisak-pinned.txt." >&2
    exit 1
fi

mkdir -p "$STAGE/ext"
for deb in "${DEBS[@]}"; do
    echo "Unpacking $(basename "$deb")"
    dpkg-deb -x "$deb" "$STAGE/ext"
done

if [[ ! -d "$STAGE/ext/usr/lib/i386-linux-gnu" ]]; then
    echo "Error: unpacked tree has no usr/lib/i386-linux-gnu directory." >&2
    exit 1
fi

# Copy the whole unpacked usr/ tree rather than cherry-picking directories:
# it covers lib/i386-linux-gnu, share/vulkan/icd.d, share/libdrm, share/drirc.d
# and the GL headers in one step. Safe because the guard above admits only
# i386 and arch-independent packages, so nothing amd64 is overwritten.
$SUDO cp -a "$STAGE/ext/usr/." /usr/

# Stock mesa driver files installed earlier by install-packages.sh remain in
# /usr/lib/i386-linux-gnu/dri - `cp -a` overlays, it does not prune. Harmless:
# make-appimage.sh copies only libdril_dri.so and symlinks the rest to it.

$SUDO ldconfig

echo "Pinned mesa stack installed."
