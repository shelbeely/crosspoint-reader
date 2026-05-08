#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)
OPENDYSLEXIC_FONT_SIZES=(8 10 12 14)

for size in ${NOTOSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

for size in ${OPENDYSLEXIC_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="opendyslexic_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/OpenDyslexic/OpenDyslexic-${style}.otf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path > $output_path
    echo "Generated $output_path"
  done
done

python fontconvert.py notosans_8_regular 8 ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf > ../builtinFonts/notosans_8_regular.h

# Material Symbols Rounded — subsetted icon font for the Material 3 theme.
# Source TTF in lib/EpdFont/builtinFonts/source/MaterialSymbols/ has already
# been instanced from the variable font (Weight=400, Fill=0, Grade=0,
# Optical Size=24) and subsetted to the codepoints below.  Keep this list in
# sync with src/components/MaterialIcons.h (kMaterialIconCodepoints).
MATERIAL_SYMBOLS_FONT="../builtinFonts/source/MaterialSymbols/MaterialSymbolsRounded-Regular.ttf"
MATERIAL_SYMBOLS_SIZES=(20 24)
# Codepoints (Material Symbols PUA): folder, description, image, menu_book,
# draft, history, settings, swap_vert, library_books, wifi, wifi_tethering,
# arrow_back, check, chevron_left, chevron_right, close, sync, battery_full,
# keyboard_backspace, keyboard_capslock, keyboard_return.
# Keep this list in sync with kIconCodepoints in src/components/MaterialIcons.h.
MATERIAL_SYMBOLS_CODEPOINTS=(
  0xe02f 0xe1a5 0xe1e2 0xe2c7 0xe317 0xe318 0xe31b 0xe3f4 0xe5c4 0xe5ca
  0xe5cb 0xe5cc 0xe5cd 0xe627 0xe63e 0xe66d 0xe873 0xe8b3 0xe8b8 0xe8d5 0xea19
)
ms_intervals=()
for cp in ${MATERIAL_SYMBOLS_CODEPOINTS[@]}; do
  ms_intervals+=(--additional-intervals "${cp},${cp}")
done
for size in ${MATERIAL_SYMBOLS_SIZES[@]}; do
  font_name="material_symbols_rounded_${size}"
  output_path="../builtinFonts/${font_name}.h"
  python fontconvert.py $font_name $size $MATERIAL_SYMBOLS_FONT --2bit --compress "${ms_intervals[@]}" > $output_path
  echo "Generated $output_path"
done

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
