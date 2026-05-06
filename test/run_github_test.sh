#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/github"
BINARY="$BUILD_DIR/GitHubResponseParsersTest"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/github/GitHubResponseParsersTest.cpp"
  "$ROOT_DIR/lib/GitHubClient/GitHubResponseParsers.cpp"
  "$ROOT_DIR/lib/JsonParser/StreamingJsonParser.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -I"$ROOT_DIR"
  -I"$ROOT_DIR/lib"
  -I"$ROOT_DIR/lib/GitHubClient"
  -I"$ROOT_DIR/lib/JsonParser"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"

CLIENT_BINARY="$BUILD_DIR/GitHubClientTest"
CLIENT_SOURCES=(
  "$ROOT_DIR/test/github/GitHubClientTest.cpp"
  "$ROOT_DIR/lib/GitHubClient/GitHubClient.cpp"
  "$ROOT_DIR/lib/GitHubClient/GitHubResponseParsers.cpp"
  "$ROOT_DIR/lib/GitHubClient/GitHubRateLimiter.cpp"
  "$ROOT_DIR/lib/JsonParser/StreamingJsonParser.cpp"
)
c++ "${CXXFLAGS[@]}" "${CLIENT_SOURCES[@]}" -o "$CLIENT_BINARY"
"$CLIENT_BINARY" "$@"

