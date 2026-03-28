# Native Helper

This directory contains the working PDF-first helper used by `translate.qmd`.

Current runtime path:

- `translate.qmd` imports `net.asivery.CommandExecutor 1.0`
- QML executes `/home/root/xovi/bin/translate-pdf-cli`
- the CLI reads one PDF page and writes extracted text to stdout
- QML forwards that text to the translation endpoint

The earlier `XoviMessageBroker` extension approach is no longer used.

## Files

- `src/pdf_text_extractor.cpp`: narrow PDF parser for page text extraction
- `src/translate_helper_cli.cpp`: standalone CLI entrypoint
- `build/translate-pdf-cli.aarch64`: packaged device binary
- `Makefile`: host CLI build helper

## Local Checks

```sh
make -C native-helper host-cli
native-helper/build/translate-pdf-cli /path/to/file.pdf 19
```

The page index is zero-based.

## Device Path

The installed binary path is:

```text
/home/root/xovi/bin/translate-pdf-cli
```

`translate.qmd` calls that binary directly.

## Cross-Compile Note

The checked-in `translate-pdf-cli.aarch64` binary was built with the official
reMarkable SDK for `ferrari` / OS `3.26.0.68`.
