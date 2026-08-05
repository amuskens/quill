#ifndef WORDPROC_H
#define WORDPROC_H

#include <Types.h>
#include <TextEdit.h>
#include <Files.h>
#include <string.h>

#define kMaxFootnotes 200
#define kMaxComments 200

/* A distinct, app-specific error code (well clear of any real classic Mac
   OS error range) so DoImport can show a size-specific message instead of
   the generic "could not be imported" - returned by both ReadDocumentFromRtf
   and ReadDocumentFromDoc when the source file is rejected by size up
   front, or when inserting its content would cross their respective
   character-count ceilings partway through parsing. Shared here (rather
   than defined separately in rtf.h/doc.h) since app.c's DoImport needs to
   check for it regardless of which reader produced it. */
#define kImportTooLargeErr (-9000)

/* CopyCStringToPascal/CopyPascalStringToC are Carbon-only in this toolchain;
   these are the 68k-safe equivalents. */
static void CToPascal(const char *src, unsigned char *dst)
{
    unsigned char len = 0;
    while (src[len] != 0 && len < 255) len++;
    dst[0] = len;
    memcpy(dst + 1, src, len);
}

static void PascalToC(const unsigned char *src, char *dst)
{
    unsigned char len = src[0];
    memcpy(dst, src + 1, len);
    dst[len] = 0;
}

typedef enum { kFormatDocx = 0, kFormatRtf, kFormatDoc, kFormatQuill } SaveFormat;

/* Shared shape for both footnotes and comments - both are "a marker sitting
   at some offset in the body, with an out-of-line text blob attached to
   it," differing only in what the marker looks like and where the text
   ends up on export. Comments reuse this rather than getting a parallel
   struct with the same four fields under different eyes. */
typedef struct {
    short number;       /* 1-based number, in insertion order (comments: an internal id, not shown on-screen) */
    long  anchorOffset;  /* char offset in body text where the marker sits */
    short markerLen;     /* length in chars of the visible in-body marker */
    Handle text;          /* body text, C string (not Pascal) */
} Footnote;

/* The in-body comment marker is a single fixed glyph (not a number - a
   comment isn't "referenced" the way a footnote is, so there's nothing to
   number on-screen) - the lozenge, chosen because it's not already used
   for anything else in this app (bullets use kBulletMarkerByte). Colored
   at insert time (yellow for comments, blue for footnotes - see
   DoInsertComment/DoInsertFootnote in app.c) so the two are visually
   distinguishable at a glance on a color Mac; QuickDraw quietly collapses
   both to black on a B&W port, so no separate monochrome handling is
   needed. */
#define kCommentMarkerChar 0xD7 /* Mac OS Roman lozenge, U+25CA */

typedef struct {
    TEHandle body;                 /* styled TextEdit record for the document */
    WindowPtr window;
    Boolean haveFile;               /* true once Save/Save As has picked a location */
    FSSpec file;
    SaveFormat format;              /* which writer Save (not Save As) should use */
    short footnoteCount;
    Footnote *footnotes;            /* malloc'd for kMaxFootnotes entries once at startup - see CreateDocumentWindow */
    short commentCount;
    Footnote *comments;             /* malloc'd for kMaxComments entries once at startup - see CreateDocumentWindow */
    Boolean dirty;
} Document;

/* ---- fonts ----
   Shared by the Format menu (main.c) and the RTF font table (rtf.c).
   Times is first/default per product requirements. */
static const char *kFontNames[] = {
    "Times", "Geneva", "New York", "Helvetica", "Courier", "Monaco", "Palatino"
};
#define kFontCount 7

/* ---- paragraph styles ----
   There is no persistent per-paragraph metadata: a paragraph's style is
   derived from the (size, bold, italic, underline) of its own text whenever
   it's needed (on-screen highlighting, docx export). This piggybacks on
   TextEdit's own style-run tracking, which already survives every edit
   correctly, instead of hand-maintaining a parallel structure that could
   drift out of sync with insertions/deletions. The tradeoff: manually
   formatting plain text to match one of these exact triples will be
   misidentified as that style. */
typedef enum {
    pStyleNormal = 0,
    pStyleHeading1, pStyleHeading2, pStyleHeading3, pStyleHeading4,
    pStyleQuote, pStyleBibliography,
    pStylePlainText,
    kParaStyleCount
} ParaStyleKind;

typedef struct {
    short size;
    Boolean bold;
    Boolean italic;
    Boolean underline;
    /* true = classic TextEdit's own default font (systemFont, i.e. Chicago -
       whatever a brand-new, never-styled TE record would show), not this
       app's body font (Times). Only Plain Text uses this - everything else
       is explicitly the app's Times-based styling. This exists so "Plain
       Text" and "Normal" are genuinely distinguishable: without it they'd
       share the exact same (size,bold,italic,underline) triple and collapse
       into one indistinguishable style, the same way Heading 2 and Quote
       used to before Heading 2 picked up bold (see below). */
    Boolean systemFont;
} ParaStyleSpec;

/* Heading 2 and Quote used to be fully identical (12pt italic, not bold) -
   not anymore: Heading 2 is now bold+italic, so bold alone tells them apart.
   Kept as a reference for why DetectParaStyle's matching logic has to
   compare every field, not just size - two styles differing in only one
   dimension (bold, or now systemFont for Plain Text vs Normal) still need
   to be distinguishable. */
static const ParaStyleSpec kParaStyleSpecs[kParaStyleCount] = {
    {12, false, false, false, false}, /* Normal */
    {14, true,  false, false, false}, /* Heading 1 */
    {12, true,  true,  false, false}, /* Heading 2 */
    {12, true,  false, true,  false}, /* Heading 3 */
    {12, false, true,  true,  false}, /* Heading 4 */
    {12, false, true,  false, false}, /* Quote */
    {10, false, false, false, false}, /* Bibliography */
    {12, false, false, false, true }, /* Plain Text - systemFont, not Times */
};

/* docx w:styleId values (no spaces; Heading1-4 match Word's own built-in IDs) */
static const char *kParaStyleIds[kParaStyleCount] = {
    "Normal", "Heading1", "Heading2", "Heading3", "Heading4", "Quote", "Bibliography", "PlainText"
};

/* docx w:name values (Word's own display names for heading 1-4) */
static const char *kParaStyleNames[kParaStyleCount] = {
    "Normal", "heading 1", "heading 2", "heading 3", "heading 4", "Quote", "Bibliography", "Plain Text"
};

static ParaStyleKind DetectParaStyle(short size, Boolean bold, Boolean italic, Boolean underline, Boolean systemFont)
{
    short i;
    for (i = 1; i < kParaStyleCount; i++) {
        if (kParaStyleSpecs[i].size == size &&
            kParaStyleSpecs[i].bold == bold &&
            kParaStyleSpecs[i].italic == italic &&
            kParaStyleSpecs[i].underline == underline &&
            kParaStyleSpecs[i].systemFont == systemFont)
            return (ParaStyleKind)i;
    }
    return pStyleNormal;
}

/* ---- bullet/numbered lists ----
   Like paragraph styles, list membership is derived from literal marker
   text at the start of a paragraph ("<bullet> " or "<digits>. ") rather
   than a parallel structure. Numbers are assigned once, not dynamically
   renumbered if items are added/removed/reordered later. */
#define kBulletMarkerByte 0xA5 /* Mac OS Roman bullet, U+2022 */

typedef enum { kListNone = 0, kListBullet, kListNumbered } ListKind;

static ListKind DetectListMarker(const char *text, long paraLen, long *outMarkerLen)
{
    if (paraLen >= 2 && (unsigned char)text[0] == kBulletMarkerByte && text[1] == ' ') {
        *outMarkerLen = 2;
        return kListBullet;
    }
    {
        long i = 0;
        while (i < paraLen && text[i] >= '0' && text[i] <= '9')
            i++;
        if (i > 0 && i + 1 < paraLen && text[i] == '.' && text[i + 1] == ' ') {
            *outMarkerLen = i + 2;
            return kListNumbered;
        }
    }
    *outMarkerLen = 0;
    return kListNone;
}

/* ---- Mac OS Roman (0x80-0xFF) -> Unicode code points ----
   Shared by docx.c (UTF-8 XML text) and rtf.c (\uNNNN escapes). */
static const unsigned short kMacRomanHigh[128] = {
    0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1, /* 80-87 */
    0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8, /* 88-8F */
    0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3, /* 90-97 */
    0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC, /* 98-9F */
    0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF, /* A0-A7 */
    0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8, /* A8-AF */
    0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211, /* B0-B7 */
    0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8, /* B8-BF */
    0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB, /* C0-C7 */
    0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153, /* C8-CF */
    0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA, /* D0-D7 */
    0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02, /* D8-DF */
    0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1, /* E0-E7 */
    0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4, /* E8-EF */
    0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC, /* F0-F7 */
    0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7  /* F8-FF */
};

/* docx.c */
OSErr WriteDocumentAsDocx(Document *doc, const FSSpec *dest);

/* rtf.c - also used for the .doc "Save As", which writes RTF content under
   a .doc name (see rtf.c's header comment for why). includeComments: see
   rtf.h's comment - true for .doc, false (with a prior warning) for .rtf. */
OSErr WriteDocumentAsRtf(Document *doc, const FSSpec *dest, Boolean includeComments);

/* native.c - Quill's own .qdoc format: a small custom XML dialect, the only
   one of the four save formats that's also readable, for resuming a
   writing session with full fidelity (exact run styling, footnotes, zoom,
   alignment - nothing derived/approximated like the export formats). */
OSErr WriteDocumentAsQuill(Document *doc, const FSSpec *dest, short zoomPercent);
OSErr ReadDocumentFromQuill(Document *doc, const FSSpec *src, short *outZoomPercent);

#endif
