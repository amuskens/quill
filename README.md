# Quill — a clean word processor for classic Mac OS

A real classic Mac OS / System 7 app, built with a genuine 68k cross-compiler
([Retro68](https://github.com/autc04/Retro68), a GCC-based toolchain) instead
of native `cc`. Quill aims to feel like a clean, modern word processor —
paragraph styles, alignment, zoom, lists, footnotes — while running natively
on 68k System 7 (or Mini vMac).

## Features

- **Rich text / WYSIWYG**: uses styled TextEdit (`TEStyleNew`) so formatting
  renders live in the window, not just on export. Default font is Times.
- **Format menu**: Bold/Italic/Underline toggles, a fixed set of common Mac
  fonts (Times, Geneva, New York, Helvetica, Courier, Monaco, Palatino), and
  point sizes (9–24). Checkmarks reflect the style at the current selection.
- **Style menu**: paragraph styles — Normal, Heading 1–4, Quote, Bibliography
  — plus Bullet List / Numbered List toggles. Every style uses the default
  font at 12pt; only the face distinguishes them (Heading 1 bold, Heading 2
  italic+bold, Heading 3 italic, Heading 4 underline, Quote italic+underline,
  Bibliography bold+italic+underline). Applying a style **leaves the affected
  paragraph(s) selected** so the change is immediately visible. Bullet/Numbered
  List items continue onto the next line when you press Return, like Word —
  pressing Return again on an empty item exits the list instead of adding
  another marker. See "How paragraph styles/lists work" below for how this
  maps onto exported files.
- **Align menu**: Left / Center / Right / Justify, applied to the whole
  document — the menu itself says so (a permanently-disabled "Whole
  Document:" label heads the menu) since classic TextEdit alignment isn't
  per-paragraph. See Known limitations for what "Justify" actually does and
  why per-paragraph alignment isn't implemented.
- **Zoom menu**: 50% / 100% / 150% / 200% / 300% / 400%, view-only — it never
  changes what actually gets saved (see "How zoom works").
- **Footnotes**: Insert menu → "Insert Footnote…" opens a dialog to type the
  note; a small in-body marker number is inserted at the cursor, shrunk to
  ~65% size as a superscript approximation (see Known limitations — classic
  TextEdit has no real superscript). **Double-click a marker** to reopen that
  same dialog, pre-filled, for editing. On export to `.docx`, the marker is
  replaced with a real OOXML `footnoteReference` with genuine
  `vertAlign="superscript"` formatting, independent of the on-screen
  approximation.
- **Save / Save As → .qdoc, .docx, .rtf, or .doc**:
  - `.qdoc`: Quill's own format (`src/native.c`) — the only one of the four
    that's also readable via **File → Open…**, for resuming a writing session
    with full fidelity (exact per-run formatting, footnotes, zoom, alignment).
    See "How .qdoc works" below.
  - `.docx`: hand-rolled, dependency-free OOXML writer (`src/docx.c`) plus a
    minimal ZIP archive writer (`src/zip.c`, stored/uncompressed entries — no
    deflate implementation needed since OOXML doesn't require compression).
    Real `w:pStyle` (Heading1–4 use Word's own built-in style IDs), real
    `w:numPr` list numbering via a generated `word/numbering.xml`, and
    per-paragraph `w:jc` alignment.
  - `.rtf` / `.doc`: hand-rolled RTF writer (`src/rtf.c`) — see "Why .doc is
    actually RTF" below.
- **Open…**: reads `.qdoc` files back (only — see Known limitations for why
  `.docx`/`.rtf`/`.doc` are write-only).

## Structure

- `src/main.c` — the actual C entry point: just calls `RunApp()`.
- `src/app.c` / `app.h` — the app itself: window, menu bar, event loop,
  dialogs, formatting commands, zoom, Save/Save As via `StandardPutFile`.
  Everything is `static` except `RunApp()`.
- `src/wordproc.h` — shared `Document`/`Footnote` types, the font list, the
  `ParaStyleKind`/`ListKind` detection tables (shared by `app.c`, `docx.c`,
  `rtf.c`), the Mac OS Roman→Unicode table, and 68k-safe Pascal-string
  helpers (`CopyCStringToPascal`/`CopyPascalStringToC` are Carbon-only in
  this toolchain, so `wordproc.h` provides its own `CToPascal`/`PascalToC`).
- `src/docx.c` / `docx.h` — walks the TextEdit style-run table and footnote
  list to generate `word/document.xml`, `word/footnotes.xml`, `word/styles.xml`,
  `word/numbering.xml`, content types, and relationship parts, then zips them.
- `src/rtf.c` / `rtf.h` — walks the same style-run/footnote data to generate
  a single RTF stream (font table, direct run formatting, inline footnote
  groups, document-wide alignment).
- `src/native.c` / `native.h` — Quill's own `.qdoc` format: both a writer
  (same style-run walk as `docx.c`/`rtf.c`) and, uniquely, a **reader** — a
  small hand-rolled parser tailored to exactly the shape this same file
  writes, not a general XML parser (see its header comment for why that
  trade-off is fine here).
- `src/zip.c` / `zip.h` — minimal ZIP writer (stored entries only) with a
  hand-rolled CRC32.
- `src/main.r` — Rez resources: menus (`MBAR`/`MENU`), the footnote dialog
  (`DLOG`/`DITL`), About/error alerts (`ALRT`/`DITL`), and the `SIZE` resource
  (1.5 MB preferred / 512 KB minimum partition).
- `CMakeLists.txt` — Retro68 build description (uses its `add_application()`
  CMake helper).
- `Makefile` — convenience wrapper around the CMake build.

## How paragraph styles/lists work

There's no paragraph-metadata structure sitting alongside the text (the way
`Document.footnotes[]` sits alongside the body). That would need to stay in
sync across every insert/delete/cut/paste, which is exactly the kind of
bookkeeping classic TextEdit already does correctly for *character* styling
via its style-run table — so paragraph style and list membership are instead
**derived from content each time they're needed** (screen-checkmark refresh,
export), piggybacking on TextEdit's own tracking instead of duplicating it:

- **Paragraph style** (Heading 1–4 / Quote / Bibliography) is recognized by
  matching a paragraph's own (size, bold, italic, underline) against a fixed
  table (`kParaStyleSpecs` in `wordproc.h`) — the same combination "Apply
  Style" itself sets. Since every style now shares the same 12pt size, the
  *only* signal is the face combination — manually toggling Bold+Underline on
  plain text, for instance, isn't one of the defined combinations, but
  Heading 3's italic-alone *is* something a user might reach for just for
  emphasis, and would be misidentified as that heading. Documented tradeoff,
  not a bug.
- **List membership** is recognized by a literal `"• "` or `"<digits>. "`
  prefix at the paragraph's start, inserted by the Bullet/Numbered List
  commands. On `.docx` export, that literal prefix is *stripped* and replaced
  with real `w:numPr` numbering (so Word renders/renumbers it as an actual
  list) — the same splice-out-the-marker trick used for footnote references.
  RTF export, by contrast, keeps the literal marker characters as-is (see
  below).
- List numbers are assigned once, left-to-right over whatever paragraphs the
  selection touched, when you invoke the command — not dynamically
  renumbered if you later add/remove/reorder items (same tradeoff as
  footnote numbers).

## How zoom works

Classic QuickDraw has no coordinate-scaling transform, and TextEdit has no
separate "display size" concept — the only way to make text visibly bigger
or smaller is to change the actual point size TextEdit renders at. So Zoom
**literally rescales every run's stored point size** proportionally
(`RescaleDocument` in `app.c`) rather than being a non-destructive visual
transform. (A pixel-accurate, format-preserving zoom is possible in classic
Mac OS via an offscreen GWorld rendered at 100% and blitted through
`CopyBits` with a scaled destination rect — that's how period apps like
this actually did it — but it's meaningfully more machinery: it touches
window resize, TE's destRect/viewRect math, and mouse-click-to-text-offset
conversion. Out of scope for this pass; `RescaleDocument`'s doc comment in
`app.c` has the details if you want to build it.)

After rescaling, `app.c`'s `ForceRedraw()` synchronously erases and redraws
the window right there (rather than just `InvalRect`-ing and waiting for the
next update event) so the new size is visible immediately — the same helper
is used after alignment changes and New.

`RescaleDocument` walks the *existing* style runs, which is all there is to
rescale once a document has real text. But a brand-new, still-empty document
has no runs at all yet — TextEdit tracks "what style should the next typed
character get" separately (its "null style"). Without special-casing that,
zooming an empty window and then typing would produce text at the old,
unscaled size. `RescaleDocument` detects the no-real-runs case
(`nRuns <= 1`) and rescales that next-character style too, via
`TEContinuousStyle`/`TESetStyle` on the (necessarily collapsed, since
there's no text) selection — the documented, public mechanism for reading
and writing it. `RunApp()` also calls `AdjustMenus()` once immediately after
creating the window, so Format/Style/Align/Zoom checkmarks are correct
before you've touched anything, not just after the first click.

Two consequences of the size-rewriting approach, both handled:

- Style/size detection (menu checkmarks) compares against the size *as it
  would be at 100%* (`UnscaleSize`), not the raw on-screen size, so Style
  menu checkmarks and Format → Size checkmarks stay correct at any zoom.
- **Save/Save As always rescale to 100% first, write the file, then rescale
  back** — so an exported `.docx`/`.rtf`/`.doc` always holds the true logical
  sizes (e.g. a real 12pt Heading) no matter what zoom you were looking at
  when you saved.

## How .qdoc works

`.qdoc` is a small custom XML dialect, documented in full in `native.c`'s
header comment. Unlike `.docx`/`.rtf`, nothing is derived or approximated —
it stores exactly what's needed to resume editing losslessly:

- Every run's actual font/size/bold/italic/underline (not the
  content-derived paragraph-style/list detection docx/rtf export use).
- Every footnote's number, anchor offset, marker length, and body text.
- The current zoom percentage and alignment, saved and restored as-is (this
  is the one format that does *not* normalize to 100% zoom before writing —
  the whole point is faithfully resuming the session you were just in).

Text is stored as raw Mac OS Roman bytes with just the four XML
metacharacters (`&`, `<`, `>`, `"`) escaped — no Unicode transcoding, since
this format is only ever read back by this same app on the same platform,
unlike `.docx`/`.rtf` which need to be readable by other software.

The reader is deliberately not a general XML parser: it's a hand-rolled
scanner that assumes exactly the shape the writer produces (attributes
always quoted, tags never nested inside same-named tags). That's a
reasonable trade for a format with exactly one producer and one consumer,
both this file — but it does mean a `.qdoc` file hand-edited into an
unusual shape could confuse it. Malformed/foreign files are detected (missing
`<quilldoc`/`<body>` tags) and rejected with an error rather than crashing.

## Document size limit

Classic TextEdit tracks every offset it deals with — `selStart`, `selEnd`,
`teLength`, the line-start table, each style run's start character — as a
16-bit signed `INTEGER` (confirmed directly in the Toolbox headers this
toolchain ships). That makes **~32,767 characters (roughly 5,000–5,500
words, ~20 double-spaced pages) a hard ceiling**, not a tunable one — it's
not affected by the app's heap size (`SIZE` resource: 512 KB min / 1.5 MB
pref), which is comfortably large enough that heap exhaustion isn't the
real constraint here. Approaching the real limit degrades before it
outright crashes (garbled selection, wrong line breaks), so the safe
ceiling is somewhat below 32,767, not exactly at it.

Quill warns once (`CheckDocumentSize` in `app.c`, checked after typing,
pasting, or opening a file) when a document crosses **28,000 characters**
— about a 10% margin — via a caution alert, then stays quiet unless the
document is trimmed back under the threshold and crosses it again.

This can't be fixed by "buffering" in the virtual-memory sense — it's an
*addressing* limit, not a capacity one; more RAM doesn't help a 16-bit
offset point past 32,767. A real fix means retiring single-TE-record
TextEdit for large documents in favor of a custom paginated/multi-record
engine (each chunk its own TE record, swapped in/out as the user scrolls)
— the same scale of rewrite as true per-paragraph alignment, and, like
that, deliberately out of scope for this pass.

## Why .doc is actually RTF

Real binary `.doc` (the OLE2/Compound File Binary + Word `WordDocument`
stream format) is a large, intricate binary format — implementing a correct
writer for it from scratch is well beyond what's reasonable to hand-roll
safely in a toolchain this size. "Save As .doc" instead writes the same RTF
content `src/rtf.c` produces, just under a `.doc` filename. This isn't a
trick that only sometimes works: Word and virtually every other word
processor identify RTF by its content header regardless of file extension,
so a `.doc`-named RTF file opens correctly everywhere. It's documented here
so it's never a surprise.

## Known limitations

- **Open only reads `.qdoc`**: `.docx`/`.rtf`/`.doc` are write-only export
  targets — reading real-world `.docx`/`.rtf`/binary `.doc` back would need a
  full parser (plus, for `.docx`, a ZIP directory reader) for each format,
  a much bigger lift than `.qdoc`'s reader, which only has to understand
  its own output. Out of scope for this pass.
- **No unsaved-changes prompt**: New and Open both discard the current
  document without asking. Consistent with the rest of the app (there's no
  "are you sure?" anywhere), but worth knowing.
- **No true superscript on-screen**: classic TextEdit's style byte has bits
  for bold/italic/underline/outline/shadow/condense/extend and nothing else
  — there's no baseline-shift or superscript concept at the Toolbox level.
  Footnote markers are approximated with a smaller (~65%) size instead. The
  exported `.docx` uses genuine superscript formatting regardless.
- **Alignment is document-wide, not per-paragraph**: classic TextEdit's
  `TESetAlignment` sets a single value for the whole TE record. Making it
  truly per-paragraph would mean replacing TextEdit's automatic layout with
  a custom multi-frame text engine (each paragraph as its own mini text box,
  independently laid out) — a rewrite of the editing core touching
  selection, click hit-testing, scrolling, and all the footnote/list/style
  code, which currently assumes one continuous text buffer. Deliberately out
  of scope for now; the Align menu itself is labeled "Whole Document:" so
  it's not a silent limitation. "Justify" isn't a concept classic TextEdit
  has at all (no stretch-to-margins algorithm), so the Justify menu item
  maps to left-align — the closest available option — rather than being
  silently absent.
- **Footnote text is capped at 255 characters** (`GetDialogItemText` returns
  a Pascal string) and footnotes are **numbered by insertion order**, not
  reflowed if you delete one.
- **No OS-level clipboard sync**: `TEToScrap`/`TEFromScrap` are declared but
  not actually implemented for the 68k target in this toolchain (Carbon-only
  in the headers), so Cut/Copy/Paste work fine *within* the app but won't
  exchange text with the emulator's host clipboard.
- **Single document window** — no multi-window/MDI support.
- **`.doc` is RTF content under a `.doc` name** — see above.
- Paragraph styles and lists are derived from content, not stored — see
  "How paragraph styles/lists work" above for the exact matching rules.
- Zoom rescales actual point sizes and is restored around Save — see
  "How zoom works" above.
- Non-ASCII text is transcoded from Mac OS Roman using a built-in table
  (`kMacRomanHigh` in `wordproc.h`) — UTF-8 for `.docx`, `\uNNNN` escapes for
  RTF/`.doc` — covering standard accented Latin letters and common symbols
  (curly quotes, dashes, bullets, etc.).

## Toolchain

The Retro68 cross-toolchain lives outside this repo, at:

```
/Users/amuskens/repo/toolchain/Retro68           # source checkout
/Users/amuskens/repo/toolchain/Retro68-build      # build dir + installed toolchain
```

It was built 68k-only (`--no-ppc`) since System 7 is the 68k-Mac era. If you
later need PowerPC/Carbon too, rerun `build-toolchain.bash` there without
that flag.

## Build

```sh
cd /Users/amuskens/repo/mac-system7-c
make
```

This configures CMake against the Retro68 toolchain file, builds, and then
assembles a floppy image. Output lands in `build/`:

- `Quill.img` — a **1.44 MB (HD) HFS floppy image** with the app already
  copied onto it, ready to mount. Built via the toolchain's bundled `hfsutils`
  (`hformat` to lay down an empty HFS volume in a 1440-block/1.44 MB file,
  `hmount`/`hcopy -m`/`humount` to copy the MacBinary-encoded app onto it
  preserving both forks and type/creator).
- `Quill.bin` — the MacBinary-encoded app by itself (what gets copied into
  the `.img`).
- `Quill.dsk` — Retro68's own 800 K disk image (still produced as a
  byproduct of the CMake build; `Quill.img` is the one to use).
- `Quill.APPL` — the raw resource-fork app.

If you move the toolchain, override the path: `make RETRO68=/other/path`.
To change the image size, edit `IMG_BLOCKS` in the `Makefile` (1 block = 1 KB;
1440 = 1.44 MB HD, 2880 = 2.88 MB ED).

## Running it (Mini vMac)

Mini vMac is already installed, at:

```
/Users/amuskens/repo/toolchain/minivmac/Mini vMac 26.app
```

This is the **Macintosh II** variant (Color QuickDraw, needed for System 7),
x86-64 build — runs fine under Rosetta 2 on Apple Silicon. Quarantine flag was
already cleared so Gatekeeper won't block it.

### 1. Put your ROM in place

Mini vMac needs a Mac II ROM image named exactly **`vMac.ROM`**, placed at:

```
~/Library/Preferences/Gryphel/mnvm_rom/vMac.ROM
```

(That folder already exists — just drop your ROM file in and rename it.) This
location is shared by every Mini vMac variant you run, regardless of where the
`.app` lives. ROM images are Apple-copyrighted — use your own, dumped from
hardware you own.

### 2. Boot System 7

Launch `Mini vMac 26.app`. If it can't find a startup disk, drag your System 7
boot/hard-disk image (also your own, e.g. exported from real hardware) onto
the emulator window to mount it, then restart the emulator.

### 3. Get the app onto it

Once System 7 is booted, drag `build/Quill.img` (built by `make`, see above)
onto the Mini vMac window — the Mac II's SuperDrive/FDHD controller Mini
vMac emulates handles 1.44 MB HD images, so it mounts as a floppy on the
desktop. Copy `Quill` from it and double-click to run.

Reference: Gryphel Project's [Getting Started](https://www.gryphel.com/c/minivmac/start.html)
and [macOS notes](https://www.gryphel.com/c/minivmac/osx_note.html) pages.

## Notes

- Toolbox API coverage matches System 7.0 (no Carbon, no MacTCP/OpenTransport,
  nothing from System 7.5+/Mac OS 8 unless you build the PowerPC/Carbon
  targets too).
- Add more `.c`/`.r` files to `CMakeLists.txt`'s `add_application()` call as
  the app grows.
