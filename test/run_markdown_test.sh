#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/markdown"
BINARY="$BUILD_DIR/MarkdownParserTest"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/markdown/MarkdownParserTest.cpp"
  "$ROOT_DIR/lib/Markdown/MarkdownParser.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -I"$ROOT_DIR"
  -I"$ROOT_DIR/lib"
  -I"$ROOT_DIR/lib/Markdown"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
