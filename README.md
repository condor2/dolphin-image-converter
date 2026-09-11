# Dolphin Image Converter 0.2.1

Batch image tools for KDE Dolphin, powered by Qt 6 and ImageMagick.

## Screenshot
<img width="440" height="557" alt="Screenshot_20260911_084052" src="https://github.com/user-attachments/assets/9c97d26e-5af1-4ced-b222-ffed684c8d9a" />

## Features

- Resize by width, by height, or inside a width × height box while preserving aspect ratio.
- Rotate 90° left or right.
- Convert to WebP, AVIF, JPEG, or PNG.
- Select a separate output folder for conversion.
- Optional metadata removal.
- JPEG conversion flattens transparency onto white instead of black.
- Conversion applies EXIF Orientation before metadata can be removed.
- Animated GIF / multi-page TIFF to JPEG or PNG uses the first frame/page only.
- Batch processing with configurable parallel ImageMagick workers.
- ImageMagick internal threading is limited when several workers are active.
- Atomic output staging: ImageMagick writes to a hidden temporary file in the destination directory and the final path is replaced only after a successful conversion.
- Cancel stops new work, keeps the progress window modal while active workers terminate, and does not leave a partially written original as the committed result.
- Existing symlink targets are resolved for the atomic commit; dangling output symlinks are refused.
- Best-effort preservation of permissions and Linux user/ACL extended attributes when replacing an existing file.
- Romanian and English application strings. Standard Qt button labels depend on the Qt/platform translation catalogs installed by the desktop environment.
- Stale temporary files created by the application are cleaned conservatively.

## Supported Dolphin MIME types

The Service Menu is deliberately restricted to formats the application is intended to read and write:

- JPEG
- PNG
- WebP
- AVIF
- TIFF
- BMP
- GIF
- HEIF / HEIC

The menu is restricted to the local `file` protocol. It is not offered for `smb://`, `sftp://`, RAW, PSD, XCF, SVG, and other formats that would make in-place resize/rotate unreliable.

## Requirements on Arch Linux

```bash
sudo pacman -S --needed \
  base-devel cmake qt6-base qt6-tools \
  imagemagick libwebp libheif
```

`qt6-tools` provides Qt LinguistTools used to build the embedded Romanian translation.

ImageMagick 7 must provide the `magick` executable. WebP and AVIF/HEIF support depends on the delegates enabled in the installed ImageMagick package. Before a batch starts, the application refuses optional output formats that ImageMagick reports as non-writable; this also prevents in-place HEIC/HEIF resize or rotation when the HEVC encoder is unavailable.

## Install

Do not run the installer itself with `sudo`. It requests elevated privileges only for `/usr/local/bin`.

```bash
./install.sh
```

The installer builds with CMake and installs:

```text
/usr/local/bin/dolphin-image-converter
~/.local/share/kio/servicemenus/dolphin-image-converter.desktop
```

The generated Service Menu uses the same absolute executable path as the CMake install configuration, so `install.sh` and `cmake --install` do not generate different `Exec=` commands.

Restart Dolphin after installation:

```bash
kquitapp6 dolphin 2>/dev/null || true
dolphin &
```

The context menu contains:

```text
Image Tools
    Resize Images...
    Rotate Left 90°
    Rotate Right 90°
    Convert Images...
```

With a Romanian KDE locale, the menu labels are translated.

## Uninstall

```bash
./uninstall.sh
```

Then restart Dolphin.

## Safety behavior

If an output path is the same file as its source, including JPEG → JPEG or PNG → PNG conversion with "Use original base name", the application asks for explicit confirmation before processing.

All outputs are written to hidden temporary files in the same directory as the final destination. A successful ImageMagick exit is followed by an atomic `rename(2)` commit on Unix. On failure or cancellation the temporary file is removed instead of committing a partially written image.

If an output path would replace another selected source image or if two selected inputs would generate the same output path, the batch is refused before any worker starts.

Hard-linked output files are detected. Atomic replacement changes only the selected directory entry, so the application warns before continuing when an existing destination has multiple hard links.

Metadata preservation when replacing an existing file is best-effort. Failure to copy permissions, ACLs, or user extended attributes is reported as a warning; a valid converted image is not discarded solely because optional metadata could not be preserved.

## Conversion notes

`-auto-orient` is applied during conversion before optional metadata stripping, so phone photos retain their displayed orientation after EXIF Orientation is removed.

When converting transparency to JPEG, transparent pixels are composited onto white.

When "Remove EXIF and other metadata" is enabled, ImageMagick `-strip` also removes embedded ICC color profiles. The dialog states this explicitly so wide-gamut images are not silently assumed to retain their original profile.

JPEG, normal PNG, and AVIF are treated as single-image output formats. When the source contains multiple frames/pages, such as an animated GIF or multi-page TIFF, only frame/page 0 is used for those outputs.

## Parallel processing

The default worker count is at most 4 and never exceeds the selected file count or the number of logical processors reported by Qt.

When more than one worker is used, each ImageMagick child process receives:

```text
MAGICK_THREAD_LIMIT=1
```

A conservative per-worker `MAGICK_MEMORY_LIMIT` is also calculated from physical memory when available. This avoids multiplying ImageMagick's own OpenMP parallelism by the number of child processes.

## Translation

The Romanian catalog is:

```text
translations/dolphin-image-converter_ro.ts
```

CMake compiles and embeds it under `:/i18n`. The application follows `QLocale::system().uiLanguages()` in preference order. If the first preferred language is English, it keeps the built-in English strings instead of falling through to Romanian.

The project translates its own application strings. Standard Qt labels such as OK, Cancel, and Show Details are translated only when the corresponding Qt/platform translation catalog is installed.

## Current limitations

- Rotation still re-encodes JPEG through ImageMagick; lossless JPEG rotation with `jpegtran`/`exiftran` is not implemented.
- Settings are not persisted between invocations yet.
- A separate output folder is available for Convert, not yet for Resize.
- The application is exposed through Dolphin and is not installed as a standalone desktop launcher.

## License

Dolphin Image Converter is licensed under the GNU General Public License v3.0 or later.

See [LICENSE](LICENSE) for the full license text.
