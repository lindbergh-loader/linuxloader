#!/bin/sh
# Fetch Unity's 64-bit Linux player of a version, for linuxloader64's Unity
# games: the linux64_withgfx_nondevelopment_mono variation of Unity's
# "Linux Build Support" installer, put in <dest>/<version> (LinuxPlayer and
# its Data/), where linuxloader64 looks for it with dest the game's unity/
# (the player goes with the game).
#
#   unity-player.sh <version> <changeset> [dest]      download, then unpack
#   unity-player.sh <version> <installer.exe> [dest]  unpack a downloaded one
#
# The changeset is in the version's download links (Unity's release archive);
# Nerf Arcade's: unity-player.sh 2018.2.21f1 a122f5dc316d "Nerf Arcade/unity"
# Needs curl and 7z.
set -e

[ $# -ge 2 ] || { sed -n '2,13p' "$0" | cut -c3-; exit 1; }
version=$1
source=$2
dest=${3:-unity}
variation=linux64_withgfx_nondevelopment_mono

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ -f "$source" ]; then
    installer=$source
else
    installer=$work/linux-support.exe
    url=https://download.unity3d.com/download_unity/$source/TargetSupportInstaller/UnitySetup-Linux-Support-for-Editor-$version.exe
    echo "Downloading $url"
    curl -fL -o "$installer" "$url"
fi

7z x -y -o"$work/x" "$installer" >/dev/null
found=$(find "$work/x" -type d -path "*/Variations/$variation" | head -1)
[ -n "$found" ] || { echo "No $variation in $installer" >&2; exit 1; }

mkdir -p "$dest"
rm -rf "${dest:?}/$version"
cp -r "$found" "$dest/$version"
# Debug symbols: not needed to run.
find "$dest/$version" -name '*.mdb' -delete
chmod +x "$dest/$version/LinuxPlayer"
echo "Unity $version player in $dest/$version"
