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
- **Style menu**: paragraph styles — Normal, Heading 1–4, Quote, Bibliography,
  Plain Text — plus Bullet List / Numbered List toggles. Size/face per style:
  Normal 12pt plain, Heading 1 14pt bold, Heading 2 12pt bold+italic,
  Heading 3 12pt bold+underlined, Heading 4 12pt italic+underlined, Quote
  12pt italic, Bibliography 10pt plain, Plain Text 12pt plain. Every style
  uses the app's body font (Times) **except Plain Text**, which deliberately
  uses classic TextEdit's own default font instead — see "How paragraph
  styles/lists work" below for why that distinction needed adding to the
  style-matching logic itself, not just the menu. Applying a style **leaves
  the affected paragraph(s) selected** so the change is immediately visible.
  Bullet/Numbered List items continue onto the next line when you press
  Return, like Word — pressing Return again on an empty item exits the list
  instead of adding another marker. See "How paragraph styles/lists work"
  below for how this maps onto exported files.
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
  TextEdit has no real superscript), colored **blue**. **Double-click a
  marker** to reopen that same dialog, pre-filled, for editing. On export to
  `.docx`, the marker is replaced with a real OOXML `footnoteReference` with
  genuine `vertAlign="superscript"` formatting, independent of the on-screen
  approximation.
- **Comments**: Insert menu → "Insert Comment…" works the same way as
  footnotes (same dialog pattern, same double-click-to-edit), but the
  in-body marker is a small **yellow** lozenge (◊) rather than a number —
  a comment isn't "referenced" by number the way a footnote is, so there's
  nothing to print on-screen. See "How comments work" below for the marker
  glyph choice, the color coding, and what happens to comments in each
  export format — notably, `.doc` and `.rtf` behave *differently* here even
  though they're otherwise the same writer (see "Why .doc is actually RTF").
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
  `.docx`/`.rtf`/`.doc` remain write-only via Save As).
- **Import…**: best-effort import of `.rtf` files, `.doc` files that are
  actually RTF content, and genuine binary Word 97-2003 `.doc`/`.dot` files
  (plain text only for the latter) — see "Importing foreign files" and
  "Reading real binary .doc files" below.
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
  for the footnote/comment editors and the save-changes confirmation, alerts
  (`ALRT`/`DITL`) for About/error/warning, and the `SIZE` resource (2 MB
  preferred / 512 KB minimum partition — see "Document size limit" for why
  2 MB, not more). `#include`s `icon.r`.
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

- **Paragraph style** (Heading 1–4 / Quote / Bibliography / Plain Text) is
  recognized by matching a paragraph's own (size, bold, italic, underline,
  and — for Plain Text specifically — font) against a fixed table
  (`kParaStyleSpecs` in `wordproc.h`) — the same combination "Apply Style"
  itself sets. Most styles share 12pt, so face is often the only
  distinguishing signal — manually italicizing plain 12pt Times text, for
  instance, would be misidentified as Quote (Heading 2 also being bold now
  rules it out specifically). Heading 2 and Quote used to be **fully
  identical** (12pt, italic, not bold, not underlined) by explicit choice;
  that changed when Heading 2 picked up bold, so bold alone now tells them
  apart — kept as a documented example of why every field has to be
  compared, not just size, rather than as a still-current limitation.
- **Plain Text vs. Normal** is a similar case, solved the same way: both are
  12pt/plain/plain/plain, so (size, bold, italic, underline) alone can't
  distinguish them — they'd collapse into one indistinguishable style just
  like Heading 2/Quote used to. `DetectParaStyle` and `ParaStyleSpec` both
  gained a `systemFont` field for exactly this: Plain Text is classic
  TextEdit's own default font (`systemFont`, i.e. Chicago — whatever a
  brand-new, never-explicitly-styled TE record would show), Normal and
  every other style use the app's body font (Times). `ApplyParaStyle` sets
  `ts.tsFont` from that flag; the Style-menu checkmark logic
  (`AdjustMenus`) now checks `doFont` continuity too, not just face/size,
  so it can tell which one is actually selected.
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
- **Normal is never an accidental "unstyled" state** — but Plain Text is a
  deliberate one. Since paragraph style is purely derived from a paragraph's
  own (size, bold, italic, underline, font), and Normal's entry in
  `kParaStyleSpecs` *is* `{12, false, false, false, Times}`, "app-default
  Times/12pt text" and "Normal style" aren't two different things that could
  drift apart; they're the same data by construction. New documents and
  New/window creation get there by calling `ApplyParaStyle(pStyleNormal)`
  (then immediately resetting the dirty flag it sets, since a just-created
  document isn't unsaved) rather than a separate hand-written `TESetStyle`
  call that happened to produce matching numbers - so there's exactly one
  place that defines what "Normal" means, and startup can't quietly drift
  from it. Plain Text exists as the *other* thing: genuine classic TextEdit
  default formatting, available from the Style menu like any other style,
  for when Times-by-default isn't wanted for a particular paragraph -
  without being what a new document silently starts as.

  **Gotcha**: when "start maximized" was added to `CreateDocumentWindow`
  (see "The window chrome" below), `ApplyParaStyle(pStyleNormal)` was still
  being called *before* the `ZoomWindow`/`ResizeDocumentWindow` pair that
  actually resizes the window and re-lays-out TE - meaning Normal was
  established on the window at its small initial size, with a layout pass
  over a still-empty TE record happening afterward, on the exact same
  handle, with no real guarantee that pass leaves the null style alone.
  `ApplyParaStyle(pStyleNormal)` is now called *last*, after the window has
  settled at its final maximized size - style is established once, on the
  window in the state it'll actually be in when the user starts typing,
  rather than being established and then having something else run after it
  with an unclear effect on that state.

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

**A second scrollbar gotcha, found once the SetPort fix above was tested**:
the thumb worked, but the arrow/page buttons made the *entire window*
visibly flash and didn't look like they were scrolling. Holding an arrow
down repeatedly invokes `TrackControl`'s action proc (`ScrollAction`), many
times a second for as long as the mouse stays down — and that callback used
to end each tick by calling `ForceRedraw()`, which does `EraseRect` over the
*whole window* before redrawing everything. Erasing and redrawing the whole
window from scratch on every tick of a fast repeat is what was showing up
as a flash, not a working scroll. The fix: `ScrollByPixels()` no longer
calls `ForceRedraw()` at all — `TEPinScroll` already performs the on-screen
scroll itself (shifting the existing bits and drawing only the newly
revealed strip), and `UpdateScrollBarRange()`'s `SetControlValue` already
redraws just the scrollbar control, so nothing was actually gained by the
full-window erase on every tick. The `mouseDown` handler now calls
`ForceRedraw()` exactly once, after `TrackControl` returns (i.e. once per
click-and-hold or drag gesture, not once per tick) as a cheap settle pass
for anything TEPinScroll's incremental redraw might have missed, like the
grow icon.

**Zoom and the scrollbar now cooperate on scroll position.** Before this,
`RescaleDocument` left `destRect.top` at whatever raw pixel offset it was
before the zoom, even though line heights (and therefore what that pixel
offset actually points at) change with font size — reflowing to a
*smaller* size could easily leave that stale offset pointing past the end
of the now-shorter document, i.e. a blank window. `RescaleDocument` now
captures the scroll position as a *fraction* of the scrollable range
(`current offset / max scroll`) before rescaling, then re-derives the
equivalent offset at the new size after `TECalText` re-lays-out the text,
clamped into `[0, newMaxScroll]`. That clamp is what actually makes "can't
go blank" true by construction — an offset within that range can never
leave less than a viewHeight's worth of text below it — and there's a
redundant belt-and-suspenders check right beside it that forces the offset
back to 0 (top of document) if the new content height ever turns out
smaller than the computed offset, since a blank document window is a much
worse failure mode than one extra comparison.

**Text was clipping behind the bottom chrome (grow icon).** `LayoutContent`
computes the TE view rect by inset-ing the whole window's `portRect` by a
flat 4px on every side, then separately pulling the *right* edge in further
to stop clear of the vertical scrollbar (`viewRectOut->right =
scrollRectOut->left - 1`) — but it never did the equivalent adjustment for
the *bottom* edge against the grow icon, so text was free to render as far
down as `portRect.bottom - 4`, well past where the scrollbar itself already
stops 14px short (`scrollRectOut->bottom = r.bottom - 14`) to leave room for
it. `LayoutContent` now applies that same "stop clear of the reserved
chrome" adjustment to the bottom edge too
(`viewRectOut->bottom = scrollRectOut->bottom - 1`), so the text view's
bottom boundary is derived from the scrollbar's geometry instead of an
independent flat inset that happened not to agree with it.

## How .qdoc works

`.qdoc` is a small custom XML dialect, documented in full in `native.c`'s
header comment. Unlike `.docx`/`.rtf`, nothing is derived or approximated —
it stores exactly what's needed to resume editing losslessly:

- Every run's actual font/size/bold/italic/underline/color (not the
  content-derived paragraph-style/list detection docx/rtf export use) — the
  color field was added alongside comments/footnote-coloring specifically so
  those marker colors survive a save/reopen round-trip rather than quietly
  reverting to black; ordinary text just carries black (0/0/0) and round-trips
  unchanged.
- Every footnote's and comment's number, anchor offset, marker length, and
  body text (in separate `<footnotes>`/`<comments>` sections — see "How
  comments work" for why they're parallel structures, not one shared list).
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

**A sample document** (`sample/Lorem Ipsum.qdoc`) is copied onto the disk
image on every build (see the `Makefile`) so there's something to open via
File → Open without having to type a whole document first. It's
hand-written directly in the `.qdoc` XML shape described above, exercising
Normal, Heading 1/2/3, and Quote formatting so the paragraph-style detection
and menu checkmarks all have something real to react to. It's copied with
`hcopy -r` (a raw byte copy, type `????`) rather than `-m` — there's no
resource fork to preserve, and `DoOpen`'s `StandardGetFile` call uses
`numTypes = -1` (no type filtering), so the file's type/creator don't matter
for it to show up and open correctly.

**Matching `.rtf` and `.doc` samples** (`sample/Lorem Ipsum.rtf` and
`sample/Lorem Ipsum.doc`, byte-identical to each other) are copied onto the
disk image the same way - same Lorem Ipsum text and heading/quote
formatting as the `.qdoc` sample, so all three are directly comparable.
Unlike a `.docx` sample would be, these are actually usable *inside* the
app: File → Import (see below) can open both, the `.doc` one exercising the
exact "doc that's actually RTF" case `DoImport`'s content-sniffing exists
for. Generated by `tools/gen_sample_rtf.py`, which builds plain RTF text
matching `rtf.c`'s `BuildRtfBody` output shape (same font table, same
`\plain\fN\b\i\ul\fsN` run-formatting convention) rather than invoking
`rtf.c` itself (there's no way to run 68k code host-side) - kept in sync
with the *current* `kParaStyleSpecs` (Heading 1 bold, Heading 2
bold+italic, Heading 3 bold+underline), not the styles' original non-bold
spec. Verified by actually running the file through a standalone copy of
`ReadDocumentFromRtf`'s parsing logic (compiled and run on the host, since
the real one can't be) and confirming every paragraph recovers the right
text and formatting, then confirmed to round-trip byte-for-byte off a
freshly built disk image.

## Importing foreign files

File → Import… (`DoImport` in `app.c`) opens `.rtf` files, `.doc` files that
turn out to actually be RTF content (a real, common case, since this app's
own "Save As .doc" produces exactly that - see "Why .doc is actually RTF"
below), and genuine binary `.doc`/`.dot` files (Word 97-2003 / OLE2 format -
see "Reading real binary .doc files" below). The file's actual *kind* is
identified by **content, not name or extension**: the first bytes are
checked for the RTF magic (`{\rtf`) and the OLE2 magic (`D0 CF 11 E0...`),
so a `.doc` that's genuinely RTF and a `.doc` that's a real binary Word
document are each routed to the reader that actually understands them,
rather than either one getting silently mishandled. A `.docx` (ZIP magic
`PK\3\4`) is detected the same way and rejected with an explanation of why,
rather than pretending to support it (see "Known limitations"). Before
either import actually runs, a warning explains what will and won't be
recovered - this is genuinely a best-effort pair of readers, not full
implementations of either format.

`ReadDocumentFromRtf` (`src/rtf.c`) is a hand-rolled RTF tokenizer, not a
parser for the whole spec — RTF's own design makes that a reasonable
trade: an unrecognized control word is *supposed* to be silently ignored
(that's the spec's own fallback rule), so a reader doesn't need to
understand everything in order to safely skip past what it doesn't. What it
does handle:

- Plain text, `\b`/`\i`/`\ul`/`\ulnone`/`\plain`, `\fs` (size), `\par`/
  `\line` (paragraph breaks), `\tab`.
- **A real font table**: `{\fonttbl...}` is actually parsed (not skipped),
  so a file's own `\fN` indices resolve to that file's *actual* font names
  — necessary for opening real Word-authored RTF, where `\f0` might be
  Calibri, not this app's own `\f0` = Times convention. Nested destinations
  within a font's own sub-group (e.g. `{\*\panose ...}`, common in
  Word-generated font tables) are correctly skipped without losing the rest
  of that font's name.
  A font-table entry is recognized purely by `\fN` occurring while inside
  `\fonttbl` — **not** by also requiring it to sit right after a `{` (fixed
  after a real report: some real-world RTF writes font-table entries flat,
  `{\fonttbl\f0\froman\fcharset0 Times New Roman;\f1...}`, with no `{...}`
  wrapping each one, rather than Word's usual
  `{\fonttbl{\f0\froman...;}{\f1...;}}`. Requiring a fresh `{` missed `\fN`
  in the flat form entirely — it only ever follows `\fonttbl` or the
  previous entry's `;`, never a `{` — so the font *name* text that should
  have been captured into the font table instead fell through to the
  ordinary body-text path and was inserted as garbled-looking literal
  characters at the very start of the imported document, e.g.
  `Times New Roman;Arial;` ahead of the real content).
  A font name that doesn't resolve to an installed font is also handled
  explicitly now: `GetFNum` silently resolves an unmatched name to font ID
  0 (the *system* font, Chicago) rather than failing outright, and a real
  `.rtf`'s font table very often names fonts that only exist on whatever
  machine authored it ("Times New Roman", "Calibri", "Arial", none of
  which classic Mac OS ships) — but font ID 0 is *also* the exact signal
  this app's own Style menu reads as "Plain Text" rather than "Normal"
  (see wordproc.h's `ParaStyleSpec.systemFont` comment), so an ordinary
  imported paragraph would silently misidentify itself as Plain Text
  purely because its named font wasn't installed. `RtfFlush` now checks
  for that and falls back to this app's own default body font (Times)
  instead, the same fallback already used when a font table entry was
  empty to begin with. Both of these were verified with a standalone host-
  compiled harness that ran the *actual* production reader (linked
  directly, not reimplemented) against small synthetic `.rtf` fixtures
  covering both font-table styles and an unresolvable font name — checked
  against the pre-fix code first to confirm each one reproduced the
  reported symptom exactly, then against the fix to confirm it didn't,
  since this couldn't be tested live without an emulator.
- `\'hh` (literal hex-escaped byte) and `\uNNNN` (Unicode code point,
  best-effort mapped back through `kMacRomanHigh`, falling back to `?`) —
  including correctly skipping the `\ucN`-controlled fallback character(s)
  that follow a `\u` escape per spec, so they don't leak into the text as
  stray characters.
- Non-text destinations are skipped by content, not by an exhaustive
  keyword list: any `{\*...}` group is unconditionally skipped (that's
  exactly what the `\*` marker means - "ignorable if unrecognized"), plus
  an explicit list of common named destinations that don't use `\*`
  (`colortbl`, `stylesheet`, `info`, `footnote`, `pict`, `object`,
  `header`/`footer` variants, `generator`).

**Large files don't need a large contiguous allocation at all.**
`ReadDocumentFromRtf` never loads the source file into one buffer - it
scans left to right through a small, fixed-size sliding window
(`kRtfChunkSize`, 16 KB) refilled from the still-open file as the scan
consumes it (`RtfEnsureLookahead`). Peak memory for the *source* side of
import is that window plus the small growable "pending run" buffer,
regardless of whether the file is 10 KB or 10 MB - a 1.5 MB `.rtf` needs
no more contiguous heap than a 15 KB one. (This replaced an earlier
design that malloc'd the whole file up front and asked `CompactMem` to
approve it first; that worked, but meant the same file could succeed on
one launch and fail on another purely because of how fragmented the
heap happened to be at that moment - see "Import readers no longer need
the whole file in memory" under "Document size limit" for why that was
worth fixing instead of just tuning the size it asked for.) What's left
is a generous byte-count sanity ceiling (16 MB) that only exists to avoid
grinding through a pathological or corrupt file, not for any memory
reason, plus a running check against `kRtfMaxImportChars` (28,000,
matching the app's own `kSizeWarnThreshold`) as text is actually inserted
during parsing, because RTF markup overhead makes a file's *byte size* a
poor proxy for the *character count* it'll produce, so that ceiling has
to be enforced against what's actually being inserted, not guessed from
the file size. The sanity ceiling returns `kImportTooLargeErr` (defined
in `wordproc.h`, shared with `doc.c`'s reader below) with nothing
imported yet at that point (there's no partial content to keep), but
hitting the character ceiling *during* insertion is different and
handled differently - see "Document size limit"'s "Import no longer
discards a document just for being too long" for why that one keeps what
was already inserted instead of throwing it away.

What it deliberately does **not** attempt: paragraph alignment, lists/
tables, embedded pictures/objects, footnotes, and comments (the `\footnote`
and `\*\annotation` groups are both skipped rather than reconstructed as
real Quill footnotes/comments — recovering the anchor position correctly
would need more bookkeeping than this pass covers). Imported content is
treated as a new, unsaved document — `File → Save` offers Save As `.qdoc`,
it does not silently overwrite the original `.rtf`/`.doc` file.

## Reading real binary .doc files

`ReadDocumentFromDoc` (`src/doc.c`) reads genuine Word 97-2003 binary `.doc`
(and `.dot` template) files - the actual OLE2-based format, not RTF wearing
a `.doc` extension. **Plain text only**: no bold/italic/underline/font, no
paragraph styles, no footnotes/comments/fields/tables/pictures. Recovering
formatting would mean parsing Word's own CHPX/PAPX property structures
(run-length-encoded lists of "sprm" opcodes) on top of everything already
needed just to reach the text - a substantial second undertaking,
deliberately out of scope for this pass.

Two independent formats are involved, and both had to be implemented from
scratch:

- **OLE2 / Compound File Binary Format**, the container. It's a small
  filesystem embedded in one file - 512-byte sectors, a FAT (File
  Allocation Table - the same "linked list of blocks" idea as a real disk
  FAT) locating a directory stream, and the directory stream listing named
  streams (`WordDocument`, and `0Table` or `1Table` depending on a flag in
  the FIB) each with their own starting sector and size. Streams below a
  cutoff size (always 4096 bytes) live in a separate "mini stream" with its
  own smaller-sector FAT - deliberately **not supported**: a real
  document's `WordDocument`/table streams are essentially always well
  above that cutoff, and implementing mini-stream random access would
  roughly double this reader's size for a case that's never actually hit in
  practice. Directory entries form a red-black tree (sibling + child
  pointers) rather than a flat list; walked as a plain binary tree since
  only lookup-by-name is needed, not the balancing semantics.
- **Word's own binary layout** within the `WordDocument` stream: a FIB
  (File Information Block) header, and - reached through it - a piece
  table (an array of "pieces", each a contiguous run of text stored either
  as 8-bit "compressed" Windows-1252 or 16-bit Unicode UTF-16LE, at some
  byte offset elsewhere in the same stream). Paragraph marks are literal
  `\r` characters *within* the piece text, the same convention this app's
  own TE buffers already use - plain-text extraction doesn't need to do
  anything special for paragraph breaks. Field codes (mail-merge-style,
  wrapped in `0x13`/`0x14`/`0x15` marker bytes) and footnote/endnote
  reference marks are passed through as literal characters rather than
  specially interpreted, for the same "best effort, text only" reasoning
  as RTF import's handling of unrecognized control words.

**Verified against a real, complex, real-world `.dot` file** (a multi-page
document with footnotes-adjacent formatting, mixed compressed/Unicode
pieces, and several non-ASCII characters) using the same method as the RTF
reader's font-table logic: prototyped and checked against the file's actual
bytes in Python first (confirming the single most error-prone number in
this whole reader - which index into the FIB's `fibRgFcLcb97` array holds
the piece table's location - against real data rather than trusting it from
memory), then the ported C was itself compiled standalone and re-run
against the same file as a second, independent check. That second pass
caught a real bug introduced during the port (an uninitialized-variable
mix-up between two similarly-named capacity counters in the FAT-building
code) before it ever reached the actual application.

Like the RTF reader, the source file is never loaded into one buffer:
every OLE2 structure (FAT sectors, the directory stream, the table
stream, each text piece) is fetched directly from the still-open file
with `SetFPos`+`FSRead`, sized to just what that structure needs -
`Ole2ReadRange` is the one choke point everything else goes through, and
the only thing genuinely proportional to the file's size afterward is the
FAT link array itself (a handful of bytes per sector - a few KB even for
a multi-megabyte document). What's left is a generous byte-count sanity
ceiling (16 MB) that exists only to avoid grinding through a pathological
or corrupt file, not for any memory reason, and text insertion is capped
against the same practical TextEdit character ceiling
(`kDocMaxImportChars`, also 28,000) - a fundamentally different kind of
limit than a memory one, since it would apply even given infinite RAM.
The sanity ceiling returns `kImportTooLargeErr` with nothing imported yet
(nothing to keep), but crossing the character ceiling *during* extraction
keeps everything inserted before that point intact and lets `DoImport`
treat it as a truncated-but-real partial import with a warning, rather
than discarding it - see "Document size limit" for the reasoning.

## How comments work

Comments work like footnotes (Insert menu, a modal dialog, double-click a
marker to reopen it for editing) but are a genuinely separate feature under
the hood — `DoInsertComment`/`EditComment`/`FindCommentContainingOffset` in
`app.c`, a parallel `comments[]`/`commentCount` on `Document` alongside the
existing `footnotes[]`/`footnoteCount` (both reuse the same `Footnote`
struct shape - see wordproc.h - since a comment is structurally identical:
an anchor offset, a marker length, and an out-of-line text blob).

**The marker.** Classic TextEdit can only draw glyphs from a font — there's
no way to embed an actual bitmap icon inline in the text flow — so "a little
comment icon" is, concretely, a single fixed character: the lozenge (◊,
Mac OS Roman `0xD7`), chosen because nothing else in this app already uses
it (bullets use a different byte, `kBulletMarkerByte`). Footnotes keep
their existing on-screen behavior (the actual number, shrunk to ~65%) — a
footnote *is* referenced by number, a comment isn't, so there was nothing
to change there except color. Both marker kinds are colored via
`TESetStyle`'s `doColor` mode at insertion time: footnotes pure blue,
comments pure yellow. This is unconditional, not a "detect a color Mac" branch —
QuickDraw quietly collapses any RGB color to black on a B&W port on its
own, so a single code path handles both correctly.

**Export.** `.docx` gets real Word comments: `word/comments.xml`, a
`CommentReference` character style, and — spliced into `document.xml` in
place of the marker character, the same splice-out-and-replace trick
footnotes and list markers already use — a zero-width
`commentRangeStart`/`commentRangeEnd` pair plus a `commentReference` run.
Word shows these as normal anchored comment balloons. `.qdoc` round-trips
comments losslessly, same as footnotes, via a `<comments>`/`<cm>` section
mirroring `<footnotes>`/`<fn>` (see "How .qdoc works").

**`.doc` and `.rtf` behave differently here**, despite being the exact same
underlying writer (`WriteDocumentAsRtf` in `rtf.c`) - see "Why .doc is
actually RTF" for why that's normally a non-issue. Real RTF has a genuine,
if legacy, comment mechanism: `\chatn` (an auto-numbered annotation
reference mark) paired with `{\*\atnid}{\*\atnauthor}{\*\annotation}`
destinations, all conventionally `\*`-prefixed (ignorable-if-unrecognized)
the way real-world RTF - including Word's own output - writes them, rather
than requiring every reader to specifically understand `\annotation`.
`WriteDocumentAsRtf` takes an `includeComments` flag so callers can choose:
"Save As .doc" passes `true` (comments survive, since a `.doc` file is
expected to carry them), "Save As .rtf" passes `false` (comments are
dropped) - **and warns first** if the document has any, so losing them on
plain RTF export is a visible, deliberate tradeoff, not a silent one. Either
way the marker character itself is always spliced out of the body text, so
a stray, meaningless lozenge glyph never leaks into exported RTF either.

**Reading them back**: only `.qdoc` can. RTF Import treats `\*`-prefixed
groups (which includes `\*\annotation`) as an unconditionally skippable
destination - correct in the sense that it won't corrupt anything, but it
means importing a `.doc` this app itself wrote loses that document's
comments on the way back in, the same way footnotes are already dropped on
import (see "Importing foreign files").

## Document size limit

Classic TextEdit tracks every offset it deals with — `selStart`, `selEnd`,
`teLength`, the line-start table, each style run's start character — as a
16-bit signed `INTEGER` (confirmed directly in the Toolbox headers this
toolchain ships). That makes **~32,767 characters (roughly 5,000–5,500
words, ~20 double-spaced pages) a hard ceiling**, not a tunable one — it's
an *addressing* limit, not a capacity one, so it holds regardless of how
much heap the app is given. Approaching the real limit degrades before it
outright crashes (garbled selection, wrong line breaks), so the safe
ceiling is somewhat below 32,767, not exactly at it.

Quill warns once (`CheckDocumentSize` in `app.c`, checked after typing,
pasting, or opening a file) when a document crosses **28,000 characters**
— about a 10% margin — via a caution alert, then stays quiet unless the
document is trimmed back under the threshold and crosses it again.

This can't be fixed by "buffering" in the virtual-memory sense — more RAM
doesn't help a 16-bit offset point past 32,767. A real fix means retiring
single-TE-record TextEdit for large documents in favor of a custom
paginated/multi-record engine (each chunk its own TE record, swapped
in/out as the user scrolls) — the same scale of rewrite as true
per-paragraph alignment, and, like that, deliberately out of scope for
this pass.

**The app's heap (`SIZE` resource: 512 KB min / 2 MB pref) is a separate
thing from this 32,767-character ceiling.** 2 MB preferred (rather than
requesting more) is a deliberate choice: on a 4 MB-RAM Mac, the System
itself needs a meaningful share of that memory, so requesting a partition
close to the machine's *total* RAM would be far less likely to actually
be granted at launch than a more realistic 2 MB ask - a preference the
Process Manager can't satisfy still lets the app launch with less, down
to the 512 KB minimum. Import used to be the main thing a bigger heap
helped with, back when both readers malloc'd the whole source file into
one block up front; now that they stream instead (see "Import readers no
longer need the whole file in memory" below), the heap's size no longer
gates how large an importable file can be - what it still budgets for is
everything else running at once (the current document's own TE record
and style table, Toolbox structures, the small fixed-size buffers the
readers do still use).

**Gotcha that actually crashed the app on launch, found the hard way:**
the `'SIZE'` resource's last two fields are a preferred size and a minimum
size, in that order - confirmed directly from the Rez template this
toolchain compiles against (`multiversal/RIncludes/Multiverse.r`:
`unsigned longint; // preferred` immediately followed by
`unsigned longint; // minimum`). `src/main.r` had them **backwards**
(`512 * 1024, 2 * 1024 * 1024` - smaller value first) essentially since
this resource was first written, which silently told the Process Manager
"prefer 512 KB, but require at least 2 MB to launch at all" - the exact
opposite of the intended "512 KB minimum, 2 MB preferred." With a small
gap between the two numbers (the project's very first values were 512 KB
and 1 MB) this went unnoticed for a long time - whatever was actually
available at launch was still probably at least 1 MB, so the app
launched fine regardless of which field meant what. It only became fatal
once the *preferred* figure was deliberately raised to 2 MB for larger
Import allocations (see above): with the fields swapped, that 2 MB became
the (backwards) *minimum*, and a machine without 2 MB free right at
launch - before the app's own code, including anything to do with
Import, ever got to run - would fail to start at all. Fixed by swapping
the order to `2 * 1024 * 1024, 512 * 1024` with an explicit `/* preferred
*/` / `/* minimum */` comment on each line, specifically so the order is
never ambiguous again at a glance.

**A second, unrelated launch crash (System error type 28 - stack
overflow), found right after fixing the one above:** an experimental
`TryMaximizeHeap()` had been added, called as the very first thing in
`RunApp()`, that called `SetApplLimit((Ptr)someByteCount)` trying to ask
for more memory at runtime. That's a fundamental misreading of what
`SetApplLimit` does: it takes an *absolute memory address* marking where
the heap zone should stop - normally computed as an offset from the zone's
actual base or current limit, used to carve out guaranteed stack headroom
*within* an already-granted partition - not a byte count to request more
of. Passing a raw size like `4L * 1024L * 1024L` cast to a `Ptr` sets the
heap ceiling to literal address `0x400000`, which has no relationship to
where this app's partition actually sits in memory; depending on layout,
that can shrink the heap zone to something nonsensical or corrupt the
heap/stack boundary outright, and with it called before any other startup
code, the failure surfaces as an immediate crash on launch with no chance
for the app's own code to run first. There is no API for growing a
partition after launch anyway - the `'SIZE'` resource fixed above is the
*only* lever for that, and it's already correct - so the fix was simply
deleting `TryMaximizeHeap()` entirely rather than trying to repair it.

While tracking this down, `Document`'s `footnotes`/`comments` were also
changed from fixed 200-entry arrays (`Footnote footnotes[200]`) to
`malloc`'d pointers, allocated once in `CreateDocumentWindow`. Two
200-entry arrays add real weight to the app's global data - and since
`Document` is a global (`gDoc`), that competes directly with stack space
in the same partition, same underlying concern as the heap-limit bug
above, just via ordinary global storage instead of a botched API call.
Most documents will never need anywhere near either array's full size, so
reserving all of it as permanent global storage was already wasteful
before this crash made it worth revisiting.

**Import readers no longer need the whole file in memory, either.** Both
`.rtf` and `.doc` import originally malloc'd the entire source file into
one buffer up front, gated by asking `CompactMem` whether the current
heap actually had a contiguous block that size free before committing to
it. That was a real improvement over an unchecked malloc, but it was
still fundamentally a bet: a 1 MB `.doc` could fail with "not enough
memory" even on a launch that got the full 2 MB preferred partition,
because *contiguous* free space is a function of everything else that's
happened to the heap since launch (the current document's own TE record
and style runs, Toolbox allocations, prior edits), not just the
partition's total size - the same file could succeed right after
launching and fail an hour into an editing session. Since neither reader
actually needs random access to the *whole* file at once - RTF is parsed
strictly left to right, and even OLE2's random-access container format
only ever needs one sector, one stream, or one text piece at a time, not
everything simultaneously - the fix was to stop needing the big
allocation at all rather than to keep tuning how it's approved.
`ReadDocumentFromRtf` now scans through a small fixed-size sliding window
refilled from the still-open file as it goes (`RtfEnsureLookahead` in
`src/rtf.c`); `ReadDocumentFromDoc` now fetches every OLE2 structure
directly from the still-open file with `SetFPos`+`FSRead`
(`Ole2ReadRange` in `src/doc.c`, the single choke point everything else
funnels through). Both were checked with host-compiled differential
tests before this landed: `RtfEnsureLookahead`'s window-refill logic was
verified byte-for-byte lossless against plain sequential file reads,
including under deliberately adversarial (near-degenerate) window sizes
designed to force a refill at every possible boundary position; and the
new file-seeking `Ole2ReadRange` was checked against the original
buffer-based version across hundreds of randomized ranges over a
synthetic, deliberately scrambled (non-contiguous) ~1.5 MB sector chain -
the case most likely to expose an off-by-one in the sector-walking logic.
What's left of the old checks is a generous byte-count sanity ceiling
(16 MB for both readers) that exists only to avoid grinding through a
pathological or corrupt file, not for any memory reason - see "Large
files don't need a large contiguous allocation at all" above and its
counterpart in "Reading real binary .doc files" for specifics.

**Import no longer discards a document just for being too long to import
in full.** Both `ReadDocumentFromRtf` and `ReadDocumentFromDoc` insert text
piece by piece, checking the running character count against their
ceiling as they go; the moment inserting the *next* piece would cross it,
they simply stop - keeping everything inserted so far intact rather than
rolling it back - and return `kImportTooLargeErr`. `DoImport` treats that
specific error as a **partial success**, not a failure: it skips the
usual discard-and-fail path entirely, finishes the same bookkeeping a
clean import would (title, dirty flag, zoom, redraw), and shows a warning
explaining that the document was truncated at the character limit rather
than a `Fail` alert that would otherwise imply nothing was imported at
all. Any *other* error (a genuinely malformed file, or memory exhausted
before any text was even reached) still discards and fails outright, since
there's no trustworthy partial content to keep in those cases.

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

One deliberate exception to ".doc and .rtf are the same output": comments.
`WriteDocumentAsRtf` takes an `includeComments` flag, `true` for `.doc` and
`false` (with a warning shown first) for `.rtf` — a `.doc` file is expected
to be able to carry Word-style comments, a plain `.rtf` export is treated
as the lighter-weight "just the formatted text" option. See "How comments
work" above for the actual RTF syntax involved.

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

**Why Finder wasn't showing the icon at all (found and fixed):** it turned
out to have nothing to do with the icon data itself, or with Finder's
Desktop Database caching an old association - the copied file was missing
the Finder "has bundle" flag (`0x2000`) entirely, on every build, so Finder
had no reason to ever look at its `BNDL`/`ICN#`/`icl4` resources in the
first place. Two things combine to cause this:

1. Retro68's `Rez` tool writes `Quill.bin` in MacBinary format, and its
   writer (`ResourceFiles/ResourceFile.cc`) hard-codes the header's
   Finder-flags byte to `0` unconditionally - it has no logic to notice a
   `BNDL` resource exists and turn the bundle bit on for it.
2. `hcopy -m` (used to put the app onto the `.img` floppy image) carries
   that byte straight from the MacBinary header into the copied file's HFS
   catalog entry, unmodified (`hfsutils/copyin.c`'s `cpi_macb`).

So every build produced a file whose icon resources were all correct, but
whose Finder Info said "no bundle here" - which reads to Finder exactly
like an app with no custom icon at all, every single time, not an
occasionally-stale cache.

The fix (`tools/set_bundle_bit.py`, run from the `Makefile` right after
`Quill.bin` is generated and before `hcopy -m`) patches that bit directly
into the MacBinary header before the file is copied onto the disk image.
The header also carries a CRC-16 checksum that `hcopy -m` verifies before
accepting the file, so the script recomputes that too using the same
lookup table Rez's writer and `hcopy`'s checker both use (transcribed
directly from `ResourceFile.cc`, not reconstructed from the general
algorithm, so it's guaranteed to agree with both sides bit-for-bit). Verified
by round-tripping the file back out of a freshly built disk image
(`hcopy -m :Quill out.bin`) and confirming its Finder flags read back as
`0x2000`, not just checking the pre-copy `.bin`.

If, after all that, Finder *still* shows a stale icon for a file it already
saw under the old (bundle-less) state, that would be the Desktop Database
caching its earlier "no custom icon" verdict - Command+Option at Finder
startup (or disk mount) and confirming "rebuild the desktop file" clears
that. But for a disk image built fresh with this fix, that shouldn't be
necessary.

## Known limitations

- **Open only reads `.qdoc`**; `.docx`/`.rtf`/`.doc` remain write-only via
  Save As. **Import (File → Import…) reads `.rtf`, `.doc` files that are
  actually RTF content** (as this app's own "Save As .doc" produces, and as
  many real-world `.doc` files turn out to be), **and genuine binary Word
  97-2003 `.doc`/`.dot` files** (plain text only — no formatting) — see
  "Importing foreign files" and "Reading real binary .doc files" below for
  exactly what is/isn't recovered. `.docx` is still not handled: real
  `.docx` needs a from-scratch DEFLATE decompressor to be useful at all,
  since Word always compresses its ZIP entries — a large, error-prone
  undertaking deliberately deferred (see "DOCX import scope" discussion -
  this was a deliberate choice, not an oversight, given real binary `.doc`
  turned out to be tractable enough to implement instead).
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
- **Footnote and comment text are both capped at 255 characters**
  (`GetDialogItemText`/`RunSimpleTextDialog` return a Pascal string), and
  footnotes are **numbered by insertion order**, not reflowed if you delete
  one (comments aren't numbered on-screen at all — see "How comments work").
- **Comments aren't reconstructed by Import**, same as footnotes — RTF's
  `\*\annotation` destination is recognized and safely skipped, not
  round-tripped back into a real Quill comment.
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
