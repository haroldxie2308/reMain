# Native Helper Design

## Goal

Keep the plugin reliable on PDFs first.

The helper exists to do exact current-page PDF text extraction that QML cannot
do safely on-device.

## Current Invocation Path

The working path is:

1. `translate.qmd` imports `net.asivery.CommandExecutor 1.0`
2. QML runs `/home/root/xovi/bin/translate-pdf-cli <pdf-path> <page>`
3. the helper prints extracted text to stdout
4. QML translates that text and renders the result

This replaced the earlier `XoviMessageBroker` experiment.

Reason:

- the broker path caused `xochitl` instability during bring-up
- the standalone CLI is simpler to debug and package
- `qt-command-executor` is already a standard XOVI extension

## Helper Scope

The helper only solves PDF page extraction. It does not know about:

- translation targets
- UI state
- notebook OCR
- EPUB heuristics

Those remain in `translate.qmd`.

## PDF Support Implemented

The parser currently handles the subset needed by real reMarkable PDFs:

- xref + trailer parsing
- page-tree traversal
- `/Contents` resolution
- `/FlateDecode` stream inflation
- `/ToUnicode` CMap decoding
- hex `Tj` and `TJ` text extraction

That is enough for exact page text on the PDFs tested during development.

## Packaging Shape

Current package contents:

- `translate.qmd`
- `native-helper/build/translate-pdf-cli.aarch64`

Installed paths:

```text
/home/root/xovi/exthome/qt-resource-rebuilder/translate.qmd
/home/root/xovi/bin/translate-pdf-cli
```

## Follow-Up

Once the PDF path stays stable, the same helper can be reused more aggressively
for EPUB sibling PDFs.
