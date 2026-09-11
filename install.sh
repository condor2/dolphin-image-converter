#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SYSTEM_BIN="/usr/local/bin/dolphin-image-converter"
SERVICE_DIR="$HOME/.local/share/kio/servicemenus"
SERVICE_FILE="$SERVICE_DIR/dolphin-image-converter.desktop"
LEGACY_BIN="$HOME/.local/bin/dolphin-image-converter"

if [[ ${EUID} -eq 0 ]]; then
    echo "Run this script as your normal desktop user, not with sudo." >&2
    exit 1
fi
if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo is required to install the executable into /usr/local/bin." >&2
    exit 1
fi

missing=0
for cmd in cmake c++; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing required command: $cmd" >&2
        missing=1
    fi
done

qt6_widgets_found=0
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists 'Qt6Widgets >= 6.5'; then
    qt6_widgets_found=1
elif command -v qtpaths6 >/dev/null 2>&1; then
    qt_libdir="$(qtpaths6 --query QT_INSTALL_LIBS 2>/dev/null || true)"
    if [[ -n "$qt_libdir" && -f "$qt_libdir/cmake/Qt6/Qt6Config.cmake" ]]; then
        qt6_widgets_found=1
    fi
fi
if [[ $qt6_widgets_found -eq 0 ]]; then
    echo "Qt 6.5+ development files (Qt6::Widgets) were not found." >&2
    missing=1
fi

LRELEASE_BIN=""
for candidate in lrelease6 lrelease-qt6 lrelease; do
    if command -v "$candidate" >/dev/null 2>&1; then
        LRELEASE_BIN="$(command -v "$candidate")"
        break
    fi
done
if [[ -z "$LRELEASE_BIN" ]] && command -v qtpaths6 >/dev/null 2>&1; then
    qt_bindir="$(qtpaths6 --query QT_INSTALL_BINS 2>/dev/null || true)"
    if [[ -n "$qt_bindir" && -x "$qt_bindir/lrelease" ]]; then
        LRELEASE_BIN="$qt_bindir/lrelease"
    fi
fi
if [[ -z "$LRELEASE_BIN" ]]; then
    echo "Qt LinguistTools was not found: lrelease6/lrelease-qt6/lrelease is unavailable." >&2
    missing=1
else
    echo "Detected Qt lrelease: $LRELEASE_BIN"
fi

if [[ $missing -ne 0 ]]; then
    if command -v pacman >/dev/null 2>&1; then
        echo >&2
        echo "On Arch Linux install the required build packages with:" >&2
        echo "  sudo pacman -S --needed base-devel cmake qt6-base qt6-tools" >&2
    fi
    exit 1
fi

if ! command -v magick >/dev/null 2>&1; then
    echo "ImageMagick 7 was not found: required executable 'magick' is missing from PATH." >&2
    if command -v pacman >/dev/null 2>&1; then
        echo "Install it with: sudo pacman -S --needed imagemagick" >&2
    fi
    exit 1
fi

MAGICK_VERSION="$(magick -version 2>/dev/null | head -n1 || true)"
if [[ -z "$MAGICK_VERSION" || "$MAGICK_VERSION" != *"ImageMagick 7"* ]]; then
    echo "The 'magick' command exists, but ImageMagick 7 could not be verified." >&2
    [[ -n "$MAGICK_VERSION" ]] && echo "Detected: $MAGICK_VERSION" >&2
    exit 1
fi
echo "Detected: $MAGICK_VERSION"

MAGICK_TEST_DIR="$(mktemp -d)"
BIN_BACKUP=""
SERVICE_BACKUP=""
cleanup() {
    rm -rf "${MAGICK_TEST_DIR:-}"
    [[ -n "${BIN_BACKUP:-}" ]] && rm -f "$BIN_BACKUP"
    [[ -n "${SERVICE_BACKUP:-}" ]] && rm -f "$SERVICE_BACKUP"
}
trap cleanup EXIT

for format in png jpg; do
    if ! magick -size 1x1 xc:white "$MAGICK_TEST_DIR/test.$format" >/dev/null 2>&1; then
        echo "ImageMagick cannot write $format files. Check delegates/security policy (policy.xml)." >&2
        exit 1
    fi
done
for format in webp avif heic; do
    if ! magick -size 1x1 xc:white "$MAGICK_TEST_DIR/test.$format" >/dev/null 2>&1; then
        echo "Warning: ImageMagick cannot currently write $format files (delegate or policy restriction)." >&2
    fi
done

if ! command -v dolphin >/dev/null 2>&1; then
    echo "Warning: Dolphin was not found in PATH. The binary will install, but the context menu requires Dolphin." >&2
fi

JOBS=1
command -v nproc >/dev/null 2>&1 && JOBS="$(nproc)"

echo "Building Dolphin Image Converter..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build "$BUILD_DIR" -j"$JOBS"

if [[ ! -x "$BUILD_DIR/dolphin-image-converter" ]]; then
    echo "Build finished, but the executable was not found." >&2
    exit 1
fi
if [[ ! -f "$BUILD_DIR/dolphin-image-converter.desktop" ]]; then
    echo "Build finished, but the generated Dolphin Service Menu was not found." >&2
    exit 1
fi

if [[ -e "$SYSTEM_BIN" ]]; then
    BIN_BACKUP="$(mktemp)"
    cp -p "$SYSTEM_BIN" "$BIN_BACKUP"
fi
if [[ -e "$SERVICE_FILE" ]]; then
    SERVICE_BACKUP="$(mktemp)"
    cp -p "$SERVICE_FILE" "$SERVICE_BACKUP"
fi

rollback() {
    echo "Rolling back installation..." >&2
    if [[ -n "$BIN_BACKUP" && -f "$BIN_BACKUP" ]]; then
        sudo install -Dm755 "$BIN_BACKUP" "$SYSTEM_BIN" || true
    else
        sudo rm -f "$SYSTEM_BIN" || true
    fi
    if [[ -n "$SERVICE_BACKUP" && -f "$SERVICE_BACKUP" ]]; then
        install -Dm755 "$SERVICE_BACKUP" "$SERVICE_FILE" || true
    else
        rm -f "$SERVICE_FILE" || true
    fi
}

echo "Installing executable to $SYSTEM_BIN..."
if ! sudo install -Dm755 "$BUILD_DIR/dolphin-image-converter" "$SYSTEM_BIN"; then
    rollback
    exit 1
fi

echo "Installing Dolphin Service Menu to $SERVICE_FILE..."
if ! install -Dm755 "$BUILD_DIR/dolphin-image-converter.desktop" "$SERVICE_FILE"; then
    rollback
    exit 1
fi

# One final sanity check of the installed integration file.
if ! grep -Fqx 'X-KDE-Protocols=file' "$SERVICE_FILE" \
   || ! grep -Fq 'Exec=/usr/local/bin/dolphin-image-converter --resize %F' "$SERVICE_FILE"; then
    echo "Installed Service Menu failed its final sanity check." >&2
    rollback
    exit 1
fi

rm -f "$LEGACY_BIN"
BIN_BACKUP=""
SERVICE_BACKUP=""

if command -v kbuildsycoca6 >/dev/null 2>&1; then
    kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
fi

echo
echo "Installed successfully:"
echo "  $SYSTEM_BIN"
echo "  $SERVICE_FILE"
echo
echo "Test with:"
echo "  dolphin-image-converter --help"
echo
echo "Restart Dolphin to reload the context menu:"
echo "  kquitapp6 dolphin 2>/dev/null || true"
echo "  dolphin &"
