#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <StandardFile.h>
#include <Memory.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app.h"
#include "wordproc.h"
#include "docx.h"
#include "rtf.h"
#include "doc.h"
#include "native.h"

enum {
    mApple = 128, mFile = 129, mEdit = 130, mFormat = 131, mInsert = 132,
    mStyle = 133, mAlign = 134, mZoom = 135
};

enum { iAbout = 1 };

enum {
    iNew = 1, iOpen = 2, iImport = 3, iSave = 4,
    iSaveAsQuill = 5, iSaveAsDocx = 6, iSaveAsRtf = 7, iSaveAsDoc = 8,
    iQuit = 10
};

enum { iUndo = 1, iCut = 3, iCopy = 4, iPaste = 5, iClear = 6 };

enum {
    iBold = 1, iItalic = 2, iUnderline = 3,
    iFontFirst = 5, iFontLast = 11,
    iSizeFirst = 13, iSizeLast = 18
};

enum { iInsertFootnote = 1, iInsertComment = 2 };

/* Style menu: items 1..kParaStyleCount map directly to ParaStyleKind 0..N-1;
   item kParaStyleCount+1 is a divider. Derived from kParaStyleCount rather
   than hardcoded so adding/removing a style can't silently desync these
   from the actual menu layout in main.r. */
enum { iBulletList = kParaStyleCount + 2, iNumberedList = kParaStyleCount + 3 };

/* item 1 is the permanently-disabled "Whole Document:" label - see SetupMenus */
enum { iAlignLeft = 2, iAlignCenter = 3, iAlignRight = 4, iAlignJustify = 5 };

#define kFootnoteDialogID 200
#define kAboutAlertID     201
#define kErrorAlertID     202
#define kWarnAlertID      203
#define kConfirmDialogID  204
#define kCommentDialogID  205
#define kDialogTextItem   4 /* the OK/Cancel/label/EditText layout is shared by the footnote and comment dialogs */
#define kDefaultSize      12
#define kDoubleClickSlop  5
#define kDoubleClickTicks 30 /* ~0.5s at 60 ticks/sec; GetDblTime() isn't available for this target */
#define kMaxTouchedParagraphs 200
#define kEnterKey         0x03 /* numeric-keypad Enter; treated the same as Return */
#define kScrollBarWidth   16
#define kZoomDocProc      8  /* zoomDocProc - a Rez-only named constant in this toolchain's headers, not exposed to C */

/* Classic TextEdit tracks every offset (selStart/selEnd/teLength/lineStarts/
   StyleRun.startChar) as a 16-bit signed INTEGER, so ~32,767 characters is a
   hard, unraisable ceiling - not a heap/SIZE-resource limit. This warns well
   before that (about a 10% margin) rather than letting things degrade
   silently near the real wall. */
#define kSizeWarnThreshold 28000

static const short kSizes[] = { 9, 10, 12, 14, 18, 24 };
#define kSizeCount 6

static const short kZoomLevels[] = { 50, 100, 150, 200, 300, 400 };
#define kZoomCount 6

static Document gDoc;
static Boolean gDone = false;
static MenuHandle gAppleMenu, gFileMenu, gEditMenu, gFormatMenu, gInsertMenu, gStyleMenu, gAlignMenu, gZoomMenu;
static ControlHandle gVScrollBar;
static short gBodyFontID;
static short gZoomPercent = 100;
static unsigned long gLastClickTime = 0;
static Point gLastClickPt = { 0, 0 };
static Boolean gSizeWarningShown = false;

static void ToolboxInit(void);
static void TryMaximizeHeap(void);
static void SetupMenus(void);
static void CreateDocumentWindow(void);
static void LayoutContent(Rect *viewRectOut, Rect *scrollRectOut);
static void ResizeDocumentWindow(void);
static void ForceRedraw(void);
static void UpdateScrollBarRange(void);
static void ScrollByPixels(short delta);
static pascal void ScrollAction(ControlHandle control, short part);
static void HandleMenuChoice(long menuChoice);
static void HandleFileMenu(short item);
static void HandleEditMenu(short item);
static void HandleFormatMenu(short item);
static void HandleInsertMenu(short item);
static void HandleStyleMenu(short item);
static void HandleAlignMenu(short item);
static void HandleZoomMenu(short item);
static void AdjustMenus(void);
static void DoNew(void);
static void DoOpen(void);
static void DoImport(void);
static Boolean DoSaveAs(SaveFormat fmt);
static void DoSave(void);
static Boolean SaveBeforeDiscard(void);
static Boolean ConfirmDiscardChanges(void);
static void DoInsertFootnote(void);
static void ClearFootnotes(void);
static void ClearComments(void);
static void ToggleFace(Style bit);
static void SetFontByName(const char *name);
static void SetSize(short size);
static void Fail(const char *msg);
static void Warn(const char *msg);
static void CheckDocumentSize(void);

static short ScaleSize(short logicalSize);
static short UnscaleSize(short actualSize);
static void RescaleDocument(short newZoomPercent);

static long TextLength(void);
static void GetParagraphRange(long offset, long *pStart, long *pEnd);
static void GetSelectionParagraphRange(long *outStart, long *outEnd);
static short CollectTouchedParagraphs(long selStart, long selEnd, long *starts, long *ends);
static void ApplyParaStyle(ParaStyleKind kind);
static void ToggleList(ListKind kind);
static void AdjustMarkersAfterEdit(long editPos, long delta);
static Footnote *FindFootnoteContainingOffset(long offset);
static Footnote *FindCommentContainingOffset(long offset);
static Boolean RunSimpleTextDialog(short dialogID, Str255 text);
static void EditFootnote(Footnote *fn);
static void EditComment(Footnote *cm);
static void DoInsertComment(void);
static void InsertPlainReturn(void);
static void HandleReturnKey(void);

static void ToolboxInit(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

/* Best-effort attempt to grow the application partition at startup.
   Strategy:
   - Prefer to set the application's limit to 4 MiB using SetApplLimit.
   - If that fails or the actual usable heap is still small, probe by
     attempting progressively smaller allocations (NewPtr) to discover
     a large allocatable block. If a probe of size S succeeds, attempt
     to set the application limit to 2*S (approximate making that block
     represent ~50%% of the partition).

   This is intentionally conservative and best-effort: the Memory Manager
   / OS (or the emulator) ultimately decides how much to grant. */
static void TryMaximizeHeap(void)
{
    const long kMaxRequested = 4L * 1024L * 1024L; /* 4 MB */
    const long kMinProbe = 4 * 1024; /* 4 KB smallest probe */
    OSErr err;

    /* First, ask the system for the full 4MB partition. If the system can
       satisfy it, great. On many hosts/emulators this may fail if the
       guest has less physical RAM configured. */
    err = SetApplLimit(kMaxRequested);
    (void)err; /* ignore result - we'll probe below to see what actually works */

    /* Probe: try to allocate a single contiguous block of up to half of
       the requested max (i.e., 2 MB). If that fails, halve and retry until
       a small threshold. */
    long probe = kMaxRequested / 2;
    while (probe >= kMinProbe) {
        Ptr p = NewPtr((Size)probe);
        if (p != NULL) {
            /* allocation succeeded: free it and try to set appl limit to
               twice the probe size (so the probe represents ~50% of the
               requested partition). If SetApplLimit fails, ignore it - at
               least the large allocation succeeded which means usable heap
               is already large enough for that probe. */
            DisposPtr(p);
            long desiredLimit = probe * 2;
            if (desiredLimit > kMaxRequested)
                desiredLimit = kMaxRequested;
            err = SetApplLimit(desiredLimit);
            (void)err;
            return;
        }
        probe /= 2; /* try a smaller probe */
    }

    /* If we get here, every probe failed - can't allocate even a small
       multi-kilobyte block beyond current usage. Give up silently; the
       app will run with its existing partition (SIZE resource still in
       effect). */
}

static void SetupMenus(void)
{
    gAppleMenu = GetMenu(mApple);
    InsertMenu(gAppleMenu, 0);
    gFileMenu = GetMenu(mFile);
    InsertMenu(gFileMenu, 0);
    gEditMenu = GetMenu(mEdit);
    InsertMenu(gEditMenu, 0);
    gFormatMenu = GetMenu(mFormat);
    InsertMenu(gFormatMenu, 0);
    gStyleMenu = GetMenu(mStyle);
    InsertMenu(gStyleMenu, 0);
    gAlignMenu = GetMenu(mAlign);
    InsertMenu(gAlignMenu, 0);
    gZoomMenu = GetMenu(mZoom);
    InsertMenu(gZoomMenu, 0);
    gInsertMenu = GetMenu(mInsert);
    InsertMenu(gInsertMenu, 0);
    DisableItem(gEditMenu, iUndo);
    DisableItem(gAlignMenu, 1); /* "Whole Document:" label, not a command */
    DrawMenuBar();
}

/* Splits the window's content area into the TE view (left/top/bottom
   inset 4px, right edge stopping short of the scrollbar) and the vertical
   scrollbar's own rect (a kScrollBarWidth strip down the right edge,
   overlapping the window frame by 1px on each side per classic Mac
   convention, stopping 14px short of the bottom to leave room for the
   grow icon). Shared by window creation and every resize/zoom. */
static void LayoutContent(Rect *viewRectOut, Rect *scrollRectOut)
{
    Rect r = gDoc.window->portRect;

    scrollRectOut->top = r.top - 1;
    scrollRectOut->left = r.right - kScrollBarWidth;
    scrollRectOut->bottom = r.bottom - 14;
    scrollRectOut->right = r.right + 1;

    *viewRectOut = r;
    InsetRect(viewRectOut, 4, 4);
    viewRectOut->right = scrollRectOut->left - 1;
    /* Same reasoning as the right edge above, just for the bottom: without
       this, InsetRect's flat 4px leaves the text view extending to
       r.bottom - 4, well past where scrollRectOut already stops (r.bottom
       - 14) to leave room for the grow icon - so text could render behind
       it instead of stopping clear of that reserved strip. */
    viewRectOut->bottom = scrollRectOut->bottom - 1;
}

static void CreateDocumentWindow(void)
{
    Rect windBounds, viewRect, scrollRect;
    Str255 pname;

    SetRect(&windBounds, 60, 60, 500, 380);
    gDoc.window = NewWindow(NULL, &windBounds, "\pUntitled", true,
                             kZoomDocProc, (WindowPtr)-1L, true, 0);
    SetPort(gDoc.window);

    LayoutContent(&viewRect, &scrollRect);

    gDoc.body = TEStyleNew(&viewRect, &viewRect);
    TEAutoView(true, gDoc.body);

    gVScrollBar = NewControl(gDoc.window, &scrollRect, "\p", true, 0, 0, 0, scrollBarProc, 0);

    CToPascal(kFontNames[0], pname); /* Times, per product requirements */
    GetFNum(pname, &gBodyFontID);

    gDoc.haveFile = false;
    gDoc.format = kFormatQuill;
    gDoc.footnoteCount = 0;

    /* Start maximized: zoom to the Window Manager's standard (full-screen)
       state, then re-lay-out TE/the scrollbar for the new size, same as a
       user clicking the zoom box would trigger. Deliberately done *before*
       establishing Normal below, not after: ResizeDocumentWindow replaces
       destRect/viewRect outright and calls TECalText on what is, at this
       point, a still-empty TE record - style should be established once,
       last, on the window in its final settled state, rather than possibly
       having a layout/recalc pass run between "apply Normal" and "user can
       start typing" that there's no real guarantee leaves it alone. */
    ZoomWindow(gDoc.window, inZoomOut, true);
    ResizeDocumentWindow();

    /* Establish Normal (Times/12pt/plain) as the document's actual style,
       through the same ApplyParaStyle path a user's "Normal" menu choice
       uses - not a separately hand-set TESetStyle call that happens to
       produce the same numbers. A brand-new document is never "unstyled
       plain text" as a distinct third thing from Normal; it's Normal from
       the first character, by construction. ApplyParaStyle marks the
       document dirty, which is wrong for a just-created window, so that's
       reset right after. */
    ApplyParaStyle(pStyleNormal);
    gDoc.dirty = false;
}

/* Used after a grow-box drag resize or a zoom-box click: re-lays-out the TE
   view and the scrollbar for the window's current size. */
static void ResizeDocumentWindow(void)
{
    Rect viewRect, scrollRect;

    LayoutContent(&viewRect, &scrollRect);

    (**gDoc.body).destRect = viewRect;
    (**gDoc.body).viewRect = viewRect;
    TECalText(gDoc.body);

    MoveControl(gVScrollBar, scrollRect.left, scrollRect.top);
    SizeControl(gVScrollBar, scrollRect.right - scrollRect.left, scrollRect.bottom - scrollRect.top);

    UpdateScrollBarRange();
    ForceRedraw();
}

/* Synchronously repaints the document window right now, instead of just
   invalidating and waiting for the next update event - used after actions
   (zoom, alignment, New, resize) whose whole point is visible-immediately
   feedback, since InvalRect only *schedules* a redraw for whenever the
   event loop next gets around to it. */
static void ForceRedraw(void)
{
    SetPort(gDoc.window);
    EraseRect(&gDoc.window->portRect);
    TEUpdate(&gDoc.window->portRect, gDoc.body);
    DrawControls(gDoc.window);
    DrawGrowIcon(gDoc.window);
}

/* Keeps the scrollbar's min/max/value in sync with the document's actual
   content height and current scroll position. TE implements scrolling by
   shifting destRect relative to a fixed viewRect, so the current scroll
   offset in pixels is simply how far destRect.top has moved above
   viewRect.top; TEGetHeight gives the total content height to compare
   against the visible height. Called after every edit that could change
   line count/wrapping (typing, paste, cut, style/size changes, zoom, etc). */
static void UpdateScrollBarRange(void)
{
    short viewHeight;

    SetPort(gDoc.window);

    viewHeight = (**gDoc.body).viewRect.bottom - (**gDoc.body).viewRect.top;
    long totalHeight = TEGetHeight((**gDoc.body).nLines, 0, gDoc.body);
    short maxScroll = (short)((totalHeight > viewHeight) ? (totalHeight - viewHeight) : 0);
    short curOffset = (short)((**gDoc.body).viewRect.top - (**gDoc.body).destRect.top);

    if (curOffset > maxScroll) curOffset = maxScroll;
    if (curOffset < 0) curOffset = 0;

    SetControlMinimum(gVScrollBar, 0);
    SetControlMaximum(gVScrollBar, maxScroll);
    SetControlValue(gVScrollBar, curOffset);
}

/* delta > 0 scrolls down (reveals later content); TEPinScroll clamps to the
   valid range on its own, so callers don't need to.

   Deliberately does NOT call ForceRedraw(): TEPinScroll already performs
   the on-screen scroll itself (shifting the existing bits and drawing only
   the newly revealed strip), unlike ForceRedraw's EraseRect-then-redraw of
   the *entire* window. That distinction matters here because ScrollAction
   below calls this once per tick for as long as an arrow/page button is
   held - erasing and redrawing the whole window on every tick, many times
   a second, is what was producing a visible whole-window flash without
   actually looking like it was scrolling. UpdateScrollBarRange's
   SetControlValue/SetControlMaximum calls already redraw just the
   scrollbar control on their own, which is enough - the caller does one
   ForceRedraw() after the gesture (click or drag) completes, not per-tick,
   to settle anything TEPinScroll's incremental redraw might have missed
   (e.g. the grow icon). */
static void ScrollByPixels(short delta)
{
    if (delta == 0)
        return;
    SetPort(gDoc.window);
    TEPinScroll(0, -delta, gDoc.body);
    UpdateScrollBarRange();
}

static pascal void ScrollAction(ControlHandle control, short part)
{
    short lineHeight = (**gDoc.body).lineHeight;
    short viewHeight = (**gDoc.body).viewRect.bottom - (**gDoc.body).viewRect.top;
    short delta = 0;

    /* ScrollByPixels'/TEPinScroll's delta>0-scrolls-down convention is
       confirmed correct by thumb-dragging (newValue - oldValue works
       right there) - but empirically, wiring inUpButton/inPageUp to
       negative deltas here scrolled *down*, and inDownButton/inPageDown
       scrolled *up*: the opposite of what those part codes should mean.
       Signs below are swapped from the "obvious" mapping to match what
       actually happens on screen. */
    (void)control;
    switch (part) {
        case inUpButton:   delta = lineHeight; break;
        case inDownButton: delta = (short)-lineHeight; break;
        case inPageUp:     delta = (short)(viewHeight - lineHeight); break;
        case inPageDown:   delta = (short)-(viewHeight - lineHeight); break;
        default: return;
    }
    ScrollByPixels(delta);
}

static void ClearFootnotes(void)
{
    short i;
    for (i = 0; i < gDoc.footnoteCount; i++) {
        if (gDoc.footnotes[i].text)
            DisposeHandle(gDoc.footnotes[i].text);
    }
    gDoc.footnoteCount = 0;
}

static void ClearComments(void)
{
    short i;
    for (i = 0; i < gDoc.commentCount; i++) {
        if (gDoc.comments[i].text)
            DisposeHandle(gDoc.comments[i].text);
    }
    gDoc.commentCount = 0;
}

static void DoNew(void)
{
    TESetSelect(0, 32767, gDoc.body);
    TEDelete(gDoc.body);
    ClearFootnotes();
    ClearComments();
    gDoc.haveFile = false;
    gDoc.format = kFormatQuill;
    gZoomPercent = 100;
    gSizeWarningShown = false;
    TESetAlignment(teJustLeft, gDoc.body);

    /* Same reasoning as CreateDocumentWindow: route through ApplyParaStyle
       so Normal is established via the one canonical definition, not a
       separately hand-set TESetStyle call. */
    ApplyParaStyle(pStyleNormal);
    gDoc.dirty = false;

    TECalText(gDoc.body);
    UpdateScrollBarRange();
    ForceRedraw();
    SetWTitle(gDoc.window, "\pUntitled");
}

/* .docx/.rtf/.doc always normalize to 100% first so the exported file holds
   true logical sizes regardless of current on-screen zoom (see "How zoom
   works" in the README). .qdoc is the exception - its whole point is
   faithfully resuming a session, so it writes the current zoom as-is. */
static OSErr WriteCurrentDocument(SaveFormat fmt)
{
    short savedZoom;
    OSErr err;

    if (fmt == kFormatQuill)
        return WriteDocumentAsQuill(&gDoc, &gDoc.file, gZoomPercent);

    /* Comments survive in .docx and .doc (real w:comment/annotation data),
       but .rtf export drops them outright - warn before writing so it's
       not a silent surprise. */
    if (fmt == kFormatRtf && gDoc.commentCount > 0) {
        Warn("This document has comments. Comments are not supported in "
             "plain .rtf export and will be lost - use .docx or .doc "
             "(which is RTF with comments included) to keep them.");
    }

    savedZoom = gZoomPercent;
    if (savedZoom != 100) RescaleDocument(100);
    err = (fmt == kFormatDocx) ? WriteDocumentAsDocx(&gDoc, &gDoc.file)
                                : WriteDocumentAsRtf(&gDoc, &gDoc.file, fmt == kFormatDoc);
    if (savedZoom != 100) RescaleDocument(savedZoom);
    return err;
}

static Boolean DoSaveAs(SaveFormat fmt)
{
    StandardFileReply reply;
    OSErr err;

    StandardPutFile("\pSave document as:",
                     fmt == kFormatQuill ? "\pUntitled.qdoc" :
                     fmt == kFormatRtf ? "\pUntitled.rtf" :
                     fmt == kFormatDoc ? "\pUntitled.doc" : "\pUntitled.docx",
                     &reply);
    if (!reply.sfGood)
        return false;

    gDoc.file = reply.sfFile;
    gDoc.haveFile = true;
    gDoc.format = fmt;

    err = WriteCurrentDocument(fmt);

    if (err != noErr) {
        Fail("Could not save the document.");
        return false;
    }
    gDoc.dirty = false;
    SetWTitle(gDoc.window, reply.sfFile.name);
    return true;
}

/* Plain Save (Cmd-S) on a document with no file yet defaults to .qdoc, not
   an export format - dedicated "Save As .docx/.rtf/.doc" menu items exist
   for when an export format is specifically wanted, so the bare/default
   Save should use Quill's own native, fully round-trippable format. */
static void DoSave(void)
{
    OSErr err;

    if (!gDoc.haveFile) {
        DoSaveAs(kFormatQuill);
        return;
    }

    err = WriteCurrentDocument(gDoc.format);

    if (err != noErr) {
        Fail("Could not save the document.");
        return;
    }
    gDoc.dirty = false;
}

/* Used by ConfirmDiscardChanges's "Save" button: saves in place if the
   document already has a file (in whatever format it was last saved as),
   otherwise falls through to a Save As .qdoc (see DoSave's comment).
   Returns false if nothing actually got saved - either a write error or
   the user cancelled the Save As dialog - so the caller knows NOT to
   discard. */
static Boolean SaveBeforeDiscard(void)
{
    OSErr err;

    if (!gDoc.haveFile)
        return DoSaveAs(kFormatQuill);

    err = WriteCurrentDocument(gDoc.format);
    if (err != noErr) {
        Fail("Could not save the document.");
        return false;
    }
    gDoc.dirty = false;
    return true;
}

/* Returns true if it's OK to proceed (discard/replace the current
   document): either there was nothing unsaved, or the user chose "Don't
   Save", or the user chose "Save" and it actually succeeded. Returns false
   for Cancel, or for "Save" if the save itself failed/was cancelled - the
   caller must not proceed in that case. */
static Boolean ConfirmDiscardChanges(void)
{
    DialogPtr dlg;
    short itemHit;
    Str255 title;

    if (!gDoc.dirty)
        return true;

    GetWTitle(gDoc.window, title);
    ParamText(title, "\p", "\p", "\p");

    dlg = GetNewDialog(kConfirmDialogID, NULL, (WindowPtr)-1L);
    if (!dlg)
        return false;

    for (;;) {
        ModalDialog(NULL, &itemHit);
        if (itemHit >= 1 && itemHit <= 3)
            break;
    }
    DisposeDialog(dlg);

    switch (itemHit) {
        case 1:  return true;                 /* Don't Save */
        case 3:  return SaveBeforeDiscard();  /* Save */
        default: return false;                 /* Cancel */
    }
}

/* Only .qdoc (Quill's own format) can be opened back - .docx/.rtf/.doc are
   write-only export targets (see the README's "Why .doc is actually RTF"
   and the general Known limitations on why round-tripping those isn't
   implemented). No unsaved-changes prompt: same as New, this is a known,
   accepted gap for a "basic" app rather than a partial/half-built one. */
static void DoOpen(void)
{
    StandardFileReply reply;
    short zoomPercent;
    OSErr err;

    StandardGetFile(NULL, -1, NULL, &reply);
    if (!reply.sfGood)
        return;

    TESetSelect(0, 32767, gDoc.body);
    TEDelete(gDoc.body);
    ClearFootnotes();
    ClearComments();

    err = ReadDocumentFromQuill(&gDoc, &reply.sfFile, &zoomPercent);
    if (err != noErr) {
        Fail("Could not open that document - is it a Quill (.qdoc) file?");
        DoNew();
        return;
    }

    gDoc.haveFile = true;
    gDoc.format = kFormatQuill;
    gDoc.file = reply.sfFile;
    gDoc.dirty = false;
    gZoomPercent = zoomPercent;

    TESetSelect(0, 0, gDoc.body);
    TECalText(gDoc.body);
    UpdateScrollBarRange();
    ForceRedraw();
    SetWTitle(gDoc.window, reply.sfFile.name);

    gSizeWarningShown = false;
    CheckDocumentSize();
}

/* Imports .rtf, and .doc files that are actually RTF content under a .doc
   name (as this app's own "Save As .doc" produces, and as many real-world
   ".doc" files in the wild turn out to be) - see ReadDocumentFromRtf's
   header comment for exactly what is/isn't recovered. Genuine binary OLE2
   .doc and .docx (DEFLATE-compressed ZIP) are both out of scope; detected
   by content, not extension, and rejected with an explanatory message
   rather than silently producing garbage. No type/creator filtering on the
   StandardGetFile call, same reasoning as DoOpen - a file brought over from
   a modern system won't reliably carry meaningful classic Mac type codes,
   so content sniffing is the only reliable signal. */
typedef enum { kImportRtf, kImportOle2Doc, kImportUnknown } ImportKind;

/* Reports why a ReadDocumentFrom{Rtf,Doc} failure happened, in terms
   specific enough to actually act on - "could not be imported" alone
   doesn't tell anyone whether to try a smaller file, free up memory, or
   give up on that particular file entirely. */
static void FailImportError(OSErr err, Boolean isDoc)
{
    if (err == kImportTooLargeErr) {
        Fail("That file is too large to import - classic TextEdit has a "
             "hard limit around 28,000 characters, and this file would "
             "exceed it.");
    } else if (err == memFullErr) {
        Fail("Not enough memory to import that file.");
    } else if (isDoc) {
        Fail("That file could not be imported - its internal structure "
             "wasn\xd5t recognized. Only Word 97-2003 format is supported, "
             "and only its plain text, not real binary Word documents in "
             "general.");
    } else {
        Fail("That file could not be imported - its RTF content could not "
             "be read.");
    }
}

static void DoImport(void)
{
    StandardFileReply reply;
    short refNum;
    long fileLen, count;
    char head[8];
    OSErr err;
    ImportKind kind;

    StandardGetFile(NULL, -1, NULL, &reply);
    if (!reply.sfGood)
        return;

    err = FSpOpenDF(&reply.sfFile, fsRdPerm, &refNum);
    if (err != noErr) {
        Fail("Could not open that file.");
        return;
    }
    err = GetEOF(refNum, &fileLen);
    if (err != noErr) {
        FSClose(refNum);
        Fail("Could not open that file.");
        return;
    }
    count = (fileLen < 8) ? fileLen : 8;
    FSRead(refNum, &count, head);
    FSClose(refNum);

    if (count >= 5 && strncmp(head, "{\\rtf", 5) == 0) {
        kind = kImportRtf;
    } else if (count >= 4 && (unsigned char)head[0] == 0xD0 &&
               (unsigned char)head[1] == 0xCF && (unsigned char)head[2] == 0x11 &&
               (unsigned char)head[3] == 0xE0) {
        kind = kImportOle2Doc;
    } else {
        kind = kImportUnknown;
    }

    if (kind == kImportUnknown) {
        if (count >= 4 && head[0] == 'P' && head[1] == 'K' && head[2] == 3 && head[3] == 4) {
            Fail("That looks like a .docx file. Importing .docx isn\xd5t "
                 "supported yet \xd0 only .rtf files, and .doc files (real "
                 "binary Word 97-2003, or files that are actually RTF "
                 "content), can be imported.");
        } else {
            Fail("That file doesn\xd5t look like RTF or a binary Word "
                 "document. Only .rtf files and .doc files can be imported.");
        }
        return;
    }

    if (kind == kImportRtf) {
        Warn("Import is best-effort: only text, bold, italic, underline, "
             "font, and size are recovered. Paragraph alignment, lists, "
             "tables, footnotes, and embedded objects are not imported.");
    } else {
        Warn("This is a real binary Word document. Import recovers plain "
             "text only \xd0 no bold, italic, underline, fonts, styles, "
             "footnotes, comments, tables, or embedded objects.");
    }

    TESetSelect(0, 32767, gDoc.body);
    TEDelete(gDoc.body);
    ClearFootnotes();
    ClearComments();

    err = (kind == kImportRtf) ? ReadDocumentFromRtf(&gDoc, &reply.sfFile)
                                : ReadDocumentFromDoc(&gDoc, &reply.sfFile);
    if (err != noErr) {
        if (err == kImportTooLargeErr) {
            /* Keep the partial content inserted by the reader and warn the user. */
            Warn("The file was partially imported: only the portion that fit in memory was opened.");
        } else {
            FailImportError(err, (Boolean)(kind == kImportOle2Doc));
            DoNew();
            return;
        }
    }

    /* Imported content isn't the same file as its source - "Save" should
       offer Save As .qdoc, not silently overwrite the original .rtf/.doc,
       so haveFile/file are deliberately left as a "new" document's. */
    gDoc.haveFile = false;
    gDoc.format = kFormatQuill;
    gDoc.dirty = true;
    gZoomPercent = 100;

    TESetSelect(0, 0, gDoc.body);
    TECalText(gDoc.body);
    UpdateScrollBarRange();
    ForceRedraw();
    SetWTitle(gDoc.window, reply.sfFile.name);

    gSizeWarningShown = false;
    CheckDocumentSize();
}

static long TextLength(void)
{
    return GetHandleSize((Handle)TEGetText(gDoc.body));
}

static short ScaleSize(short logicalSize)
{
    return (short)(((long)logicalSize * gZoomPercent + 50) / 100);
}

static short UnscaleSize(short actualSize)
{
    if (gZoomPercent == 0)
        return actualSize;
    return (short)(((long)actualSize * 100 + gZoomPercent / 2) / gZoomPercent);
}

/* Zoom is implemented by rescaling every stored point size proportionally
   (current zoom -> new zoom) rather than a separate visual-only transform -
   see the README for why (classic TextEdit/QuickDraw has no
   coordinate-scaling primitive short of an offscreen GWorld blit, which is
   much more machinery for this pass). Save/Save As always rescale to 100%
   first so exported files hold true logical sizes no matter the current
   zoom.

   This edits the style TABLE's size fields directly, in place, rather than
   the earlier approach of walking runs and calling TESetSelect/TESetStyle
   per run. That earlier approach was unreliable in practice: TESetStyle can
   split/merge/reindex style-table entries as it goes, and issuing a whole
   sequence of such calls back to back (one per run, in the same pass) could
   end up applying to the wrong entries or skipping some - which matches
   reports of "normal text doesn't scale" / "scales inconsistently with
   headings". Every run's font/size/face lives in exactly one STElement
   entry in the style table (`styleTab`), addressed by `runs[i].styleIndex`;
   multiple runs can share one entry. Multiplying every entry's `stSize` in
   place rescales *all* text in one pass, with no risk of run-table
   reshuffling, and doesn't touch the selection at all. The one thing not
   covered by the table is the "null style" (what the next *typed*
   character will use, relevant for a still-empty document or typing at the
   very end) - that's rescaled the same way via its own separate struct.

   Each STElement/ScrpSTElement also caches stHeight/stAscent (and
   scrpHeight/scrpAscent for the null style) - the line height and ascent
   TextEdit measured for that style *when it was created*, used for the
   caret and line layout. Changing stSize alone leaves those stale: the
   glyphs themselves draw at the new size (drawing reads stSize directly),
   but the caret is drawn using the old cached height, so it stops matching
   the text - it stays whatever size it was before the zoom changed. Every
   entry's height/ascent needs recomputing from the font's *actual* metrics
   at its new size, the same way TESetStyle itself would when first
   creating a style at that size. */
static void RescaleDocument(short newZoomPercent)
{
    TEStyleHandle sh;
    TEStyleRec *rec;
    STHandle tab;
    STElement *table;
    short nStyles, i;
    NullSTHandle nsh;
    GrafPtr port;
    INTEGER savedFont, savedSize;
    Style savedFace;
    short viewHeight, oldMaxScroll, oldOffset, newMaxScroll, newOffset;
    long oldTotalHeight, newTotalHeight;

    if (newZoomPercent == gZoomPercent)
        return;

    SetPort(gDoc.window);

    /* Capture the current scroll position as a *fraction* of the
       scrollable range before rescaling, so it can be restored
       proportionally afterward - line heights change with font size, so
       the raw pixel offset that was valid before reflow means something
       different (or points past the end of a now-shorter document, which
       is exactly the "screen goes blank" case) once TECalText re-lays-out
       the text at the new size. */
    viewHeight = (**gDoc.body).viewRect.bottom - (**gDoc.body).viewRect.top;
    oldTotalHeight = TEGetHeight((**gDoc.body).nLines, 0, gDoc.body);
    oldMaxScroll = (short)((oldTotalHeight > viewHeight) ? (oldTotalHeight - viewHeight) : 0);
    oldOffset = (short)((**gDoc.body).viewRect.top - (**gDoc.body).destRect.top);
    if (oldOffset < 0) oldOffset = 0;
    if (oldOffset > oldMaxScroll) oldOffset = oldMaxScroll;

    /* TextFont/TextFace/TextSize below are only a means to feed GetFontInfo
       for each style table entry - they mutate the port's *ambient* text
       state as a side effect, which has nothing to do with any entry's
       stored formatting. Left unrestored, the port would come out of this
       function with its ambient face set to whatever the last-processed
       entry happened to be (often a heading's bold/italic combination),
       which is stray state no caller expects. */
    port = gDoc.window;
    savedFont = port->txFont;
    savedFace = port->txFace;
    savedSize = port->txSize;

    sh = TEGetStyleHandle(gDoc.body);
    HLock((Handle)sh);
    rec = *sh;
    nStyles = rec->nStyles;
    tab = rec->styleTab;
    nsh = rec->nullStyle;

    HLock((Handle)tab);
    table = *tab;
    for (i = 0; i < nStyles; i++) {
        FontInfo info;

        table[i].stSize = (short)(((long)table[i].stSize * newZoomPercent + gZoomPercent / 2) / gZoomPercent);

        TextFont(table[i].stFont);
        TextFace(table[i].stFace);
        TextSize(table[i].stSize);
        GetFontInfo(&info);
        table[i].stAscent = info.ascent;
        table[i].stHeight = (short)(info.ascent + info.descent + info.leading);
    }
    HUnlock((Handle)tab);

    if (nsh) {
        NullSTPtr ns;
        StScrpHandle scrapH;

        HLock((Handle)nsh);
        ns = *nsh;
        scrapH = ns->nullScrap;
        if (scrapH) {
            StScrpRec *scrap;

            HLock((Handle)scrapH);
            scrap = *scrapH;
            if (scrap->scrpNStyles > 0) {
                ScrpSTElement *e = &scrap->scrpStyleTab[0];
                FontInfo info;

                e->scrpSize = (short)(((long)e->scrpSize * newZoomPercent + gZoomPercent / 2) / gZoomPercent);

                TextFont(e->scrpFont);
                TextFace(e->scrpFace);
                TextSize(e->scrpSize);
                GetFontInfo(&info);
                e->scrpAscent = info.ascent;
                e->scrpHeight = (short)(info.ascent + info.descent + info.leading);
            }
            HUnlock((Handle)scrapH);
        }
        HUnlock((Handle)nsh);
    }

    HUnlock((Handle)sh);

    TextFont(savedFont);
    TextFace(savedFace);
    TextSize(savedSize);

    gZoomPercent = newZoomPercent;
    TECalText(gDoc.body);

    /* Re-derive the proportional scroll position at the new size, then
       clamp into [0, newMaxScroll]. That clamp is what actually guarantees
       the view can't end up blank - destRect.top can never be pushed to
       where less than a viewHeight's worth of text remains below it - not
       a special case bolted on afterward. The extra newTotalHeight check
       just below is belt-and-suspenders on top of that guarantee, since
       the user-visible cost of being wrong here (a blank document window)
       is much worse than the cost of a redundant check. */
    newTotalHeight = TEGetHeight((**gDoc.body).nLines, 0, gDoc.body);
    newMaxScroll = (short)((newTotalHeight > viewHeight) ? (newTotalHeight - viewHeight) : 0);
    newOffset = (oldMaxScroll > 0)
        ? (short)(((long)oldOffset * newMaxScroll + oldMaxScroll / 2) / oldMaxScroll)
        : 0;
    if (newOffset < 0) newOffset = 0;
    if (newOffset > newMaxScroll) newOffset = newMaxScroll;
    if (newTotalHeight <= newOffset)
        newOffset = 0;

    {
        short newDestTop = (short)((**gDoc.body).viewRect.top - newOffset);
        short destHeight = (**gDoc.body).destRect.bottom - (**gDoc.body).destRect.top;
        (**gDoc.body).destRect.top = newDestTop;
        (**gDoc.body).destRect.bottom = (short)(newDestTop + destHeight);
    }

    UpdateScrollBarRange();
    ForceRedraw();
}

static void AdjustMarkersAfterEdit(long editPos, long delta)
{
    short i;
    if (delta == 0)
        return;
    for (i = 0; i < gDoc.footnoteCount; i++) {
        if (gDoc.footnotes[i].anchorOffset >= editPos)
            gDoc.footnotes[i].anchorOffset += delta;
    }
    for (i = 0; i < gDoc.commentCount; i++) {
        if (gDoc.comments[i].anchorOffset >= editPos)
            gDoc.comments[i].anchorOffset += delta;
    }
}

static Footnote *FindFootnoteContainingOffset(long offset)
{
    short i;
    for (i = 0; i < gDoc.footnoteCount; i++) {
        if (offset >= gDoc.footnotes[i].anchorOffset &&
            offset < gDoc.footnotes[i].anchorOffset + gDoc.footnotes[i].markerLen)
            return &gDoc.footnotes[i];
    }
    return NULL;
}

static Footnote *FindCommentContainingOffset(long offset)
{
    short i;
    for (i = 0; i < gDoc.commentCount; i++) {
        if (offset >= gDoc.comments[i].anchorOffset &&
            offset < gDoc.comments[i].anchorOffset + gDoc.comments[i].markerLen)
            return &gDoc.comments[i];
    }
    return NULL;
}

/* Shared by the footnote and comment dialogs (DLOG/DITL 200 and 205 in
   main.r) - identical layout (OK=1, Cancel=2, label, EditText=kDialogTextItem),
   just different title/label text baked into the resource. */
static Boolean RunSimpleTextDialog(short dialogID, Str255 text)
{
    DialogPtr dlg;
    short itemType;
    Handle itemH;
    Rect box;
    short itemHit;

    dlg = GetNewDialog(dialogID, NULL, (WindowPtr)-1L);
    if (!dlg)
        return false;

    GetDialogItem(dlg, kDialogTextItem, &itemType, &itemH, &box);
    SetDialogItemText(itemH, text);
    SelectDialogItemText(dlg, kDialogTextItem, 0, 32767);

    for (;;) {
        ModalDialog(NULL, &itemHit);
        if (itemHit == 1 || itemHit == 2)
            break;
    }

    if (itemHit == 1)
        GetDialogItemText(itemH, text);

    DisposeDialog(dlg);
    return (Boolean)(itemHit == 1);
}

static void EditFootnote(Footnote *fn)
{
    Str255 text;
    long len = GetHandleSize(fn->text);

    if (len > 255)
        len = 255;
    text[0] = (unsigned char)len;
    HLock(fn->text);
    BlockMove(*fn->text, &text[1], len);
    HUnlock(fn->text);

    if (RunSimpleTextDialog(kFootnoteDialogID, text)) {
        SetHandleSize(fn->text, text[0]);
        if (text[0] > 0)
            BlockMove(&text[1], *fn->text, text[0]);
        gDoc.dirty = true;
    }
}

static void EditComment(Footnote *cm)
{
    Str255 text;
    long len = GetHandleSize(cm->text);

    if (len > 255)
        len = 255;
    text[0] = (unsigned char)len;
    HLock(cm->text);
    BlockMove(*cm->text, &text[1], len);
    HUnlock(cm->text);

    if (RunSimpleTextDialog(kCommentDialogID, text)) {
        SetHandleSize(cm->text, text[0]);
        if (text[0] > 0)
            BlockMove(&text[1], *cm->text, text[0]);
        gDoc.dirty = true;
    }
}

/* The in-body footnote marker is shrunk to ~65% of the surrounding text's
   actual size as a superscript approximation: classic TextEdit has no
   baseline-shift/superscript concept at all (checked - there's truly no
   such style bit or call in this toolchain), so a real raised, small
   reference mark isn't achievable without a custom text renderer. The
   exported .docx already uses a genuine <w:vertAlign val="superscript">
   run, independent of this on-screen approximation. Colored pure blue
   (doColor) so it also reads as a distinct marker at a glance, matching
   comments' yellow - QuickDraw collapses both to black on a B&W port, so
   this doesn't need separate handling for non-color Macs. */
static void DoInsertFootnote(void)
{
    Str255 text;
    Footnote *fn;
    char markerBuf[8];
    long markerLen;
    TextStyle baseStyle, markerStyle;
    short baseMode;

    if (gDoc.footnoteCount >= kMaxFootnotes) {
        Fail("Too many footnotes in this document.");
        return;
    }

    text[0] = 0;
    if (!RunSimpleTextDialog(kFootnoteDialogID, text))
        return;

    fn = &gDoc.footnotes[gDoc.footnoteCount];
    fn->number = gDoc.footnoteCount + 1;
    fn->text = NewHandle(text[0]);
    if (text[0] > 0)
        BlockMove(&text[1], *fn->text, text[0]);

    sprintf(markerBuf, "%d", fn->number);
    markerLen = (long)strlen(markerBuf);

    fn->anchorOffset = (**gDoc.body).selStart;
    fn->markerLen = (short)markerLen;

    baseMode = doFont | doSize | doFace;
    TEContinuousStyle(&baseMode, &baseStyle, gDoc.body);
    markerStyle = baseStyle;
    markerStyle.tsSize = (short)(((long)baseStyle.tsSize * 65 + 50) / 100);
    if (markerStyle.tsSize < 6)
        markerStyle.tsSize = 6;
    markerStyle.tsColor.red = 0x0000;
    markerStyle.tsColor.green = 0x0000;
    markerStyle.tsColor.blue = 0xFFFF;

    TEInsert(markerBuf, markerLen, gDoc.body);
    AdjustMarkersAfterEdit(fn->anchorOffset, markerLen);
    TESetSelect(fn->anchorOffset, fn->anchorOffset + markerLen, gDoc.body);
    TESetStyle(doSize | doColor, &markerStyle, true, gDoc.body);
    TESetSelect(fn->anchorOffset + markerLen, fn->anchorOffset + markerLen, gDoc.body);

    gDoc.footnoteCount++;
    gDoc.dirty = true;
    UpdateScrollBarRange();
}

/* Mirrors DoInsertFootnote, but the in-body marker is a single fixed glyph
   (kCommentMarkerChar) rather than a printed number - see its comment in
   wordproc.h for why - shrunk the same ~65% and colored pure yellow. */
static void DoInsertComment(void)
{
    Str255 text;
    Footnote *cm;
    long markerLen = 1;
    TextStyle baseStyle, markerStyle;
    short baseMode;
    char markerChar = (char)kCommentMarkerChar;

    if (gDoc.commentCount >= kMaxComments) {
        Fail("Too many comments in this document.");
        return;
    }

    text[0] = 0;
    if (!RunSimpleTextDialog(kCommentDialogID, text))
        return;

    cm = &gDoc.comments[gDoc.commentCount];
    cm->number = gDoc.commentCount + 1;
    cm->text = NewHandle(text[0]);
    if (text[0] > 0)
        BlockMove(&text[1], *cm->text, text[0]);

    cm->anchorOffset = (**gDoc.body).selStart;
    cm->markerLen = (short)markerLen;

    baseMode = doFont | doSize | doFace;
    TEContinuousStyle(&baseMode, &baseStyle, gDoc.body);
    markerStyle = baseStyle;
    markerStyle.tsSize = (short)(((long)baseStyle.tsSize * 65 + 50) / 100);
    if (markerStyle.tsSize < 6)
        markerStyle.tsSize = 6;
    markerStyle.tsColor.red = 0xFFFF;
    markerStyle.tsColor.green = 0xFFFF;
    markerStyle.tsColor.blue = 0x0000;

    TEInsert(&markerChar, markerLen, gDoc.body);
    AdjustMarkersAfterEdit(cm->anchorOffset, markerLen);
    TESetSelect(cm->anchorOffset, cm->anchorOffset + markerLen, gDoc.body);
    TESetStyle(doSize | doColor, &markerStyle, true, gDoc.body);
    TESetSelect(cm->anchorOffset + markerLen, cm->anchorOffset + markerLen, gDoc.body);

    gDoc.commentCount++;
    gDoc.dirty = true;
    UpdateScrollBarRange();
}

static void GetParagraphRange(long offset, long *pStart, long *pEnd)
{
    CharsHandle th = TEGetText(gDoc.body);
    long textLen = GetHandleSize((Handle)th);
    long start, end;
    char *text;

    HLock((Handle)th);
    text = *th;

    start = offset;
    while (start > 0 && text[start - 1] != '\r')
        start--;

    end = offset;
    while (end < textLen && text[end] != '\r')
        end++;

    HUnlock((Handle)th);

    *pStart = start;
    *pEnd = end;
}

static void GetSelectionParagraphRange(long *outStart, long *outEnd)
{
    long selStart = (**gDoc.body).selStart;
    long selEnd = (**gDoc.body).selEnd;
    long pStart, pEnd, dummy;

    GetParagraphRange(selStart, &pStart, &dummy);
    GetParagraphRange(selEnd, &dummy, &pEnd);

    *outStart = pStart;
    *outEnd = pEnd;
}

static short CollectTouchedParagraphs(long selStart, long selEnd, long *starts, long *ends)
{
    CharsHandle th;
    long textLen;
    long pos, dummy;
    short count = 0;

    GetParagraphRange(selStart, &pos, &dummy);

    th = TEGetText(gDoc.body);
    textLen = GetHandleSize((Handle)th);

    while (pos <= selEnd && count < kMaxTouchedParagraphs) {
        long end;
        char *text;

        HLock((Handle)th);
        text = *th;
        end = pos;
        while (end < textLen && text[end] != '\r')
            end++;
        HUnlock((Handle)th);

        starts[count] = pos;
        ends[count] = end;
        count++;

        if (end >= textLen)
            break;
        pos = end + 1;
    }

    return count;
}

/* Every paragraph style uses the app's body font (Times) except Plain Text,
   which deliberately uses classic TextEdit's own default (systemFont) - see
   wordproc.h's ParaStyleSpec.systemFont comment for why that distinction
   exists. Applying a style leaves the affected paragraph(s) selected so the
   change is immediately visible. */
static void ApplyParaStyle(ParaStyleKind kind)
{
    long rangeStart, rangeEnd;
    TextStyle ts;

    GetSelectionParagraphRange(&rangeStart, &rangeEnd);
    TESetSelect(rangeStart, rangeEnd, gDoc.body);

    memset(&ts, 0, sizeof(ts));
    ts.tsFont = kParaStyleSpecs[kind].systemFont ? systemFont : gBodyFontID;
    ts.tsSize = ScaleSize(kParaStyleSpecs[kind].size);
    ts.tsFace = 0;
    if (kParaStyleSpecs[kind].bold) ts.tsFace |= bold;
    if (kParaStyleSpecs[kind].italic) ts.tsFace |= italic;
    if (kParaStyleSpecs[kind].underline) ts.tsFace |= underline;

    TESetStyle(doFont | doSize | doFace, &ts, true, gDoc.body);
    gDoc.dirty = true;
    UpdateScrollBarRange();
}

static void ToggleList(ListKind kind)
{
    long starts[kMaxTouchedParagraphs], ends[kMaxTouchedParagraphs];
    short count, i;
    long selStart = (**gDoc.body).selStart;
    long selEnd = (**gDoc.body).selEnd;

    count = CollectTouchedParagraphs(selStart, selEnd, starts, ends);

    for (i = count - 1; i >= 0; i--) {
        CharsHandle th;
        char *text;
        long paraLen = ends[i] - starts[i];
        long existingMarkerLen;
        ListKind existing;

        th = TEGetText(gDoc.body);
        HLock((Handle)th);
        text = *th;
        existing = DetectListMarker(text + starts[i], paraLen, &existingMarkerLen);
        HUnlock((Handle)th);

        if (existing != kListNone) {
            TESetSelect(starts[i], starts[i] + existingMarkerLen, gDoc.body);
            TEDelete(gDoc.body);
            AdjustMarkersAfterEdit(starts[i], -existingMarkerLen);
        }

        if (existing != kind) {
            char marker[16];
            long markerLen;

            if (kind == kListBullet) {
                marker[0] = (char)kBulletMarkerByte;
                marker[1] = ' ';
                markerLen = 2;
            } else {
                sprintf(marker, "%d. ", i + 1);
                markerLen = (long)strlen(marker);
            }

            TESetSelect(starts[i], starts[i], gDoc.body);
            TEInsert(marker, markerLen, gDoc.body);
            AdjustMarkersAfterEdit(starts[i], markerLen);
        }
    }

    gDoc.dirty = true;
    UpdateScrollBarRange();
}

static void ToggleFace(Style bit)
{
    short mode = doFace;
    TextStyle ts;
    Boolean continuous;
    Style newFace;

    memset(&ts, 0, sizeof(ts));
    continuous = TEContinuousStyle(&mode, &ts, gDoc.body);
    newFace = (continuous && (mode & doFace)) ? (Style)(ts.tsFace ^ bit) : bit;

    memset(&ts, 0, sizeof(ts));
    ts.tsFace = newFace;
    TESetStyle(doFace, &ts, true, gDoc.body);
    gDoc.dirty = true;
    UpdateScrollBarRange();
}

static void SetFontByName(const char *name)
{
    Str255 pname;
    short fontID;
    TextStyle ts;

    CToPascal(name, pname);
    GetFNum(pname, &fontID);
    memset(&ts, 0, sizeof(ts));
    ts.tsFont = fontID;
    TESetStyle(doFont, &ts, true, gDoc.body);
    gDoc.dirty = true;
    UpdateScrollBarRange();
}

static void SetSize(short size)
{
    TextStyle ts;
    memset(&ts, 0, sizeof(ts));
    ts.tsSize = ScaleSize(size);
    TESetStyle(doSize, &ts, true, gDoc.body);
    gDoc.dirty = true;
    UpdateScrollBarRange();
}

/* Classic TextEdit's alignment (`just`) is a single value for the whole
   text record, not per-paragraph - every paragraph in the document shares
   it. There's also no true "justify" (stretch-to-both-margins); Justify
   maps to left-align, the closest available, and is documented as such. */
static void HandleAlignMenu(short item)
{
    short j;

    switch (item) {
        case iAlignLeft:    j = teJustLeft; break;
        case iAlignCenter:  j = teJustCenter; break;
        case iAlignRight:   j = teJustRight; break;
        case iAlignJustify: j = teJustLeft; break;
        default: return;
    }

    TESetAlignment(j, gDoc.body);
    TECalText(gDoc.body);
    UpdateScrollBarRange();
    ForceRedraw();
    gDoc.dirty = true;
}

static void HandleZoomMenu(short item)
{
    if (item >= 1 && item <= kZoomCount)
        RescaleDocument(kZoomLevels[item - 1]);
}

static void Fail(const char *msg)
{
    Str255 s;
    CToPascal(msg, s);
    ParamText(s, "\p", "\p", "\p");
    StopAlert(kErrorAlertID, NULL);
}

static void Warn(const char *msg)
{
    Str255 s;
    CToPascal(msg, s);
    ParamText(s, "\p", "\p", "\p");
    CautionAlert(kWarnAlertID, NULL);
}

/* Fires once when the document crosses kSizeWarnThreshold, and resets so it
   can fire again if the document is trimmed back down and grows past the
   threshold a second time. See kSizeWarnThreshold's comment for why this
   can't just be raised instead - it's a hard TextEdit limit, not a tunable. */
static void CheckDocumentSize(void)
{
    long len = TextLength();

    if (len >= kSizeWarnThreshold) {
        if (!gSizeWarningShown) {
            gSizeWarningShown = true;
            Warn("This document is approaching classic TextEdit's hard "
                 "~32,000-character limit (about 5,000 words). Consider "
                 "splitting it into more than one document soon.");
        }
    } else {
        gSizeWarningShown = false;
    }
}

static void AdjustMenus(void)
{
    short mode;
    TextStyle ts;
    Boolean continuous;
    short i;

    mode = doFace | doFont | doSize;
    memset(&ts, 0, sizeof(ts));
    continuous = TEContinuousStyle(&mode, &ts, gDoc.body);

    CheckItem(gFormatMenu, iBold, (Boolean)(continuous && (ts.tsFace & bold) != 0));
    CheckItem(gFormatMenu, iItalic, (Boolean)(continuous && (ts.tsFace & italic) != 0));
    CheckItem(gFormatMenu, iUnderline, (Boolean)(continuous && (ts.tsFace & underline) != 0));

    for (i = 0; i < kFontCount; i++) {
        Str255 pname;
        short fnum;
        CToPascal(kFontNames[i], pname);
        GetFNum(pname, &fnum);
        CheckItem(gFormatMenu, iFontFirst + i, (Boolean)(continuous && ts.tsFont == fnum));
    }
    for (i = 0; i < kSizeCount; i++) {
        CheckItem(gFormatMenu, iSizeFirst + i, (Boolean)(continuous && ts.tsSize == ScaleSize(kSizes[i])));
    }

    {
        long pStart, pEnd, markerLen;
        CharsHandle th;
        char *text;
        ParaStyleKind kind;
        ListKind lk;
        short mode2;
        TextStyle ts2;
        Boolean cont2;
        short k;

        GetParagraphRange((**gDoc.body).selStart, &pStart, &pEnd);

        mode2 = doFace | doSize | doFont;
        cont2 = TEContinuousStyle(&mode2, &ts2, gDoc.body);
        kind = cont2
                   ? DetectParaStyle(UnscaleSize(ts2.tsSize),
                                      (Boolean)((ts2.tsFace & bold) != 0),
                                      (Boolean)((ts2.tsFace & italic) != 0),
                                      (Boolean)((ts2.tsFace & underline) != 0),
                                      (Boolean)(ts2.tsFont == systemFont))
                   : pStyleNormal;

        for (k = 0; k < kParaStyleCount; k++)
            CheckItem(gStyleMenu, k + 1, (Boolean)(k == kind));

        th = TEGetText(gDoc.body);
        HLock((Handle)th);
        text = *th;
        lk = DetectListMarker(text + pStart, pEnd - pStart, &markerLen);
        HUnlock((Handle)th);

        CheckItem(gStyleMenu, iBulletList, (Boolean)(lk == kListBullet));
        CheckItem(gStyleMenu, iNumberedList, (Boolean)(lk == kListNumbered));
    }

    {
        short just = (**gDoc.body).just;
        CheckItem(gAlignMenu, iAlignLeft, (Boolean)(just == teJustLeft));
        CheckItem(gAlignMenu, iAlignCenter, (Boolean)(just == teJustCenter));
        CheckItem(gAlignMenu, iAlignRight, (Boolean)(just == teJustRight));
        CheckItem(gAlignMenu, iAlignJustify, false);
    }

    {
        short z;
        for (z = 0; z < kZoomCount; z++)
            CheckItem(gZoomMenu, z + 1, (Boolean)(kZoomLevels[z] == gZoomPercent));
    }
}

static void HandleFileMenu(short item)
{
    switch (item) {
        case iNew:           if (ConfirmDiscardChanges()) DoNew(); break;
        case iOpen:          if (ConfirmDiscardChanges()) DoOpen(); break;
        case iImport:        if (ConfirmDiscardChanges()) DoImport(); break;
        case iSave:          DoSave(); break;
        case iSaveAsQuill:   DoSaveAs(kFormatQuill); break;
        case iSaveAsDocx:    DoSaveAs(kFormatDocx); break;
        case iSaveAsRtf:     DoSaveAs(kFormatRtf); break;
        case iSaveAsDoc:     DoSaveAs(kFormatDoc); break;
        case iQuit:
            /* Only bother confirming if there's actually something in the
               document - an empty (even if somehow "dirty") document has
               nothing worth losing, so quit straight away. */
            if (TextLength() == 0 || ConfirmDiscardChanges())
                gDone = true;
            break;
    }
}

static void HandleEditMenu(short item)
{
    long pos, lenBefore;

    switch (item) {
        case iCut:
            pos = (**gDoc.body).selStart;
            lenBefore = TextLength();
            TECut(gDoc.body);
            AdjustMarkersAfterEdit(pos, TextLength() - lenBefore);
            gDoc.dirty = true;
            UpdateScrollBarRange();
            break;
        case iCopy:
            TECopy(gDoc.body);
            break;
        case iPaste:
            pos = (**gDoc.body).selStart;
            lenBefore = TextLength();
            TEPaste(gDoc.body);
            AdjustMarkersAfterEdit(pos, TextLength() - lenBefore);
            gDoc.dirty = true;
            UpdateScrollBarRange();
            CheckDocumentSize();
            break;
        case iClear:
            pos = (**gDoc.body).selStart;
            lenBefore = TextLength();
            TEDelete(gDoc.body);
            AdjustMarkersAfterEdit(pos, TextLength() - lenBefore);
            gDoc.dirty = true;
            UpdateScrollBarRange();
            break;
    }
}

static void HandleFormatMenu(short item)
{
    if (item == iBold) ToggleFace(bold);
    else if (item == iItalic) ToggleFace(italic);
    else if (item == iUnderline) ToggleFace(underline);
    else if (item >= iFontFirst && item <= iFontLast)
        SetFontByName(kFontNames[item - iFontFirst]);
    else if (item >= iSizeFirst && item <= iSizeLast)
        SetSize(kSizes[item - iSizeFirst]);
}

static void HandleInsertMenu(short item)
{
    if (item == iInsertFootnote)
        DoInsertFootnote();
    else if (item == iInsertComment)
        DoInsertComment();
}

static void HandleStyleMenu(short item)
{
    if (item >= 1 && item <= kParaStyleCount)
        ApplyParaStyle((ParaStyleKind)(item - 1));
    else if (item == iBulletList)
        ToggleList(kListBullet);
    else if (item == iNumberedList)
        ToggleList(kListNumbered);
}

static void HandleMenuChoice(long menuChoice)
{
    short menuID = HiWord(menuChoice);
    short item = LoWord(menuChoice);

    if (menuID != 0) {
        switch (menuID) {
            case mApple:
                if (item == iAbout)
                    Alert(kAboutAlertID, NULL);
                break;
            case mFile:   HandleFileMenu(item); break;
            case mEdit:   HandleEditMenu(item); break;
            case mFormat: HandleFormatMenu(item); break;
            case mInsert: HandleInsertMenu(item); break;
            case mStyle:  HandleStyleMenu(item); break;
            case mAlign:  HandleAlignMenu(item); break;
            case mZoom:   HandleZoomMenu(item); break;
        }
        HiliteMenu(0);
    }
}

static void InsertPlainReturn(void)
{
    long pos = (**gDoc.body).selStart;
    long lenBefore = TextLength();
    TEKey('\r', gDoc.body);
    AdjustMarkersAfterEdit(pos, TextLength() - lenBefore);
    gDoc.dirty = true;
}

/* Enter continues the current bullet/numbered list onto the new line
   (carrying over the same marker type, auto-incrementing the number),
   matching Word. Pressing Enter on an EMPTY list item instead strips that
   item's marker and exits list mode - consuming the keystroke rather than
   also inserting a blank line, again matching Word. */
static void HandleReturnKey(void)
{
    long caret = (**gDoc.body).selStart;
    long pStart, pEnd;
    CharsHandle th;
    char *text;
    ListKind existing;
    long markerLen;
    Boolean paragraphEmpty;
    short prevNum = 0;

    GetParagraphRange(caret, &pStart, &pEnd);

    th = TEGetText(gDoc.body);
    HLock((Handle)th);
    text = *th;
    existing = DetectListMarker(text + pStart, pEnd - pStart, &markerLen);
    paragraphEmpty = (pEnd - pStart) <= markerLen;
    if (existing == kListNumbered) {
        long k = 0;
        while (k < markerLen && text[pStart + k] >= '0' && text[pStart + k] <= '9') {
            prevNum = (short)(prevNum * 10 + (text[pStart + k] - '0'));
            k++;
        }
    }
    HUnlock((Handle)th);

    if (existing != kListNone && paragraphEmpty) {
        TESetSelect(pStart, pEnd, gDoc.body);
        TEDelete(gDoc.body);
        AdjustMarkersAfterEdit(pStart, pStart - pEnd);
        gDoc.dirty = true;
        UpdateScrollBarRange();
        return;
    }

    InsertPlainReturn();

    if (existing != kListNone) {
        char marker[16];
        long newMarkerLen;
        long newParaStart = (**gDoc.body).selStart;

        if (existing == kListBullet) {
            marker[0] = (char)kBulletMarkerByte;
            marker[1] = ' ';
            newMarkerLen = 2;
        } else {
            sprintf(marker, "%d. ", prevNum + 1);
            newMarkerLen = (long)strlen(marker);
        }

        TEInsert(marker, newMarkerLen, gDoc.body);
        AdjustMarkersAfterEdit(newParaStart, newMarkerLen);
        TESetSelect(newParaStart + newMarkerLen, newParaStart + newMarkerLen, gDoc.body);
    }

    UpdateScrollBarRange();
}

void RunApp(void)
{
    EventRecord event;

    ToolboxInit();
    /* Try to grow the application's heap before allocating big buffers/windows */
    TryMaximizeHeap();

    SetupMenus();
    CreateDocumentWindow();
    AdjustMenus(); /* so Format/Style/Align/Zoom checkmarks are correct before any interaction */

    while (!gDone) {
        if (WaitNextEvent(everyEvent, &event, 15, NULL)) {
            switch (event.what) {
                case mouseDown: {
                    WindowPtr whichWindow;
                    short part = FindWindow(event.where, &whichWindow);
                    switch (part) {
                        case inMenuBar:
                            AdjustMenus();
                            HandleMenuChoice(MenuSelect(event.where));
                            break;
                        case inContent:
                            if (whichWindow != FrontWindow()) {
                                SelectWindow(whichWindow);
                            } else {
                                Point local = event.where;
                                ControlHandle ctl;
                                short ctlPart;

                                /* GlobalToLocal converts relative to the
                                   *current* port's origin - if a dialog
                                   (Save/Open/Alert) left the port pointing
                                   somewhere else, this would silently
                                   mis-hit-test the scrollbar. */
                                SetPort(whichWindow);
                                GlobalToLocal(&local);
                                ctlPart = FindControl(local, whichWindow, &ctl);

                                if (ctl == gVScrollBar && ctlPart != 0) {
                                    if (ctlPart == inThumb) {
                                        short oldValue = GetControlValue(gVScrollBar);
                                        if (TrackControl(gVScrollBar, local, NULL) != 0) {
                                            short newValue = GetControlValue(gVScrollBar);
                                            ScrollByPixels((short)(newValue - oldValue));
                                        }
                                    } else {
                                        TrackControl(gVScrollBar, local, (ControlActionUPP)ScrollAction);
                                    }
                                    /* One settle redraw after the whole
                                       gesture (click-and-hold or drag)
                                       completes, not per-tick - see
                                       ScrollByPixels for why per-tick
                                       redraws were removed. */
                                    ForceRedraw();
                                } else {
                                    Boolean isDoubleClick;

                                    isDoubleClick = (Boolean)(
                                        (event.when - gLastClickTime) <= kDoubleClickTicks &&
                                        abs(local.h - gLastClickPt.h) < kDoubleClickSlop &&
                                        abs(local.v - gLastClickPt.v) < kDoubleClickSlop);
                                    gLastClickTime = event.when;
                                    gLastClickPt = local;

                                    TEClick(local, (event.modifiers & shiftKey) != 0, gDoc.body);

                                    if (isDoubleClick) {
                                        Footnote *fn = FindFootnoteContainingOffset((**gDoc.body).selStart);
                                        Footnote *cm = FindCommentContainingOffset((**gDoc.body).selStart);
                                        if (fn)
                                            EditFootnote(fn);
                                        else if (cm)
                                            EditComment(cm);
                                    }
                                }
                            }
                            break;
                        case inDrag: {
                            Rect screenRect = qd.screenBits.bounds;
                            DragWindow(whichWindow, event.where, &screenRect);
                            break;
                        }
                        case inGrow: {
                            Rect limits;
                            long newSize;
                            SetRect(&limits, 200, 150, 2000, 2000);
                            newSize = GrowWindow(whichWindow, event.where, &limits);
                            if (newSize != 0) {
                                SizeWindow(whichWindow, LoWord(newSize), HiWord(newSize), true);
                                ResizeDocumentWindow();
                            }
                            break;
                        }
                        case inZoomIn:
                        case inZoomOut:
                            if (TrackBox(whichWindow, event.where, part)) {
                                SetPort(whichWindow);
                                ZoomWindow(whichWindow, part, true);
                                ResizeDocumentWindow();
                            }
                            break;
                        case inGoAway:
                            if (TrackGoAway(whichWindow, event.where))
                                gDone = true;
                            break;
                    }
                    break;
                }
                case keyDown:
                case autoKey: {
                    char c = (char)(event.message & charCodeMask);
                    if (event.modifiers & cmdKey) {
                        if (event.what == keyDown) {
                            AdjustMenus();
                            HandleMenuChoice(MenuKey(c));
                        }
                    } else if (c == '\r' || c == kEnterKey) {
                        HandleReturnKey();
                        CheckDocumentSize();
                    } else {
                        long pos = (**gDoc.body).selStart;
                        long lenBefore = TextLength();
                        TEKey(c, gDoc.body);
                        AdjustMarkersAfterEdit(pos, TextLength() - lenBefore);
                        gDoc.dirty = true;
                        UpdateScrollBarRange();
                        CheckDocumentSize();
                    }
                    break;
                }
                case updateEvt: {
                    WindowPtr w = (WindowPtr)event.message;
                    BeginUpdate(w);
                    SetPort(w);
                    EraseRect(&w->portRect);
                    TEUpdate(&w->portRect, gDoc.body);
                    DrawControls(w);
                    DrawGrowIcon(w);
                    EndUpdate(w);
                    break;
                }
                case activateEvt: {
                    WindowPtr w = (WindowPtr)event.message;
                    SetPort(w);
                    if (event.modifiers & activeFlag) {
                        TEActivate(gDoc.body);
                        HiliteControl(gVScrollBar, 0);
                    } else {
                        TEDeactivate(gDoc.body);
                        HiliteControl(gVScrollBar, 255);
                    }
                    break;
                }
            }
        } else {
            TEIdle(gDoc.body);
        }
    }
}
