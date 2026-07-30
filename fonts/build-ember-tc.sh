#!/usr/bin/env bash
# Build .cpfont sets pairing Amazon Ember (Latin) with Noto Sans TC (CJK
# fallback) over the top-5000 Traditional Chinese subset.
#
# Builds two families:
#   AmazonEmberTC          regular->TC-Regular, bold->TC-Bold  (weight-matched)
#   AmazonEmberTCBoldCJK   regular->TC-Bold,    bold->TC-Bold  (all-bold CJK)
#
# Both carry identical interval tables and glyph sets, so they differ only in
# CJK stroke weight -- which makes them directly comparable on device.
#
# NOTE: this bypasses build-sd-fonts.py on purpose. That script hardcodes the
# fallback font to NotoSans-Regular (build-sd-fonts.py:218,225) with no YAML
# override, so a CJK fallback has to be wired up by calling the converter
# directly.
#
# Amazon Ember is proprietary Amazon type -- these outputs are for local use
# only. Do not commit them or publish them through release-fonts.yml.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
PY="${PY:-python3}"                 # needs fonttools + freetype-py
OUT="${1:-$HERE/output}"
GAP="${GAP:-8}"
SIZES="${SIZES:-12,14,16,18}"

CONVERT="$REPO/lib/EpdFont/scripts/fontconvert_sdcard.py"

# Strict gap-merge: only merge across gaps both fonts can draw, or the
# converter's validation pass re-splits them (fontconvert_sdcard.py:605).
# gap=8 hits the floor of ~2100 intervals; larger gaps add glyphs without
# reducing resident RAM further.
#
# For a much smaller interval table see build-bitter-huninn.sh, which uses
# --mode sparse + the converter's --sparse-intervals to reach 24 intervals
# (0.14 KB/style) instead of 2103 (12.32 KB/style). That mode is not applied
# here so this family stays byte-comparable with what is already on the SD card.
"$PY" "$HERE/gen_intervals.py" \
  --primary AmazonEmber_Rg.ttf,AmazonEmber_Bd.ttf \
  --fallback NotoSansTC-Regular.ttf,NotoSansTC-Bold.ttf \
  --mode strict --gap "$GAP" --styles 2 \
  --out "$HERE/.intervals.txt"
IV="$(cat "$HERE/.intervals.txt")"

build() {
  local family="$1" fb_regular="$2" fb_bold="$3"
  echo "=== $family (regular<-$(basename "$fb_regular"), bold<-$(basename "$fb_bold")) ==="
  mkdir -p "$OUT/$family"
  "$PY" "$CONVERT" \
    --regular "$HERE/AmazonEmber_Rg.ttf" --fallback-regular "$HERE/$fb_regular" \
    --bold    "$HERE/AmazonEmber_Bd.ttf" --fallback-bold    "$HERE/$fb_bold" \
    --intervals "$IV" \
    --sizes "$SIZES" \
    --name "$family" \
    --output-dir "$OUT/$family/"
}

build AmazonEmberTC        NotoSansTC-Regular.ttf NotoSansTC-Bold.ttf
build AmazonEmberTCBoldCJK NotoSansTC-Bold.ttf    NotoSansTC-Bold.ttf

echo
echo "Built into $OUT/"
echo "Copy a family directory to your SD card as /.fonts/<FamilyName>/"
"$PY" "$HERE/validate_cpfont.py" "$OUT/AmazonEmberTC"
