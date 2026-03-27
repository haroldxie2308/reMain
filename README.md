# reMain — Page Translator for reMarkable

A floating translation overlay for the reMarkable paper tablet, installable
via **[vellum](https://github.com/asivery/vellum)**.

Tap the "T" button to expand the panel, optionally load the current page's
OCR text, select a target language, and tap **Translate** — the result
appears instantly inside the floating box.

![screenshot placeholder](images/screenshot.png)

---

## How it works

The plugin is implemented as a **QMLDiff** (`.qmd`) patch file applied by
[qt-resource-rebuilder](https://github.com/asivery/rm-xovi-extensions/tree/master/qt-resource-rebuilder),
part of the [xovi](https://github.com/asivery/xovi) extension framework.

At boot, `qt-resource-rebuilder` injects the patch into xochitl's compiled
QML resources.  The patch adds a draggable, collapsible floating panel to
the document-view component.  All translation is done client-side inside
xochitl's QML JavaScript engine via `XMLHttpRequest`, calling Google's
**unofficial free** translation endpoint:

```
https://translate.googleapis.com/translate_a/single
  ?client=gtx&sl=auto&tl=<TARGET>&dt=t&q=<TEXT>
```

No API key is required for personal/low-volume use.  For production or
high-volume use, replace the endpoint with the
[official Cloud Translation API](https://cloud.google.com/translate/docs).

---

## Features

| Feature | Details |
|---|---|
| Floating, draggable box | Always visible over the document; position persisted across sessions |
| One-tap expand / collapse | Tap the "T" button to toggle the full panel |
| Load OCR from current page | Reads `xochitl.conf` to find the open document, then loads its `.textconversion` file |
| Language picker | 12 built-in target languages; selection persisted |
| Auto-detect source language | Google's `sl=auto` detects the source automatically |
| Config persistence | Position & language stored in `/home/root/.config/translate-plugin.json` |

---

## Compatibility

| xochitl version | Status |
|---|---|
| **3.25** | ✅ Tested (hash selectors match `FouzR/xovi-extensions 3.25/floating.qmd`) |
| **3.26** | ⚠️ Likely works (same component hashes) — community testing welcome |
| **< 3.25 / > 3.26** | ❌ Untested — see [Porting to other versions](#porting-to-other-xochitl-versions) |

---

## Prerequisites

1. **xovi** — the LD\_PRELOAD extension framework
   ([installation guide](https://github.com/asivery/rm-xovi-extensions#to-install-xovi))
2. **qt-resource-rebuilder** — applies `.qmd` patches to xochitl
   ([part of rm-xovi-extensions](https://github.com/asivery/rm-xovi-extensions/tree/master/qt-resource-rebuilder))
3. **vellum** — the reMarkable package manager
   ([repository](https://github.com/asivery/vellum))
4. The hashtable must be rebuilt after installing or updating
   `qt-resource-rebuilder`:
   ```
   /home/root/xovi/rebuild_hashtable
   ```
5. Network access on your reMarkable (Wi-Fi must be on when translating).

---

## Installation via vellum

```sh
# On the reMarkable over SSH:
vellum add https://github.com/haroldxie2308/reMain/raw/main/VELBUILD
vellum install page-translator
# Reboot xochitl to apply:
systemctl restart xochitl
```

---

## Manual installation

```sh
# Copy the patch file to the qt-resource-rebuilder extension home:
scp translate.qmd root@remarkable:/home/root/xovi/exthome/qt-resource-rebuilder/

# Reboot xochitl to apply:
ssh root@remarkable systemctl restart xochitl
```

---

## Usage

1. Open any notebook or PDF in xochitl.
2. A small **"T"** button appears in the upper-left corner (default position).
3. **Tap** the button to expand the translation panel.
4. **Drag** the header bar to reposition the panel anywhere on screen.
5. _(Optional)_ Tap **Load OCR** to auto-fill the source field with the
   current page's recognised text (requires xochitl OCR to be enabled and
   the document to have been OCR-processed).
6. Tap the language tag (e.g. **en**) to pick a target language.
7. Tap **Translate** — the translated text appears in the result box below.
8. Tap **×** to collapse the panel (position & language are saved).

### Supported target languages

English · Chinese (Simplified) · Chinese (Traditional) · Japanese · Korean ·
Spanish · French · German · Italian · Portuguese · Russian · Arabic

Additional languages can be added by editing the `model: [...]` array in
`translate.qmd`.

---

## Configuration file

The plugin stores its state in `/home/root/.config/translate-plugin.json`:

```json
{
  "targetLang": "en",
  "x": 60,
  "y": 120
}
```

Delete this file to reset the position and language to defaults.

---

## Porting to other xochitl versions

The `AFFECT` and `TRAVERSE` selectors in `translate.qmd` contain **hashed**
identifiers taken from
[FouzR/xovi-extensions 3.25/floating.qmd](https://github.com/FouzR/xovi-extensions/blob/main/3.25/floating.qmd).
These hashes encode the QML file path and component tree for xochitl 3.25.

If you are on a different version:

1. Make sure `xovi` and `qt-resource-rebuilder` are installed and the
   hashtable has been rebuilt for your version.
2. Identify the correct hash for xochitl's document-view QML file on your
   version.  The easiest way is to look at the corresponding `floating.qmd`
   in [FouzR/xovi-extensions](https://github.com/FouzR/xovi-extensions) for
   your version and copy its first `AFFECT` target.
3. Replace the `AFFECT [[…]]` and `TRAVERSE [[…]]` selectors in
   `translate.qmd` with the values for your version.
4. Copy the updated file to
   `/home/root/xovi/exthome/qt-resource-rebuilder/` and restart xochitl.

Pull requests with version-specific hashed files (placed in a `3.XX/`
directory, like FouzR's repo) are very welcome!

### Unhashed development form

If you have access to xochitl's QML hashtable you can also write the patch in
fully **unhashed** form (using real property names) and let QMLDiff hash it:

```sh
qmldiff hash-diffs /path/to/hashtable.json translate-unhashed.qmd
```

The INSERT blocks in `translate.qmd` only use new identifiers, so they require
no hashing regardless.

---

## Notes on the translation API

- The `translate.googleapis.com` endpoint used here is the **unofficial**
  free API (`client=gtx`).  It has no formal SLA and may change without
  notice.
- For privacy-sensitive use, swap in the
  [official Cloud Translation API](https://cloud.google.com/translate/docs/reference/rest)
  by editing the `url` construction in the `doTranslate` function inside
  `translate.qmd`.
- Source language is always set to `auto` (automatic detection).  You can
  change this by modifying `&sl=auto` in the URL.

---

## License

MIT — see [LICENSE](LICENSE).
