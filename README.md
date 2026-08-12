# Dolphin Image Converter

A small Qt 6 application plus a KDE Dolphin Service Menu for batch image operations.

## Current features

- Resize by width with automatic per-image height, by height with automatic per-image width, or fit inside a width/height box. Aspect ratio is always preserved.
- Optional "do not enlarge" behavior.
- Rotate left/right by 90 degrees.
- Convert to WebP, AVIF, JPEG or PNG.
- Adjustable quality for lossy formats.
- Optional EXIF/metadata removal on conversion.
- Output suffix or explicit overwrite mode.
- Batch progress and per-file ImageMagick error reporting.
- Romanian and English Dolphin menu labels.

## Requirements

- KDE Plasma / Dolphin using the current `kio/servicemenus` mechanism.
- Qt 6 development files, version 6.5 or newer.
- CMake 3.20 or newer.
- A C++17 compiler.
- ImageMagick 7, with the `magick` executable in PATH.
- WebP/AVIF delegates in ImageMagick if those output formats are required.

Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake qt6-base imagemagick libwebp libheif
```

Debian/Ubuntu-family build dependencies (package names may vary by release):

```bash
sudo apt install build-essential cmake qt6-base-dev imagemagick desktop-file-utils
```

Check ImageMagick format support with:

```bash
magick -list format | grep -E 'AVIF|WEBP'
```

## Build and install for the current user

```bash
./install-user.sh
```

This installs to:

```text
~/.local/bin/dolphin-image-converter
~/.local/share/kio/servicemenus/dolphin-image-converter.desktop
```

The `.desktop` file is marked executable because KDE requires authorization for locally installed service menus. The user installer also writes the absolute path to `~/.local/bin/dolphin-image-converter` into the Service Menu, so Dolphin does not depend on the graphical session PATH.

Restart Dolphin after installation if the actions were already loaded.

## Manual build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run manually:

```bash
./build/dolphin-image-converter --resize image1.jpg image2.png
./build/dolphin-image-converter --convert image1.jpg
```

## Validate the Service Menu

```bash
desktop-file-validate data/dolphin-image-converter.desktop
```

## Design

Dolphin passes the selected URLs with `%U`. The Qt application converts `file://` URLs to local paths and starts ImageMagick directly with `QProcess`; it does not interpolate file names into a shell command.

The first version intentionally uses a Service Menu instead of `KAbstractFileItemActionPlugin`. This keeps the Dolphin integration small and decouples it from KDE Frameworks APIs while retaining a native Qt interface.

## License

GPL-2.0-or-later.
