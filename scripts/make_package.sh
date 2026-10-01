#!/usr/bin/env bash
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${1:-$ROOT/paint/build/package}"
ARCH="$(uname -m)"
PKG_NAME="ArmorPaint-linux-${ARCH}.tar.xz"
SHA_NAME="${PKG_NAME}.sha256"

if [ -n "${2:-}" ] && [ "$2" = "--rebuild" ]; then
	echo "Rebuilding..."
	(cd "$ROOT/paint" && ../base/make --compile)
fi

BUILD_DIR="$ROOT/paint/build/out"
if [ ! -x "$BUILD_DIR/ArmorPaint" ]; then
	echo "error: $BUILD_DIR/ArmorPaint not found — run 'cd paint && ../base/make --compile' first" >&2
	exit 1
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/ArmorPaint"
cp -r "$BUILD_DIR/ArmorPaint" "$BUILD_DIR/data" "$ROOT/base/icon.png" "$STAGE/ArmorPaint/"

mkdir -p "$OUTDIR"
tar -C "$STAGE" -cJf "$OUTDIR/$PKG_NAME" ArmorPaint
(cd "$OUTDIR" && sha256sum "$PKG_NAME" > "$SHA_NAME")

echo "Package: $OUTDIR/$PKG_NAME"
echo "SHA256:  $OUTDIR/$SHA_NAME ($(sha256sum "$OUTDIR/$PKG_NAME" | cut -d' ' -f1))"
echo
echo "Upload as a GitHub Release asset (replace v0.1.2 with your tag):"
echo "  gh release create v0.1.2 $OUTDIR/$PKG_NAME "$OUTDIR/$SHA_NAME" --repo dionarley/armorpaint --title 'ArmorPaint v0.1.2' --notes 'Linux x86_64 binary (Vulkan)'"