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
#include "native.h"

enum {
    mApple = 128, mFile = 129, mEdit = 130, mFormat = 131, mInsert = 132,
    mStyle = 133, mAlign = 134, mZoom = 135
};

enum { iAbout = 1 };

enum {
    iNew = 1, iOpen = 2, iSave = 3,
    iSaveAsQuill = 4, iSaveAsDocx = 5, iSaveAsRtf = 6, iSaveAsDoc = 7,
    iQuit = 9
};

enum { iUndo = 1, iCut = 3, iCopy = 4, iPaste = 5, iClear = 6 };

enum {
    iBold = 1, iItalic = 2, iUnderline = 3,
    iFontFirst = 5, iFontLast = 11,
    iSizeFirst = 13, iSizeLast = 18
};

enum { iInsertFootnote = 1 };

/* Style menu: items 1..kParaStyleCount map directly to ParaStyleKind 0..N-1;
   item kParaStyleCount+1 is a divider. */
enum { iBulletList = 9, iNumberedList = 10 };

/* item 1 is the permanently-disabled "Whole Document:" label - see SetupMenus */
enum { iAlignLeft = 2, iAlignCenter = 3, iAlignRight = 4, iAlignJustify = 5 };

#define kFootnoteDialogID 200
#define kAboutAlertID     201
#define kErrorAlertID     202
#define kFootnoteTextItem 4
#define kDefaultSize      12
#define kDoubleClickSlop  5
#define kDoubleClickTicks 30 /* ~0.5s at 60 ticks/sec; GetDblTime() isn't available for this target */
#define kMaxTouchedParagraphs 200
#define kEnterKey         0x03 /* numeric-keypad Enter; treated the same as Return */

static const short kSizes[] = { 9, 10, 12, 14, 18, 24 };
#define kSizeCount 6

static const short kZoomLevels[] = { 50, 100, 150, 200, 300, 400 };
#define kZoomCount 6

static Document gDoc;
static Boolean gDone = false;
static MenuHandle gAppleMenu, gFileMenu, gEditMenu, gFormatMenu, gInsertMenu, gStyleMenu, gAlignMenu, gZoomMenu;
static short gBodyFontID;
static short gZoomPercent = 100;
static unsigned long gLastClickTime = 0;
static Point gLastClickPt = { 0, 0 };

static void ToolboxInit(void);
static void SetupMenus(void);
static void CreateDocumentWindow(void);
static void ResizeDocumentWindow(void);
static void ForceRedraw(void);
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
static Boolean DoSaveAs(SaveFormat fmt);
static void DoSave(void);
static void DoInsertFootnote(void);
static void ClearFootnotes(void);
static void ToggleFace(Style bit);
static void SetFontByName(const char *name);
static void SetSize(short size);
static void Fail(const char *msg);

static short ScaleSize(short logicalSize);
static short UnscaleSize(short actualSize);
static void RescaleDocument(short newZoomPercent);

static long TextLength(void);
static void GetParagraphRange(long offset, long *pStart, long *pEnd);
static void GetSelectionParagraphRange(long *outStart, long *outEnd);
static short CollectTouchedParagraphs(long selStart, long selEnd, long *starts, long *ends);
static void ApplyParaStyle(ParaStyleKind kind);
static void ToggleList(ListKind kind);
static void AdjustFootnotesAfterEdit(long editPos, long delta);
static Footnote *FindFootnoteContainingOffset(long offset);
static Boolean RunFootnoteDialog(Str255 text);
static void EditFootnote(Footnote *fn);
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

static void CreateDocumentWindow(void)
{
    Rect windBounds, viewRect;
    Str255 pname;
    TextStyle ts;

    SetRect(&windBounds, 60, 60, 500, 380);
    gDoc.window = NewWindow(NULL, &windBounds, "\pUntitled", true,
                             documentProc, (WindowPtr)-1L, true, 0);
    SetPort(gDoc.window);

    viewRect = gDoc.window->portRect;
    InsetRect(&viewRect, 4, 4);

    gDoc.body = TEStyleNew(&viewRect, &viewRect);
    TEAutoView(true, gDoc.body);

    memset(&ts, 0, sizeof(ts));
    CToPascal(kFontNames[0], pname); /* Times, per product requirements */
    GetFNum(pname, &gBodyFontID);
    ts.tsFont = gBodyFontID;
    ts.tsSize = ScaleSize(kDefaultSize);
    TESetStyle(doFont | doSize | doFace, &ts, false, gDoc.body);

    gDoc.haveFile = false;
    gDoc.format = kFormatDocx;
    gDoc.dirty = false;
    gDoc.footnoteCount = 0;
}

static void ResizeDocumentWindow(void)
{
    Rect viewRect = gDoc.window->portRect;
    InsetRect(&viewRect, 4, 4);
    (**gDoc.body).destRect = viewRect;
    (**gDoc.body).viewRect = viewRect;
    TECalText(gDoc.body);
    ForceRedraw();
}

/* Synchronously repaints the document window right now, instead of just
   invalidating and waiting for the next update event - used after actions
   (zoom, alignment, New) whose whole point is visible-immediately feedback,
   since InvalRect only *schedules* a redraw for whenever the event loop
   next gets around to it. */
static void ForceRedraw(void)
{
    SetPort(gDoc.window);
    EraseRect(&gDoc.window->portRect);
    TEUpdate(&gDoc.window->portRect, gDoc.body);
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

static void DoNew(void)
{
    TextStyle ts;

    TESetSelect(0, 32767, gDoc.body);
    TEDelete(gDoc.body);
    ClearFootnotes();
    gDoc.haveFile = false;
    gDoc.format = kFormatDocx;
    gDoc.dirty = false;
    gZoomPercent = 100;
    TESetAlignment(teJustLeft, gDoc.body);

    memset(&ts, 0, sizeof(ts));
    ts.tsFont = gBodyFontID;
    ts.tsSize = ScaleSize(kDefaultSize);
    TESetStyle(doFont | doSize | doFace, &ts, false, gDoc.body);

    TECalText(gDoc.body);
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

    savedZoom = gZoomPercent;
    if (savedZoom != 100) RescaleDocument(100);
    err = (fmt == kFormatDocx) ? WriteDocumentAsDocx(&gDoc, &gDoc.file)
                                : WriteDocumentAsRtf(&gDoc, &gDoc.file);
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

static void DoSave(void)
{
    OSErr err;

    if (!gDoc.haveFile) {
        DoSaveAs(kFormatDocx);
        return;
    }

    err = WriteCurrentDocument(gDoc.format);

    if (err != noErr) {
        Fail("Could not save the document.");
        return;
    }
    gDoc.dirty = false;
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
    ForceRedraw();
    SetWTitle(gDoc.window, reply.sfFile.name);
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

/* Zoom is implemented by literally rescaling every run's stored point size
   (proportionally, from the current zoom to the new one) rather than a
   separate visual-only transform - see the README for why (classic
   TextEdit/QuickDraw has no coordinate-scaling primitive short of an
   offscreen GWorld blit, which is much more machinery for this pass).
   Save/Save As always rescale to 100% first so exported files hold true
   logical sizes no matter the current zoom. */
static void RescaleDocument(short newZoomPercent)
{
    TEStyleHandle sh;
    TEStyleRec *rec;
    STHandle tab;
    STElement *table;
    long nRuns, i;
    long *starts;
    short *sizes;
    long selStart, selEnd;

    if (newZoomPercent == gZoomPercent)
        return;

    selStart = (**gDoc.body).selStart;
    selEnd = (**gDoc.body).selEnd;

    sh = TEGetStyleHandle(gDoc.body);
    HLock((Handle)sh);
    rec = *sh;
    nRuns = rec->nRuns;
    tab = rec->styleTab;
    HLock((Handle)tab);
    table = *tab;

    starts = (long *)malloc(sizeof(long) * (nRuns > 0 ? nRuns : 1));
    sizes = (short *)malloc(sizeof(short) * (nRuns > 0 ? nRuns : 1));

    for (i = 0; i < nRuns; i++) {
        StyleRun sr = rec->runs[i];
        starts[i] = sr.startChar;
        sizes[i] = (i < nRuns - 1) ? table[sr.styleIndex].stSize : 12;
    }

    HUnlock((Handle)tab);
    HUnlock((Handle)sh);

    for (i = 0; i < nRuns - 1; i++) {
        long rangeStart = starts[i];
        long rangeEnd = starts[i + 1];
        TextStyle ts;

        if (rangeEnd <= rangeStart)
            continue;

        memset(&ts, 0, sizeof(ts));
        ts.tsSize = (short)(((long)sizes[i] * newZoomPercent + gZoomPercent / 2) / gZoomPercent);

        TESetSelect(rangeStart, rangeEnd, gDoc.body);
        TESetStyle(doSize, &ts, false, gDoc.body);
    }

    free(starts);
    free(sizes);

    /* nRuns <= 1 means there's no real text yet (brand-new empty document):
       the per-run loop above never runs, so without this, text typed after
       zooming - before any explicit formatting - would come out at the old,
       unscaled size. TEContinuousStyle on the (necessarily collapsed, since
       there's no text) selection reports the "next typed character" style;
       TESetStyle on that same collapsed selection is the documented way to
       update it. Only safe to do when there are no real runs: otherwise the
       caret could be sitting inside/at the edge of a run this function
       already rescaled above, and this would double-scale it. */
    if (nRuns <= 1) {
        short qmode = doSize;
        TextStyle qts;
        if (TEContinuousStyle(&qmode, &qts, gDoc.body)) {
            TextStyle nts;
            memset(&nts, 0, sizeof(nts));
            nts.tsSize = (short)(((long)qts.tsSize * newZoomPercent + gZoomPercent / 2) / gZoomPercent);
            TESetStyle(doSize, &nts, false, gDoc.body);
        }
    }

    gZoomPercent = newZoomPercent;
    TESetSelect(selStart, selEnd, gDoc.body);
    TECalText(gDoc.body);
    ForceRedraw();
}

static void AdjustFootnotesAfterEdit(long editPos, long delta)
{
    short i;
    if (delta == 0)
        return;
    for (i = 0; i < gDoc.footnoteCount; i++) {
        if (gDoc.footnotes[i].anchorOffset >= editPos)
            gDoc.footnotes[i].anchorOffset += delta;
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

static Boolean RunFootnoteDialog(Str255 text)
{
    DialogPtr dlg;
    short itemType;
    Handle itemH;
    Rect box;
    short itemHit;

    dlg = GetNewDialog(kFootnoteDialogID, NULL, (WindowPtr)-1L);
    if (!dlg)
        return false;

    GetDialogItem(dlg, kFootnoteTextItem, &itemType, &itemH, &box);
    SetDialogItemText(itemH, text);
    SelectDialogItemText(dlg, kFootnoteTextItem, 0, 32767);

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

    if (RunFootnoteDialog(text)) {
        SetHandleSize(fn->text, text[0]);
        if (text[0] > 0)
            BlockMove(&text[1], *fn->text, text[0]);
        gDoc.dirty = true;
    }
}

/* The in-body footnote marker is shrunk to ~65% of the surrounding text's
   actual size as a superscript approximation: classic TextEdit has no
   baseline-shift/superscript concept at all (checked - there's truly no
   such style bit or call in this toolchain), so a real raised, small
   reference mark isn't achievable without a custom text renderer. The
   exported .docx already uses a genuine <w:vertAlign val="superscript">
   run, independent of this on-screen approximation. */
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
    if (!RunFootnoteDialog(text))
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

    TEInsert(markerBuf, markerLen, gDoc.body);
    AdjustFootnotesAfterEdit(fn->anchorOffset, markerLen);
    TESetSelect(fn->anchorOffset, fn->anchorOffset + markerLen, gDoc.body);
    TESetStyle(doSize, &markerStyle, true, gDoc.body);
    TESetSelect(fn->anchorOffset + markerLen, fn->anchorOffset + markerLen, gDoc.body);

    gDoc.footnoteCount++;
    gDoc.dirty = true;
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

/* Every paragraph style uses the default font (Times) and 12pt; only the
   face bits differ (see wordproc.h). Applying a style leaves the affected
   paragraph(s) selected so the change is immediately visible. */
static void ApplyParaStyle(ParaStyleKind kind)
{
    long rangeStart, rangeEnd;
    TextStyle ts;

    GetSelectionParagraphRange(&rangeStart, &rangeEnd);
    TESetSelect(rangeStart, rangeEnd, gDoc.body);

    memset(&ts, 0, sizeof(ts));
    ts.tsFont = gBodyFontID;
    ts.tsSize = ScaleSize(kParaStyleSpecs[kind].size);
    ts.tsFace = 0;
    if (kParaStyleSpecs[kind].bold) ts.tsFace |= bold;
    if (kParaStyleSpecs[kind].italic) ts.tsFace |= italic;
    if (kParaStyleSpecs[kind].underline) ts.tsFace |= underline;

    TESetStyle(doFont | doSize | doFace, &ts, true, gDoc.body);
    gDoc.dirty = true;
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
            AdjustFootnotesAfterEdit(starts[i], -existingMarkerLen);
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
            AdjustFootnotesAfterEdit(starts[i], markerLen);
        }
    }

    gDoc.dirty = true;
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
}

static void SetSize(short size)
{
    TextStyle ts;
    memset(&ts, 0, sizeof(ts));
    ts.tsSize = ScaleSize(size);
    TESetStyle(doSize, &ts, true, gDoc.body);
    gDoc.dirty = true;
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

        mode2 = doFace | doSize;
        cont2 = TEContinuousStyle(&mode2, &ts2, gDoc.body);
        kind = cont2
                   ? DetectParaStyle(UnscaleSize(ts2.tsSize),
                                      (Boolean)((ts2.tsFace & bold) != 0),
                                      (Boolean)((ts2.tsFace & italic) != 0),
                                      (Boolean)((ts2.tsFace & underline) != 0))
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
        case iNew:          DoNew(); break;
        case iOpen:          DoOpen(); break;
        case iSave:          DoSave(); break;
        case iSaveAsQuill:   DoSaveAs(kFormatQuill); break;
        case iSaveAsDocx:    DoSaveAs(kFormatDocx); break;
        case iSaveAsRtf:     DoSaveAs(kFormatRtf); break;
        case iSaveAsDoc:     DoSaveAs(kFormatDoc); break;
        case iQuit:          gDone = true; break;
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
            AdjustFootnotesAfterEdit(pos, TextLength() - lenBefore);
            gDoc.dirty = true;
            break;
        case iCopy:
            TECopy(gDoc.body);
            break;
        case iPaste:
            pos = (**gDoc.body).selStart;
            lenBefore = TextLength();
            TEPaste(gDoc.body);
            AdjustFootnotesAfterEdit(pos, TextLength() - lenBefore);
            gDoc.dirty = true;
            break;
        case iClear:
            pos = (**gDoc.body).selStart;
            lenBefore = TextLength();
            TEDelete(gDoc.body);
            AdjustFootnotesAfterEdit(pos, TextLength() - lenBefore);
            gDoc.dirty = true;
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
    AdjustFootnotesAfterEdit(pos, TextLength() - lenBefore);
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
        AdjustFootnotesAfterEdit(pStart, pStart - pEnd);
        gDoc.dirty = true;
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
        AdjustFootnotesAfterEdit(newParaStart, newMarkerLen);
        TESetSelect(newParaStart + newMarkerLen, newParaStart + newMarkerLen, gDoc.body);
    }
}

void RunApp(void)
{
    EventRecord event;

    ToolboxInit();
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
                                Boolean isDoubleClick;

                                GlobalToLocal(&local);

                                isDoubleClick = (Boolean)(
                                    (event.when - gLastClickTime) <= kDoubleClickTicks &&
                                    abs(local.h - gLastClickPt.h) < kDoubleClickSlop &&
                                    abs(local.v - gLastClickPt.v) < kDoubleClickSlop);
                                gLastClickTime = event.when;
                                gLastClickPt = local;

                                TEClick(local, (event.modifiers & shiftKey) != 0, gDoc.body);

                                if (isDoubleClick) {
                                    Footnote *fn = FindFootnoteContainingOffset((**gDoc.body).selStart);
                                    if (fn)
                                        EditFootnote(fn);
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
                    } else {
                        long pos = (**gDoc.body).selStart;
                        long lenBefore = TextLength();
                        TEKey(c, gDoc.body);
                        AdjustFootnotesAfterEdit(pos, TextLength() - lenBefore);
                        gDoc.dirty = true;
                    }
                    break;
                }
                case updateEvt: {
                    WindowPtr w = (WindowPtr)event.message;
                    BeginUpdate(w);
                    SetPort(w);
                    EraseRect(&w->portRect);
                    TEUpdate(&w->portRect, gDoc.body);
                    EndUpdate(w);
                    break;
                }
                case activateEvt: {
                    WindowPtr w = (WindowPtr)event.message;
                    SetPort(w);
                    if (event.modifiers & activeFlag)
                        TEActivate(gDoc.body);
                    else
                        TEDeactivate(gDoc.body);
                    break;
                }
            }
        } else {
            TEIdle(gDoc.body);
        }
    }
}
