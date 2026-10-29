#!/bin/sh
# Builds iron_lz4.c without the GPU headers that iron_system.h pulls in.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../sources
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$src/iron_lz4.c" "$tmp/iron_lz4.c"
echo 'void iron_log(const char *format, ...);' > "$tmp/iron_system.h"
${CC:-gcc} -w -g -fsanitize=address,undefined -I"$tmp" -I"$src" \
	"$here/test.c" "$tmp/iron_lz4.c" "$src/iron_array.c" "$src/iron_string.c" -lm -o "$tmp/test"
ASAN_OPTIONS=detect_leaks=0 "$tmp/test"
