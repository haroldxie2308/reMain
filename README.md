# reMain

Floating page translation for reMarkable documents.

The plugin adds a small camera-style button to the document toolbar. Tapping it
opens a draggable translation panel with:

- `<>` fold handle in the header
- `To` language rotation:
  `KR -> CN -> EN -> JP -> ES -> FR -> DE -> IT -> PT -> RU`
- `Refresh` to read the current page and translate it in one step
- fold into a small square and unfold again from that square

The current UI is black/white only and shows translated text only.

## Status

- PDF page extraction: working
- Translation flow: working
- PDF context expansion across page boundaries: working
- EPUB support: still fallback-heavy and not the main focus
- Tested on reMarkable Paper Pro, reMarkable OS `3.26.0.68`

## Architecture

`translate.qmd` is a `qt-resource-rebuilder` patch that injects the UI into
`xochitl`.

For PDFs, the patch calls a small native helper through
`net.asivery.CommandExecutor 1.0`:

- QML calls `/home/root/xovi/bin/translate-pdf-cli`
- the helper extracts text from the current PDF page
- QML also reads `page - 1` and `page + 1` and extends to nearby separators so
  a sentence that crosses the page boundary is less likely to be cut off
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

For this self-hosted release, install from the release `.apk`, not from the raw
`VELBUILD`.

1. Download these two release assets:
- `page-translator-0.2.1-r0.apk`
- [`vellum-dev.rsa.pub`](/Volumes/Local/reMain/keys/vellum-dev.rsa.pub)

2. Copy them to the tablet and install:

```sh
mkdir -p /home/root/.vellum/etc/apk/keys
cp vellum-dev.rsa.pub /home/root/.vellum/etc/apk/keys/
vellum add page-translator-0.2.1-r0.apk
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
6. Tap `<>` to fold the panel, or tap the folded square to reopen it.

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
- [`keys/vellum-dev.rsa.pub`](/Volumes/Local/reMain/keys/vellum-dev.rsa.pub): public key for the release package

## Releasing

This repo is structured for self-hosted vellum releases via release assets. The
release flow is:

1. Build or verify the packaged helper:

```sh
make -C native-helper host-cli
```

If the native helper changed, rebuild `native-helper/build/translate-pdf-cli.aarch64`
with the reMarkable SDK before releasing.

2. Bump the version in [`VELBUILD`](/Volumes/Local/reMain/VELBUILD):

- `pkgver=...`
- `_commit="vX.Y.Z"`

The `_commit` value should match the Git tag you are about to create. That is
what makes the source package reproducible.

3. Build the signed `.apk` and keep the private signing key outside the repo.

4. Commit the release prep.

5. Create the tag locally:

```sh
git tag v0.2.1
```

6. Push the branch and tag together:

```sh
git push origin main v0.2.1
```

7. Publish a GitHub release for that tag.

If release automation is configured, the workflow will attach:

- `page-translator-0.2.1-r0.apk`
- `vellum-dev.rsa.pub`

If release automation is not configured yet, attach those two files manually.

8. Test the release install on a tablet:

```sh
mkdir -p /home/root/.vellum/etc/apk/keys
cp vellum-dev.rsa.pub /home/root/.vellum/etc/apk/keys/
vellum add page-translator-0.2.1-r0.apk
/home/root/xovi/start >/dev/null 2>&1 &
```

## Release Automation

GitHub Actions can attach the release assets automatically when you publish a
GitHub release.

The workflow expects one repository secret:

- `VELLUM_SIGNING_KEY`: the private RSA key that signs the `.apk`

Create it under GitHub:

- `Settings -> Secrets and variables -> Actions -> New repository secret`

The matching public key is tracked at
[`keys/vellum-dev.rsa.pub`](/Volumes/Local/reMain/keys/vellum-dev.rsa.pub). The
workflow derives a public key from the secret and fails if it does not match the
tracked public key.

Once that secret is set, publishing a GitHub release will automatically:

- build `page-translator-<version>-r0.apk`
- upload the `.apk`
- upload `vellum-dev.rsa.pub`

## Notes

- The translation endpoint is the unofficial Google Translate `client=gtx`
  endpoint.
- The current panel defaults to automatic source-language detection.
- If `qt-resource-rebuilder` selectors drift on a future OS release, the hash
  targets in `translate.qmd` will need to be refreshed.
- Only the public key should be committed or attached to releases. Keep the
  private signing key local and backed up safely.

## License

MIT. See [`LICENSE`](/Volumes/Local/reMain/LICENSE).
