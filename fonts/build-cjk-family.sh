#!/usr/bin/env bash
# Build a .cpfont set pairing a Latin face with a Traditional Chinese fallback
# over the top-5000 CJK subset.
#
#   ./build-cjk-family.sh Inter  huninn   -> output/InterHuninn/
#   ./build-cjk-family.sh Bitter huninn   -> output/BitterHuninn/
#   ./build-cjk-family.sh Ember  ibm      -> output/EmberPlex/
#
# Env: OUT= GAP=64 SIZES=12,14,16,18 LATIN_SUBSET=reading FAMILY= PY=python3
#
# Licensing: Bitter, Inter, jf-openhuninn, IBM Plex and Noto are all SIL OFL 1.1
# and redistributable. AMAZON EMBER IS NOT -- it is proprietary Amazon type
# (vendor DAMA). Any family built with Ember is for local use only: do not
# commit the output or publish it through release-fonts.yml.
#
# Things this handles that are easy to get wrong:
#
#  1. Bitter and Inter ship as VARIABLE fonts. Bitter's wght default is 100
#     (Thin) -- rasterizing it directly gives hairlines. Inter defaults to
#     wght 400 but opsz 14..32, so its optical size needs pinning.
#     prepare_faces.py instances real statics; see its VARIABLE_FACES comment.
#     Ember already ships as statics.
#
#  2. Every CJK face here has coverage holes, and the converter takes only ONE
#     fallback per style, so prepare_faces.py pre-merges a NotoSansTC subset
#     covering exactly those holes. It also points U+FFFD at U+25A1, which none
#     of these CJK faces has -- without it every unrenderable character would
#     silently vanish instead of showing a box.
#
#  3. RAM. Two levers, both applied:
#       --sparse-intervals collapses the resident interval table from thousands
#       of entries to ~20 (undrawable codepoints become zero-size glyphs: 16B of
#       on-SD metadata, nothing resident).
#       --latin-subset reading trims the PRIMARY cmap, which shrinks the kern
#       class tables -- also resident, and the dominant cost once intervals are
#       gone. For Inter that was 9.91 -> 3.65 KB/style.
#     Note the CJK font barely affects resident RAM in sparse mode; its cmap
#     density only matters in strict mode. Pick it for looks and for whether it
#     has a real Bold, not for memory.
set -euo pipefail

LATIN="${1:-Inter}"
CJKSET="${2:-huninn}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
PY="${PY:-python3}"                 # needs fonttools + freetype-py
OUT="${OUT:-$HERE/output}"
GAP="${GAP:-64}"
SIZES="${SIZES:-12,14,16,18}"
LATIN_SUBSET="${LATIN_SUBSET:-reading}"

CONVERT="$REPO/lib/EpdFont/scripts/fontconvert_sdcard.py"

# Latin: Ember ships as statics under its own names; the rest are instanced.
case "$LATIN" in
  Ember)  REG="AmazonEmber_Rg.ttf"; BLD="AmazonEmber_Bd.ttf"; LTAG="Ember" ;;
  *)      REG="$LATIN-Regular.static.ttf"; BLD="$LATIN-Bold.static.ttf"; LTAG="$LATIN" ;;
esac

# CJK: must match prepare_faces.py CJK_FAMILIES. huninn is single-weight so both
# styles share one composite; IBM Plex has real weights so they differ.
case "$CJKSET" in
  huninn) CREG="jf-openhuninn-2.1+rescue.ttf"; CBLD="$CREG"; CTAG="Huninn" ;;
  ibm)    CREG="IBMPlexSansTC-Regular+rescue.ttf"; CBLD="IBMPlexSansTC-Bold+rescue.ttf"; CTAG="Plex" ;;
  *)      echo "unknown CJK set '$CJKSET' (want: huninn, ibm)" >&2; exit 1 ;;
esac

FAMILY="${FAMILY:-${LTAG}${CTAG}}"

# NOTE: bypasses build-sd-fonts.py on purpose -- that script hardcodes the
# fallback to NotoSans-Regular (build-sd-fonts.py:218,225) with no YAML
# override, so a CJK fallback has to be wired up by calling the converter
# directly.

echo "=== Preparing faces ==="
"$PY" "$HERE/prepare_faces.py" "$CJKSET"
for f in "$REG" "$BLD" "$CREG" "$CBLD"; do
  [ -f "$HERE/$f" ] || { echo "missing $f -- check prepare_faces.py" >&2; exit 1; }
done

echo "=== Intervals ==="
"$PY" "$HERE/gen_intervals.py" \
  --primary "$REG,$BLD" --fallback "$CREG,$CBLD" \
  --mode sparse --gap "$GAP" --latin-subset "$LATIN_SUBSET" --styles 2 \
  --out "$HERE/.intervals-$FAMILY.txt"
IV="$(cat "$HERE/.intervals-$FAMILY.txt")"

echo "=== $FAMILY ==="
mkdir -p "$OUT/$FAMILY"
"$PY" "$CONVERT" \
  --regular "$HERE/$REG" --bold "$HERE/$BLD" \
  --fallback-regular "$HERE/$CREG" --fallback-bold "$HERE/$CBLD" \
  --intervals "$IV" \
  --sparse-intervals \
  --sizes "$SIZES" \
  --name "$FAMILY" \
  --output-dir "$OUT/$FAMILY/"

echo
echo "Built into $OUT/$FAMILY/"
echo "Copy it to your SD card as /.fonts/$FAMILY/"
"$PY" "$HERE/validate_cpfont.py" "$OUT/$FAMILY"
