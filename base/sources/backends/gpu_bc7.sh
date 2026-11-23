#!/bin/sh
# Regenerates vulkan_bc7.h and metal_bc7.h from gpu_bc7.shader
set -e
cd "$(dirname "$0")"

case "$(uname -s)-$(uname -m)" in
Darwin-*) AMAKE=../../tools/bin/macos/amake ;;
Linux-aarch64) AMAKE=../../tools/bin/linux_arm64/amake ;;
Linux-*) AMAKE=../../tools/bin/linux_x64/amake ;;
*) AMAKE=../../tools/bin/windows_x64/amake.exe ;;
esac

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

$AMAKE --ashader spirv gpu_bc7.shader "$TMP/bc7.spv"
$AMAKE --ashader metal gpu_bc7.shader "$TMP/bc7.metal"

words() {
	od -An -v -tx4 "$1" | sed 's/ \([0-9a-f]*\)/0x\1,/g'
}

header() {
	echo '// Generated from gpu_bc7.shader by gpu_bc7.sh, do not edit'
	echo '#pragma once'
	echo
}

{
	header
	echo 'static const uint32_t vulkan_bc7_spirv[] = {'
	words "$TMP/bc7.spv"
	echo '};'
} > vulkan_bc7.h

{
	header
	echo 'static const char *metal_bc7_source ='
	sed 's/\\/\\\\/g; s/"/\\"/g; s/^/\t"/; s/$/\\n"/' "$TMP/bc7.metal"
	echo ';'
} > metal_bc7.h
