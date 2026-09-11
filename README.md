# Dolphin Image Converter 0.4.5

Batch image tools for KDE Dolphin, powered by Qt 6 and ImageMagick.

## Screenshot
<img width="440" height="557" alt="Screenshot_20260911_084052" src="https://github.com/user-attachments/assets/9c97d26e-5af1-4ced-b222-ffed684c8d9a" />

## Features

- Resize by width, by height, or inside a width × height box while preserving aspect ratio.
- Rotate 90° left or right, using EXIF metadata-only rotation for oriented JPEGs and lossless `jpegtran` transforms for compatible normal-orientation JPEGs.
- Convert to WebP, AVIF, JPEG, or PNG.
- Select a separate output folder for Resize or Convert.
- Optional metadata removal.
- JPEG conversion flattens transparency onto white instead of black.
- Conversion applies EXIF Orientation before metadata can be removed.
- Animated GIF / multi-page TIFF to JPEG or PNG uses the first frame/page only.
- Remembers commonly used Resize, Convert, output-folder, suffix, and parallel-job settings between invocations.
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
  imagemagick libjpeg-turbo libwebp libheif
```

`qt6-tools` provides Qt LinguistTools used to build the embedded Romanian translation. `libjpeg-turbo` provides `jpegtran`, used for normal-orientation JPEGs when a perfect lossless transform is possible.

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


## JPEG lossless rotation

JPEG/JFIF rotation uses three processing paths, chosen automatically:

1. The application parses EXIF Orientation itself and uses the same parser for both the decision and the metadata rewrite. A valid Orientation value is therefore interpreted consistently in both places.
2. The XMP `tiff:Orientation` value is parsed as well as its presence. Missing XMP is represented as 0, valid values as 1–8, and an Orientation token that cannot be interpreted safely as -1. EXIF and XMP are compared before choosing a rotation path.
3. If effective EXIF Orientation is normal (1, including a missing EXIF tag) and XMP is missing or also 1, `jpegtran -copy all -perfect -rotate 90/270` is used when available. This keeps common Photoshop/Lightroom JPEGs with EXIF=1 and XMP=1 on the lossless DCT path.
4. If EXIF Orientation is 2–8 and XMP Orientation is absent or contains exactly one matching `tiff:Orientation` value, the requested 90° rotation is composed into the metadata only. EXIF is updated and, when present, the same new orientation is written to the single XMP ASCII digit. JPEG pixel data is not decoded, transformed, or recompressed. This is instantaneous, has no iMCU alignment restriction, and keeps the stored JPEG and embedded thumbnail data unchanged.
5. If EXIF or XMP Orientation is malformed, or the two metadata sources disagree, the job takes the conservative ImageMagick path. If a perfect `jpegtran` transform is not possible, that job also falls back to ImageMagick. Re-encoded files are reported with a warning.

If the metadata-only update unexpectedly cannot locate or safely rewrite the same valid Orientation metadata that was inspected earlier, the job falls back to ImageMagick rather than committing a questionable file. XMP is rewritten only when exactly one parseable `tiff:Orientation` value exists and it matches EXIF. All paths still use the same temporary-file staging and atomic commit logic.

JPEG orientation inspection parses only the marker/header chain before the Start of Scan marker. File I/O starts with a 256 KiB prefix and falls back to reading the remainder only when that prefix does not contain the complete JPEG header chain. This avoids reading entire large JPEG files just to inspect EXIF/XMP metadata in the common case.

If `jpegtran` is not installed, EXIF-oriented JPEGs can still use the metadata-only lossless path. Normal-orientation JPEGs continue to work through ImageMagick, but their requested rotation is then re-encoded. On Arch Linux, `jpegtran` is provided by `libjpeg-turbo`.

For the `jpegtran` path, `-copy all` preserves embedded EXIF thumbnails as-is, so an embedded thumbnail may still show the pre-rotation orientation. `jpegtran` also does not update EXIF `PixelXDimension` / `PixelYDimension` after a 90° physical transform. These limitations do not apply to the metadata-only path because the stored pixel matrix is not changed.

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

## Persistent settings

The application remembers the last commonly used settings through Qt `QSettings`, including Resize mode and dimensions, Convert format and quality, suffix choices, output folders, metadata-removal choice, and the parallel-job count. The settings namespace is application-specific (`DolphinImageConverter/dolphin-image-converter`) and contains no personal or company identifiers.

Resize and Convert can both use either the source folder or a remembered custom output folder. If a saved custom folder no longer exists, the next invocation safely falls back to the source folder.

Canceling or closing an options dialog does not save changes. If the current selection contains fewer files than the remembered parallel-job preference, the spin box is temporarily clamped without overwriting that remembered preference.

## Current limitations

- The application is exposed through Dolphin and is not installed as a standalone desktop launcher.

## License

Dolphin Image Converter is licensed under the GNU General Public License v3.0 or later.

See [LICENSE](LICENSE) for the full license text.
