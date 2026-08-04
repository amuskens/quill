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
  — plus Bullet List / Numbered List toggles. Always the default font
  (Times); size/face per style: Normal 12pt plain, Heading 1 14pt plain,
  Heading 2 12pt italic, Heading 3 12pt underlined, Heading 4 12pt
  italic+underlined, Quote 12pt italic, Bibliography 10pt plain. **Heading 2
  and Quote are identical** (12pt italic) — a deliberate choice, not a bug;
  see "How paragraph styles/lists work" below for what that means in
  practice. Applying a style **leaves the affected paragraph(s) selected**
  so the change is immediately visible. Bullet/Numbered
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
- **Scrollbar and zoom box**: a real vertical scrollbar (Control Manager,
  `scrollBarProc`) — drag the thumb, click the arrows, or click the track for
  page up/down — and a title-bar zoom box that toggles the window between its
  current size and the Window Manager's standard/maximized state. Both live
  in `app.c`; see "The window chrome" below for how they're wired up.
- **Footnotes**: Insert menu → "Insert Footnote…" opens a dialog to type the
  note; a small in-body marker number is inserted at the cursor, shrunk to
  ~65% size as a superscript approximation (see Known limitations — classic
  TextEdit has no real superscript). **Double-click a marker** to reopen that
  same dialog, pre-filled, for editing. On export to `.docx`, the marker is
  replaced with a real OOXML `footnoteReference` with genuine
  `vertAlign="superscript"` formatting, independent of the on-screen
  approximation.
- **Save / Save As → .qdoc, .docx, .rtf, or .doc**: plain **Save** (⌘S) on a
  document with no file yet defaults to `.qdoc`, not an export format —
  dedicated "Save As .docx/.rtf/.doc" menu items exist for when an export
  format is specifically wanted, so bare Save uses Quill's own native,
  fully round-trippable format instead.
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
- **Save-changes confirmation**: New, Open, and Quit all check `gDoc.dirty`
  first and, if there are unsaved changes, show a Save / Cancel / Don't Save
  dialog before proceeding — picking Save runs the normal save flow (Save
  As if the document has no file yet) and only proceeds with New/Open/Quit
  if that save actually succeeds; Cancel aborts the action entirely, leaving
  the current document untouched. Quit additionally skips the check (and
  the dialog) altogether when the document is empty — nothing worth
  confirming the loss of.

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
- `src/main.r` — Rez resources: menus (`MBAR`/`MENU`), dialogs (`DLOG`/`DITL`)
  for the footnote editor and the save-changes confirmation, alerts
  (`ALRT`/`DITL`) for About/error/warning, and the `SIZE` resource (1.5 MB
  preferred / 512 KB minimum partition). `#include`s `icon.r`.
- `src/icon.r` — the app icon (`BNDL`/`FREF`/`ICN#`/`ics#`/`icl4`/`ics4`) —
  see "The app icon" below.
- `CMakeLists.txt` — Retro68 build description (uses its `add_application()`
  CMake helper; also sets the app's creator code to `Quil`, matching
  `icon.r`'s `BNDL` signature).
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
  Style" itself sets. Most styles share 12pt, so face is often the only
  distinguishing signal — manually italicizing plain 12pt text, for
  instance, would be misidentified as Heading 2/Quote. **Heading 2 and
  Quote are fully identical** (12pt, italic, not bold, not underlined) by
  explicit choice, so they're not just collision-prone but genuinely
  indistinguishable: applying Quote will show up checked as Heading 2 in
  the Style menu, and export with `w:pStyle val="Heading2"`, not Quote.
  Documented tradeoff, not a bug.
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
- **Normal is never a separate "unstyled" state.** Since paragraph style is
  purely derived from a paragraph's own (size, bold, italic, underline) -
  and Normal's entry in `kParaStyleSpecs` *is* `{12, false, false, false}` -
  "plain Times/12pt text" and "Normal style" aren't two different things
  that could drift apart; they're the same data by construction. New
  documents and New/window creation get there by calling
  `ApplyParaStyle(pStyleNormal)` (then immediately resetting the dirty flag
  it sets, since a just-created document isn't unsaved) rather than a
  separate hand-written `TESetStyle` call that happened to produce matching
  numbers - so there's exactly one place that defines what "Normal" means,
  and startup can't quietly drift from it.

## How zoom works

Classic QuickDraw has no coordinate-scaling transform, and TextEdit has no
separate "display size" concept — the only way to make text visibly bigger
or smaller is to change the actual point size TextEdit renders at. So Zoom
**rescales every stored point size** proportionally (`RescaleDocument` in
`app.c`) rather than being a non-destructive visual transform. (A
pixel-accurate, format-preserving zoom is possible in classic Mac OS via an
offscreen GWorld rendered at 100% and blitted through `CopyBits` with a
scaled destination rect — that's how period apps like this actually did it
— but it's meaningfully more machinery: it touches window resize, TE's
destRect/viewRect math, and mouse-click-to-text-offset conversion. Out of
scope for this pass; `RescaleDocument`'s doc comment in `app.c` has the
details if you want to build it.)

After rescaling, `app.c`'s `ForceRedraw()` synchronously erases and redraws
the window right there (rather than just `InvalRect`-ing and waiting for the
next update event) so the new size is visible immediately — the same helper
is used after alignment changes and New.

**Implementation note (this took two attempts):** the first version walked
each style *run* and issued a `TESetSelect`/`TESetStyle` pair per run to
rescale it. That turned out to be unreliable in testing - inconsistent
scaling between normal text and headings, sometimes text not scaling at
all. The likely cause: `TESetStyle` can split, merge, and reindex the style
*table* as a side effect of each call, so a sequence of such calls back to
back in one pass could end up touching the wrong entries or missing some.
The current version instead edits the style **table** directly: every run's
font/size/face lives in exactly one `STElement` entry (`styleTab`, found via
`TEGetStyleHandle`), addressed by `runs[i].styleIndex` - multiple runs can
share one entry. Multiplying every table entry's `stSize` in place rescales
all text in a single pass, doesn't touch the selection at all, and has no
run-splitting side effects to go wrong. The one thing not covered by the
table is TextEdit's separate "null style" (what the *next typed* character
will use - relevant for a still-empty document, or typing at the very end)
- that's rescaled the same way via its own struct
(`nullStyle -> nullScrap -> scrpStyleTab[0].scrpSize`), unconditionally
rather than only in the empty-document case the first version special-cased.
`RunApp()` also calls `AdjustMenus()` once immediately after creating the
window, so Format/Style/Align/Zoom checkmarks are correct before you've
touched anything, not just after the first click.

**A third gotcha, found after that fix**: the caret stopped matching the
text size after zooming - it kept whatever size it was before the zoom
changed, even though the surrounding glyphs correctly scaled. Each
`STElement` (and the null style's `ScrpSTElement`) caches `stHeight`/
`stAscent` (`scrpHeight`/`scrpAscent`) - the line height and ascent
TextEdit measured for that exact font/face/size combination *when the
style was created*. Glyph drawing reads `stSize` directly, so it always
looked right; the caret and line layout use the cached height/ascent
instead, which `RescaleDocument` was leaving untouched. The fix: after
changing each entry's `stSize`, recompute its `stHeight`/`stAscent` from
the font's actual metrics at the new size, the same way `TESetStyle` itself
would - `TextFont`/`TextFace`/`TextSize` to point the current port at that
exact style, then `GetFontInfo` to read back real `ascent`/`descent`/
`leading` for it.

Two consequences of the size-rewriting approach, both handled:

- Style/size detection (menu checkmarks) compares against the size *as it
  would be at 100%* (`UnscaleSize`), not the raw on-screen size, so Style
  menu checkmarks and Format → Size checkmarks stay correct at any zoom.
- **Save/Save As always rescale to 100% first, write the file, then rescale
  back** — so an exported `.docx`/`.rtf`/`.doc` always holds the true logical
  sizes (e.g. a real 12pt Heading) no matter what zoom you were looking at
  when you saved.

**A fourth gotcha, also fixed:** the metrics-recomputation step needs to ask
`GetFontInfo` for each style table entry's real ascent/descent/leading at its
new size, which means pointing the current port at that entry's exact
font/face/size first via `TextFont`/`TextFace`/`TextSize`. Those three calls
mutate the port's *ambient* text state as a side effect — they have nothing
to do with what gets stored in the style table, but if left unrestored, the
port would come out of `RescaleDocument` with its ambient face set to
whatever the last-processed entry happened to be (often a heading's
bold/italic combination). `RescaleDocument` now saves the port's
`txFont`/`txFace`/`txSize` before the metrics loop and restores them
afterward, so zooming can't leave stray formatting state lying around.
(Note: on a system where "Times" only has bitmap glyphs and not a TrueType
outline, arbitrary zoom percentages can still land on point sizes the font
has no hinted bitmap for, which QuickDraw then crudely scales from the
nearest available size — this can look chunkier/heavier at a glance without
any Bold style bit actually being set anywhere; it's a font-rendering
artifact of the classic bitmap-font model, not a formatting bug.)

## The window chrome

The document window uses `zoomDocProc` (the window definition variant with a
title-bar zoom box) instead of plain `documentProc`. It's worth noting this
constant isn't actually exposed to C in this toolchain - it's a Rez-script-only
named constant (Rez has its own symbol table for template values, separate
from what's exported in the C headers), so `app.c` defines it locally as
`kZoomDocProc` with its documented numeric value (8) rather than referencing
a name that doesn't exist at the C level.

Clicking the zoom box is handled the same way as any other window-frame hit:
`FindWindow` reports `inZoomIn`/`inZoomOut`, `TrackBox` tracks the click, and
`ZoomWindow` does the actual resize (toggling between the window's current
size and the Window Manager's standard/maximized state, which it computes a
reasonable default for on first use). Afterward, `ResizeDocumentWindow()` -
the same function the grow-box drag path already used - re-lays-out the TE
view and the scrollbar for the new size, so both paths share one layout
routine (`LayoutContent`) rather than duplicating the geometry math.

The vertical scrollbar is a real Control Manager control
(`NewControl(..., scrollBarProc, ...)`), not a custom-drawn imitation.
Scrolling itself goes through `TEPinScroll`, which shifts TE's `destRect`
relative to a fixed `viewRect` and clamps automatically so it can't scroll
past the start or end of the text - the scrollbar's displayed position is
then *derived* from that relationship (`viewRect.top - destRect.top`) rather
than tracked as separate state that could drift out of sync, the same
single-source-of-truth approach `RescaleDocument` uses for zoom. Dragging the
thumb uses the classic "outline drag" pattern (`TrackControl` with a `NULL`
action proc: the thumb's outline follows the mouse, and the actual content
only scrolls once you release) rather than live-scrolling during the drag -
this is the authentic default Control Manager behavior, not a shortcut.
`UpdateScrollBarRange()` recomputes the thumb's range/position after every
edit that could change line count or wrapping (typing, paste, style/size
changes, zoom, window resize) so it can't fall out of sync with what's
actually on screen.

**Gotcha found after initial testing:** `GlobalToLocal` converts a point
relative to the *current* GrafPort's origin, not necessarily the window being
clicked in. Standard File dialogs (Save/Open) and alerts switch the current
port to their own dialog port while they're up, and classic Mac OS doesn't
guarantee it's restored the instant they close. If a click on the scrollbar
happened to be the first thing the event loop saw after one of those, the
coordinate conversion — and therefore `FindControl`'s hit test — would be
computed against the wrong origin, so the click could silently miss the
control and fall through to `TEClick` instead, making the scrollbar look
inert. The `mouseDown`/`inContent` handler and `UpdateScrollBarRange()`/
`ScrollByPixels()` now explicitly `SetPort(gDoc.window)` before doing any
coordinate conversion or control update, the same defensive pattern
`ForceRedraw()` and `RescaleDocument()` already followed.

The document window now also starts maximized: at the end of
`CreateDocumentWindow()`, `ZoomWindow(gDoc.window, inZoomOut, true)` zooms to
the Window Manager's standard (full-screen) state, followed by
`ResizeDocumentWindow()` to re-lay-out the TE view and scrollbar for the new
size — the same path a user clicking the zoom box triggers.

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

## The app icon

A stylized quill pen — a diagonal tapered shaft, feather barbs branching off
one edge, and a small ink drop at the nib — in 4 colors from the standard
classic Mac OS 16-color icon palette: white background, black
outline/barbs/nib, tan feather fill, blue ink drop. Provided as both a
1-bit fallback (`ICN#`/`ics#`, 32×32 and 16×16) and true color
(`icl4`/`ics4`, 4-bit/16-color depth, though only 4 of the 16 are actually
used).

The pixel data was generated programmatically (a small Python script doing
circle/line-segment distance math, not hand-plotted) rather than
transcribed by hand — accurately hand-plotting ~1KB of hex bitmap data
across four resources is exactly the kind of tedious, error-prone task a
script does more reliably. The 16-color palette's index order (0=white,
1=yellow, ... 15=black — `icl4`/`ics4` reference colors by index into this
*fixed system palette*, not an arbitrary embedded one) was cross-checked
against two independent sources rather than trusted from memory, since
getting an index wrong would silently produce a wrong-but-plausible-looking
color.

`src/icon.r` also defines the `BNDL`/`FREF` pair that tells Finder to
actually use this icon for the app: `BNDL`'s signature (`'Quil'`) must match
the app's own creator code, which `CMakeLists.txt` sets via
`add_application(... CREATOR "Quil")` — without that match, Finder falls
back to the generic application icon regardless of what icon resources
exist in the file.

**If Finder still shows the old icon after rebuilding:** this is almost
always the Desktop Database, not a stale build. Finder/System 7 caches each
creator code's icon the first time it sees a file with that signature, and
won't re-read `BNDL`/`ICN#`/`icl4` from a rebuilt file with the *same*
creator code on its own. Rebuilding the desktop database forces it to
re-scan: hold down Command+Option while the Finder is starting up (or while
inserting/mounting the disk image), and confirm the "rebuild the desktop
file" prompt. This has been confirmed on this end — a freshly built
`Quill.bin` genuinely contains the quill-pen `icl4`/`ICN#` data and the
`Quil` creator/signature (checked directly against the built binary's
bytes), so if the icon still looks wrong after a desktop rebuild, that would
point at something else worth re-checking rather than the resource data
itself.

## Known limitations

- **Open only reads `.qdoc`**: `.docx`/`.rtf`/`.doc` are write-only export
  targets — reading real-world `.docx`/`.rtf`/binary `.doc` back would need a
  full parser (plus, for `.docx`, a ZIP directory reader) for each format,
  a much bigger lift than `.qdoc`'s reader, which only has to understand
  its own output. Out of scope for this pass.
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
