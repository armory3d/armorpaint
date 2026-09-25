#!/usr/bin/env sh
set -eu

REPO="dionarley/armorpaint"
APP="ArmorPaint"
PKG_BASE="ArmorPaint-linux"

say()  { printf '%s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }
uname_s() { uname -s; }
uname_m() { uname -m; }

if [ "$(uname_s)" != "Linux" ]; then
	die "unsupported OS: $(uname_s) — this installer targets Linux (Vulkan)"
fi

ARCH="$(uname_m)"
case "$ARCH" in
	x86_64) ;;
	aarch64) die "no prebuilt release for aarch64 yet — build from source (see docs/build_linux.md)" ;;
	*) die "unsupported architecture: $ARCH" ;;
esac

VERSION="${ARMORPAINT_VERSION:-latest}"
DEST="${ARMORPAINT_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/armorpaint}"
BIN_DIR="${ARMORPAINT_BIN_DIR:-${XDG_BIN_HOME:-$HOME/.local/bin}}"
APPS_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICON_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
LOCAL=""
UNINSTALL=""

while [ "$#" -gt 0 ]; do
	case "$1" in
		--local)
			[ "$#" -ge 2 ] || die "--local requires a path"
			LOCAL="$2"
			shift 2 ;;
		--uninstall) UNINSTALL=1; shift ;;
		--help|-h)
			say "Usage: sh install.sh [--local PATH] [--uninstall]"
			say "  (default)      download the latest release from $REPO and install"
			say "  --local PATH   install from a local .tar.xz package instead of downloading"
			say "  --uninstall    remove ArmorPaint (files, wrapper, icon, .desktop entry)"
			exit 0 ;;
		*) die "unknown argument: $1" ;;
	esac
done

CONF_DIR="${XDG_CONFIG_HOME:-$HOME/.config}"
CONF_FILE="$CONF_DIR/$APP/install-meta.txt"

if [ -n "$UNINSTALL" ]; then
	if [ -f "$CONF_FILE" ]; then
		DEST_BIN="$(sed -n '1p' "$CONF_FILE")"
		DEST_DATA="$(sed -n '2p' "$CONF_FILE")"
		ICON_FILE="$(sed -n '3p' "$CONF_FILE")"
		DESKTOP_FILE="$(sed -n '4p' "$CONF_FILE")"
		rm -rf "$DEST_DATA"
		rm -f "$DEST_BIN" "$ICON_FILE" "$DESKTOP_FILE"
		rm -f "$CONF_FILE"
		say "Uninstalled ArmorPaint."
	else
		say "ArmorPaint is not installed."
	fi
	exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ -n "$LOCAL" ]; then
	[ -f "$LOCAL" ] || die "package not found: $LOCAL"
	PKG="$LOCAL"
	PKG_SHA=""
	say "Using local package: $LOCAL"
else
	if [ -x "$DEST/$APP" ]; then
		say "$APP is already installed at $DEST/$APP"
		say "Run the installer again to reinstall, or 'sh install.sh --uninstall' to remove it."
		exit 0
	fi
	PKG="$TMP/$PKG_BASE-$ARCH.tar.xz"
	PKG_SHA="$TMP/$PKG_BASE-$ARCH.tar.xz.sha256"
	BASE_URL="${ARMORPAINT_URL:-https://github.com/$REPO/releases/$VERSION/download}"
	URL="$BASE_URL/$PKG_BASE-$ARCH.tar.xz"
	SHA_URL="$BASE_URL/$(basename "$PKG_SHA")"
	say "Downloading $URL"
	if command -v curl >/dev/null 2>&1; then
		curl -fSL "$URL" -o "$PKG" || die "download failed — make sure a Release with the $ARCH asset exists (REPO=$REPO, VERSION=$VERSION)"
		curl -fSL "$SHA_URL" -o "$PKG_SHA" 2>/dev/null || PKG_SHA=""
	else
		wget -q --show-progress "$URL" -O "$PKG" || die "download failed — make sure a Release with the $ARCH asset exists"
		wget -q "$SHA_URL" -O "$PKG_SHA" 2>/dev/null || PKG_SHA=""
	fi
	if [ -n "$PKG_SHA" ] && [ -f "$PKG_SHA" ]; then
		(cd "$TMP" && sha256sum -c "$(basename "$PKG_SHA")" >/dev/null 2>&1) || die "checksum verification failed"
		say "Checksum OK."
	fi
fi

mkdir -p "$DEST" "$BIN_DIR" "$APPS_DIR" "$ICON_DIR"
tar -xJf "$PKG" -C "$TMP"
rm -rf "$DEST/data" "$DEST/$APP" "$DEST/icon.png"
cp "$TMP/ArmorPaint/$APP" "$DEST/"
cp -r "$TMP/ArmorPaint/data" "$DEST/"
cp "$TMP/ArmorPaint/icon.png" "$DEST/"
chmod +x "$DEST/$APP"

WRAPPER="$BIN_DIR/$APP"
cat > "$WRAPPER" <<EOF
#!/bin/sh
exec "$DEST/$APP" "\$@"
EOF
chmod +x "$WRAPPER"

cp "$DEST/icon.png" "$ICON_DIR/$APP.png"
OUTNAME="$APP.desktop"
cat > "$APPS_DIR/$OUTNAME" <<EOF
[Desktop Entry]
Type=Application
Name=ArmorPaint
GenericName=3D PBR Texture Painting
Comment=Paint directly on your 3D models with a layer-based, real-time workflow
Exec="$DEST/$APP" %F
Icon=$APP
Terminal=false
Categories=Graphics;3DGraphics;Art;
Keywords=texture;paint;3d;pbr;
StartupWMClass=$APP
EOF

mkdir -p "$(dirname "$CONF_FILE")"
printf '%s\n' "$WRAPPER" "$DEST" "$ICON_DIR/$APP.png" "$APPS_DIR/$OUTNAME" > "$CONF_FILE"

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -f -t "${XDG_DATA_HOME:-$HOME/.local/share}/icons" >/dev/null 2>&1 || true

say ""
say "Installed $APP to $DEST"
say "  launcher: $WRAPPER (add $BIN_DIR to your PATH)"
say "  app menu: $APPS_DIR/$OUTNAME (restart your session if it does not appear)"
say ""
say "Run it: $APP"
say "Remove it: sh install.sh --uninstall"