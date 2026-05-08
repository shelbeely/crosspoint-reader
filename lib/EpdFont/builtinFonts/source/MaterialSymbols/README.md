# Material Symbols Rounded (subset)

This directory contains a static, subsetted instance of Google's Material
Symbols Rounded variable font, used by the Material 3 theme as the source
for UI icons.

## Source

- Upstream: <https://github.com/google/material-design-icons>
- Variable font: `variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`
- License: Apache License, Version 2.0 (see `LICENSE.txt`).

## Reproducing the static instance

The `MaterialSymbolsRounded-Regular.ttf` checked in here was produced by:

1. Instancing the upstream variable font with `fontTools.varLib.instancer` at
   axes `wght=400, FILL=0, GRAD=0, opsz=24` (matches the default Material 3
   icon style).
2. Subsetting with `fontTools.subset` to **only** the codepoints listed in
   `MATERIAL_SYMBOLS_CODEPOINTS` in
   `lib/EpdFont/scripts/convert-builtin-fonts.sh`.
3. Dropping `GSUB` / `GPOS` / `GDEF` / `DSIG` tables (we do not use OpenType
   ligatures or kerning for the icon font).

The resulting TTF is a few KB and contains no Latin glyphs, so the
`fontconvert.py` Latin/Cyrillic intervals naturally drop out — only the
requested PUA codepoints are emitted into the generated header.

## Generated headers

- `lib/EpdFont/builtinFonts/material_symbols_rounded_24.h` (24 px, primary)
- `lib/EpdFont/builtinFonts/material_symbols_rounded_20.h` (20 px, status bar)

Regenerate with `lib/EpdFont/scripts/convert-builtin-fonts.sh`.

## Adding a new icon

1. Look up the canonical name and codepoint in Google's `*.codepoints` file
   (in the same upstream `variablefont/` directory).
2. Add the codepoint to `MATERIAL_SYMBOLS_CODEPOINTS` in
   `convert-builtin-fonts.sh`.
3. Re-subset the source TTF (the `fontTools.subset` invocation above) so
   the new glyph is present in the source.
4. Add a named constant + table entry in `src/components/MaterialIcons.h`.
5. Re-run `convert-builtin-fonts.sh` and `build-font-ids.sh`.
