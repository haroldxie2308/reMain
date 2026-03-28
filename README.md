# reMain

Floating page translation for reMarkable documents.

The plugin adds a small camera-style button to the document toolbar. Tapping it
opens a draggable translation panel with:

- `Translate` header
- `To` language rotation: `KR -> CN -> EN`
- `Refresh` to read the current page and translate it in one step
- fold/unfold chevron

The current UI is black/white only and shows translated text only.

## Status

- PDF page extraction: working
- Translation flow: working
- EPUB support: still fallback-heavy and not the main focus
- Tested on reMarkable Paper Pro, reMarkable OS `3.26.0.68`

## Architecture

`translate.qmd` is a `qt-resource-rebuilder` patch that injects the UI into
`xochitl`.

For PDFs, the patch calls a small native helper through
`net.asivery.CommandExecutor 1.0`:

- QML calls `/home/root/xovi/bin/translate-pdf-cli`
- the helper extracts text from the current PDF page
- QML sends that text to `translate.googleapis.com`
- the translated result is shown in the floating panel

Notebook `.textconversion` remains as a fallback. EPUB support is currently a
mix of cached section slicing and sibling-PDF fallback.

## Requirements

- `xovi`
- `qt-resource-rebuilder`
- `qt-command-executor`
- network access on the tablet for translation requests

## Install With Vellum

```sh
vellum add https://github.com/haroldxie2308/reMain/raw/main/VELBUILD
vellum install page-translator
/home/root/xovi/start >/dev/null 2>&1 &
```

This installs:

- `/home/root/xovi/exthome/qt-resource-rebuilder/translate.qmd`
- `/home/root/xovi/bin/translate-pdf-cli`

## Manual Install

```sh
scp translate.qmd rmpp:/home/root/xovi/exthome/qt-resource-rebuilder/translate.qmd
scp native-helper/build/translate-pdf-cli.aarch64 rmpp:/home/root/xovi/bin/translate-pdf-cli
ssh rmpp 'chmod 755 /home/root/xovi/bin/translate-pdf-cli && /home/root/xovi/start >/dev/null 2>&1 &'
```

## Usage

1. Open a PDF.
2. Tap the camera button.
3. Tap `Refresh`.
4. Read the translated text.
5. Tap `To` to rotate target language.
6. Tap the chevron to fold or unfold the panel.

The camera button remains the open/close toggle for the whole panel.

## Building The Helper

Host-side parser checks:

```sh
make -C native-helper host-cli
native-helper/build/translate-pdf-cli /path/to/file.pdf 19
```

Device binaries must be cross-compiled with the official reMarkable SDK for
the target OS release. The checked-in package artifact is:

- `native-helper/build/translate-pdf-cli.aarch64`

## Repo Layout

- [`translate.qmd`](/Volumes/Local/reMain/translate.qmd): main QML patch
- [`native-helper/src/pdf_text_extractor.cpp`](/Volumes/Local/reMain/native-helper/src/pdf_text_extractor.cpp): PDF parser
- [`native-helper/src/translate_helper_cli.cpp`](/Volumes/Local/reMain/native-helper/src/translate_helper_cli.cpp): CLI entrypoint
- [`native-helper/build/translate-pdf-cli.aarch64`](/Volumes/Local/reMain/native-helper/build/translate-pdf-cli.aarch64): packaged helper binary
- [`extract_epub_cache.sh`](/Volumes/Local/reMain/extract_epub_cache.sh): optional EPUB cache generator used by the fallback path
- [`docs/native-helper-design.md`](/Volumes/Local/reMain/docs/native-helper-design.md): helper notes

## Notes

- The translation endpoint is the unofficial Google Translate `client=gtx`
  endpoint.
- The current panel defaults to automatic source-language detection.
- If `qt-resource-rebuilder` selectors drift on a future OS release, the hash
  targets in `translate.qmd` will need to be refreshed.

## License

MIT. See [`LICENSE`](/Volumes/Local/reMain/LICENSE).
