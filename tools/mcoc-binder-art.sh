#!/bin/bash
# Marvel Contest of Champions: the card binder's art (dump/binder_art in the
# game's directory, PNGs named by card code) from scans of the Series 2
# cards' fronts, MCoC_Fronts_Page_001.png .. _100.png. The fronts follow the
# order of the Series 2 entries of src/loader/rawthrills/mcoc/mcocCards.inc.
#
# usage: tools/mcoc-binder-art.sh <folder of the fronts> <game directory>
# needs ImageMagick (magick)
set -e
src=$1
game=$2
if [ ! -d "$src" ] || [ ! -d "$game" ]; then
    echo "usage: $0 <folder of the fronts> <game directory>" >&2
    exit 2
fi
inc=$(dirname "$0")/../src/loader/rawthrills/mcoc/mcocCards.inc
out=$game/dump/binder_art
mkdir -p "$out"
n=0
grep -o '{"[0-9a-z]\{8\}", "[^"]*", "[^"]*", "[^"]*", 2}' "$inc" | cut -d'"' -f2 | while read -r code; do
    n=$((n + 1))
    page=$(printf '%s/MCoC_Fronts_Page_%03d.png' "$src" "$n")
    if [ -f "$page" ]; then
        magick "$page" -resize '373x526!' -strip "$out/$code.png"
    else
        echo "no $page" >&2
    fi
done
echo "art in $out: $(ls "$out" | wc -l) cards"
