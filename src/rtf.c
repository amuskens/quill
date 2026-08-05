#include "rtf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- growable output buffer (mirrors docx.c's; kept private per-TU) ---- */

typedef struct {
    char *data;
    unsigned long len;
    unsigned long cap;
} DynBuf;

static void DBInit(DynBuf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void DBFree(DynBuf *b)
{
    if (b->data)
        free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static void DBEnsure(DynBuf *b, unsigned long extra)
{
    if (b->len + extra + 1 > b->cap) {
        unsigned long newCap = b->cap ? b->cap * 2 : 1024;
        while (newCap < b->len + extra + 1)
            newCap *= 2;
        b->data = (char *)realloc(b->data, newCap);
        b->cap = newCap;
    }
}

static void DBAppend(DynBuf *b, const char *s, unsigned long n)
{
    DBEnsure(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void DBAppendStr(DynBuf *b, const char *s)
{
    DBAppend(b, s, strlen(s));
}

/* Escapes RTF-special chars and encodes non-ASCII via \uNNNN (with a
   single '?' fallback char, matching the \uc1 declared in the header). */
static void DBAppendRtfText(DynBuf *b, const char *s, long n)
{
    long i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '{' || c == '}') {
            DBAppend(b, "\\", 1);
            DBAppend(b, (char *)&c, 1);
        } else if (c == '\t') {
            DBAppendStr(b, "\\tab ");
        } else if (c < 0x20) {
            /* skip other control chars; \r is handled by the caller */
        } else if (c < 0x80) {
            DBAppend(b, (char *)&c, 1);
        } else {
            char buf[16];
            long cp = (long)kMacRomanHigh[c - 0x80];
            if (cp >= 0x8000)
                cp -= 0x10000; /* RTF \u wants a signed 16-bit value */
            sprintf(buf, "\\u%ld?", cp);
            DBAppendStr(b, buf);
        }
    }
}

/* ---- snapshotting the styled TextEdit run table (mirrors docx.c's) ---- */

typedef struct {
    long startChar;
    short font;
    Style face;
    short size;
} RunSnap;

static RunSnap *SnapshotRuns(TEHandle teh, long *outCount, long *outTextLen, char **outText)
{
    CharsHandle th;
    TEStyleHandle sh;
    TEStyleRec *rec;
    STHandle tab;
    STElement *table;
    RunSnap *snap;
    long nRuns, textLen, i;
    char *text;

    th = TEGetText(teh);
    textLen = GetHandleSize((Handle)th);
    text = (char *)malloc(textLen > 0 ? textLen : 1);
    HLock((Handle)th);
    memcpy(text, *th, textLen);
    HUnlock((Handle)th);

    sh = TEGetStyleHandle(teh);
    HLock((Handle)sh);
    rec = *sh;
    nRuns = rec->nRuns;
    tab = rec->styleTab;
    HLock((Handle)tab);
    table = *tab;

    snap = (RunSnap *)malloc(sizeof(RunSnap) * (nRuns > 0 ? nRuns : 1));
    for (i = 0; i < nRuns; i++) {
        StyleRun sr = rec->runs[i];
        snap[i].startChar = sr.startChar;
        if (i < nRuns - 1) {
            STElement e = table[sr.styleIndex];
            snap[i].font = e.stFont;
            snap[i].face = e.stFace;
            snap[i].size = e.stSize;
        } else {
            snap[i].font = 0;
            snap[i].face = 0;
            snap[i].size = 12;
        }
    }

    HUnlock((Handle)tab);
    HUnlock((Handle)sh);

    *outCount = nRuns;
    *outTextLen = textLen;
    *outText = text;
    return snap;
}

static Footnote *FindFootnoteAtOffset(Document *doc, long offset)
{
    short i;
    for (i = 0; i < doc->footnoteCount; i++) {
        if (doc->footnotes[i].anchorOffset == offset)
            return &doc->footnotes[i];
    }
    return NULL;
}

static Footnote *FindCommentAtOffset(Document *doc, long offset)
{
    short i;
    for (i = 0; i < doc->commentCount; i++) {
        if (doc->comments[i].anchorOffset == offset)
            return &doc->comments[i];
    }
    return NULL;
}

static short FontTableIndex(short fontID)
{
    Str255 fname;
    char cname[256];
    short i;

    GetFontName(fontID, fname);
    PascalToC((const unsigned char *)fname, cname);

    for (i = 0; i < kFontCount; i++) {
        if (strcmp(cname, kFontNames[i]) == 0)
            return i;
    }
    return 0; /* fall back to Times */
}

static void EmitRunProps(DynBuf *out, RunSnap *rs)
{
    char buf[32];

    DBAppendStr(out, "\\plain");
    sprintf(buf, "\\f%d", FontTableIndex(rs->font));
    DBAppendStr(out, buf);
    if (rs->face & bold) DBAppendStr(out, "\\b");
    if (rs->face & italic) DBAppendStr(out, "\\i");
    if (rs->face & underline) DBAppendStr(out, "\\ul");
    sprintf(buf, "\\fs%d ", (int)(rs->size * 2));
    DBAppendStr(out, buf);
}

static void EmitTextRun(DynBuf *out, RunSnap *rs, const char *text, long len)
{
    if (len <= 0)
        return;
    EmitRunProps(out, rs);
    DBAppendRtfText(out, text, len);
}

/* Footnotes are represented inline: Word (and most readers) auto-insert the
   superscript reference mark wherever this group appears in the main flow,
   using \chftn for both the in-place mark and the mark repeated before the
   note's own text at the bottom of the page. */
static void EmitFootnoteGroup(DynBuf *out, Handle text)
{
    char *body;
    long bodyLen;

    DBAppendStr(out, "{\\footnote\\pard\\plain\\fs20\\up6\\chftn\\tab ");
    HLock(text);
    body = *text;
    bodyLen = GetHandleSize(text);
    DBAppendRtfText(out, body, bodyLen);
    HUnlock(text);
    DBAppendStr(out, "}");
}

/* \chatn inserts an annotation reference mark (auto-numbered by the reader)
   at this point in the main text; the \*-prefixed destinations right after
   it carry the id/author/text - all marked ignorable-if-unrecognized, which
   is the conservative, widely-compatible way real-world RTF (including
   Word's own output) represents comments, rather than requiring every
   reader to specifically understand \annotation. Only used for .doc export
   (includeComments) - .rtf export drops comments entirely (see
   WriteDocumentAsRtf's header comment for why). */
static void EmitCommentGroup(DynBuf *out, short number, Handle text)
{
    char *body;
    long bodyLen;
    char idbuf[16];

    sprintf(idbuf, "%d", (int)number);
    DBAppendStr(out, "\\chatn{\\*\\atnid ");
    DBAppendStr(out, idbuf);
    DBAppendStr(out, "}{\\*\\atnauthor Quill}{\\*\\annotation\\pard\\plain\\fs20 ");
    HLock(text);
    body = *text;
    bodyLen = GetHandleSize(text);
    DBAppendRtfText(out, body, bodyLen);
    HUnlock(text);
    DBAppendStr(out, "}");
}

/* Unlike docx export, list markers are kept as plain visible text here
   (bullet character / "N. ") rather than converted to real RTF list
   numbering (\listtable) - simpler, and RTF is the secondary/legacy export
   target in this app (see README). */
static void BuildRtfBody(Document *doc, DynBuf *out, Boolean includeComments)
{
    RunSnap *runs;
    long nRuns, textLen, pos, runIdx, segStart;
    char *text;
    short just = (**doc->body).just;
    const char *alignWord = (just == teJustCenter) ? "\\qc" : (just == teJustRight) ? "\\qr" : "\\ql";
    short i;

    runs = SnapshotRuns(doc->body, &nRuns, &textLen, &text);

    DBAppendStr(out, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl");
    for (i = 0; i < kFontCount; i++) {
        char buf[32];
        sprintf(buf, "{\\f%d\\fnil ", i);
        DBAppendStr(out, buf);
        DBAppendStr(out, kFontNames[i]);
        DBAppendStr(out, ";}");
    }
    DBAppendStr(out, "}\n\\viewkind4\\pard");
    DBAppendStr(out, alignWord);
    DBAppendStr(out, "\\f0\\fs24\n");

    pos = 0;
    runIdx = 0;
    segStart = 0;

    while (pos < textLen) {
        Footnote *fn;
        Footnote *cm;

        while (runIdx < nRuns - 1 && pos >= runs[runIdx + 1].startChar) {
            EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);
            segStart = pos;
            runIdx++;
        }

        fn = FindFootnoteAtOffset(doc, pos);
        if (fn) {
            EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);
            EmitFootnoteGroup(out, fn->text);
            pos += fn->markerLen;
            segStart = pos;
            continue;
        }

        cm = FindCommentAtOffset(doc, pos);
        if (cm) {
            EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);
            if (includeComments)
                EmitCommentGroup(out, cm->number, cm->text);
            pos += cm->markerLen;
            segStart = pos;
            continue;
        }

        if (text[pos] == '\r') {
            EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);
            DBAppendStr(out, "\\par\\pard");
            DBAppendStr(out, alignWord);
            DBAppendStr(out, " ");
            pos++;
            segStart = pos;
            continue;
        }

        pos++;
    }

    while (runIdx < nRuns - 1 && pos >= runs[runIdx + 1].startChar)
        runIdx++;
    if (pos > segStart)
        EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);

    DBAppendStr(out, "\\par}");

    free(runs);
    free(text);
}

OSErr WriteDocumentAsRtf(Document *doc, const FSSpec *dest, Boolean includeComments)
{
    DynBuf b;
    OSErr err;
    short refNum;
    long count;

    DBInit(&b);
    BuildRtfBody(doc, &b, includeComments);

    FSpDelete((FSSpecPtr)dest); /* ignore "doesn't exist" error */

    err = FSpCreate((FSSpecPtr)dest, '????', '????', smSystemScript);
    if (err != noErr) {
        DBFree(&b);
        return err;
    }

    err = FSpOpenDF((FSSpecPtr)dest, fsWrPerm, &refNum);
    if (err != noErr) {
        DBFree(&b);
        return err;
    }

    count = (long)b.len;
    err = FSWrite(refNum, &count, b.data);

    FSClose(refNum);
    DBFree(&b);
    return err;
}

/* ==================== RTF reader (best-effort import) ====================

   A hand-rolled RTF tokenizer - not a full implementation of the spec, just
   enough to recover what DocImport promises: plain text, bold/italic/
   underline, font, and size. Paragraph alignment, lists/tables, footnotes,
   and embedded objects are all deliberately not attempted; unrecognized
   control words are simply ignored (per the RTF spec's own fallback rule),
   which is what makes "best effort" viable at all - a reader doesn't need
   to understand everything to safely skip past what it doesn't.

   Groups ({...}) scope formatting - entering one saves the current
   bold/italic/underline/font/size state, leaving it restores that state -
   so the parser keeps a small stack. A destination group (one whose first
   control word is meant to be consumed by something other than the main
   document body - \fonttbl, \colortbl, \footnote, \pict, an explicit \*
   marker, etc.) has its content skipped outright: skipDepth records the
   depth such a group started at, and everything is discarded until that
   depth is left again. \fonttbl is the one destination actually parsed
   (not skipped) - the file's own \fN font indices only mean anything
   relative to *that* file's font table, so recovering real font names
   (rather than assuming \f0 means Times, \f1 means Geneva, etc. the way
   this app's own writer numbers things) matters for opening real
   Word-authored files, not just round-tripping this app's own output. */

typedef struct {
    Boolean bold, italic, underline;
    short fontIdx;   /* index into the *file's own* font table */
    short fontSize;  /* points */
} RtfRunState;

#define kRtfMaxDepth    64
#define kRtfMaxFonts    32
#define kRtfFontNameLen 64

typedef struct {
    Document *doc;
    long insertPos;

    RtfRunState cur;
    RtfRunState stack[kRtfMaxDepth];
    short depth;

    short skipDepth; /* -1 = not skipping; else the depth a skip destination started at */

    Boolean inFontTable;
    short fontTableDepth;
    short curFontSlot; /* -1 = not currently inside a per-font sub-group */
    char fontNames[kRtfMaxFonts][kRtfFontNameLen];
    short fontNameLen[kRtfMaxFonts];

    DynBuf pending;
    RtfRunState pendingState;

    short ucSkip; /* plain fallback chars to skip after \uN - \ucN sets this, default 1 */

    Boolean tooLarge; /* set once insertPos would cross kRtfMaxImportChars; the main loop bails out when this is set */
} RtfParser;

/* Classic TextEdit's selStart/selEnd/teLength are 16-bit INTEGERs (see the
   README's "Document size limit") - inserting text that pushes the total
   past ~32,767 characters doesn't fail cleanly, it wraps/corrupts those
   fields. RTF markup overhead means a file's byte size is a poor proxy for
   the plain-text character count it'll actually produce, so this is
   checked against the running insertPos as text is actually inserted,
   not against the source file's size up front. Kept a safety margin below
   the hard ceiling, matching the app's own kSizeWarnThreshold. */
#define kRtfMaxImportChars 28000

static int RtfHexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* Reverse of kMacRomanHigh, for \uNNNN escapes in imported RTF - best
   effort, not a full Unicode-to-MacRoman transcoder, same spirit as the
   writer's own one-directional use of the table. */
static char UnicodeToMacRomanByte(long cp)
{
    short i;
    if (cp >= 0 && cp < 0x80)
        return (char)cp;
    for (i = 0; i < 128; i++) {
        if (kMacRomanHigh[i] == (unsigned short)cp)
            return (char)(0x80 + i);
    }
    return '?';
}

static Boolean RtfWordIs(const char *w, int wlen, const char *lit)
{
    long litLen = (long)strlen(lit);
    return (Boolean)(litLen == wlen && strncmp(w, lit, wlen) == 0);
}

/* Flushes accumulated plain text as one styled TEInsert/TESetStyle, using
   pendingState (the formatting in effect when this run started, not
   whatever cur has drifted to by the time of the flush - a control word
   changing state calls this first, precisely to keep those from mixing). */
static void RtfFlush(RtfParser *rp)
{
    TextStyle ts;
    Str255 pname;
    short fontID;

    if (rp->pending.len == 0)
        return;

    if (rp->tooLarge) {
        rp->pending.len = 0; /* already past the limit - discard instead of inserting further */
        return;
    }
    if (rp->insertPos + (long)rp->pending.len > kRtfMaxImportChars) {
        rp->tooLarge = true;
        rp->pending.len = 0;
        return;
    }

    TESetSelect(rp->insertPos, rp->insertPos, rp->doc->body);
    TEInsert(rp->pending.data, (long)rp->pending.len, rp->doc->body);

    if (rp->pendingState.fontIdx >= 0 && rp->pendingState.fontIdx < kRtfMaxFonts &&
        rp->fontNameLen[rp->pendingState.fontIdx] > 0) {
        CToPascal(rp->fontNames[rp->pendingState.fontIdx], pname);
    } else {
        CToPascal("Times", pname);
    }
    GetFNum(pname, &fontID);

    memset(&ts, 0, sizeof(ts));
    ts.tsFont = fontID;
    ts.tsSize = (rp->pendingState.fontSize > 0) ? rp->pendingState.fontSize : 12;
    ts.tsFace = 0;
    if (rp->pendingState.bold) ts.tsFace |= bold;
    if (rp->pendingState.italic) ts.tsFace |= italic;
    if (rp->pendingState.underline) ts.tsFace |= underline;

    TESetSelect(rp->insertPos, rp->insertPos + (long)rp->pending.len, rp->doc->body);
    TESetStyle(doFont | doSize | doFace, &ts, false, rp->doc->body);

    rp->insertPos += (long)rp->pending.len;
    rp->pending.len = 0;
    if (rp->pending.data) rp->pending.data[0] = 0;
}

static void RtfEnsureState(RtfParser *rp)
{
    if (rp->pending.len > 0 &&
        (rp->pendingState.bold != rp->cur.bold ||
         rp->pendingState.italic != rp->cur.italic ||
         rp->pendingState.underline != rp->cur.underline ||
         rp->pendingState.fontIdx != rp->cur.fontIdx ||
         rp->pendingState.fontSize != rp->cur.fontSize)) {
        RtfFlush(rp);
    }
    rp->pendingState = rp->cur;
}

static void RtfAppendByte(RtfParser *rp, char c)
{
    RtfEnsureState(rp);
    DBAppend(&rp->pending, &c, 1);
}

static void RtfPushGroup(RtfParser *rp)
{
    RtfFlush(rp); /* don't let a run span a group boundary */
    if (rp->depth < kRtfMaxDepth)
        rp->stack[rp->depth] = rp->cur;
    rp->depth++;
}

static void RtfPopGroup(RtfParser *rp)
{
    RtfFlush(rp);
    if (rp->depth > 0) {
        rp->depth--;
        if (rp->depth < kRtfMaxDepth)
            rp->cur = rp->stack[rp->depth];
    }
    if (rp->skipDepth >= 0 && rp->depth < rp->skipDepth)
        rp->skipDepth = -1;
    if (rp->inFontTable && rp->depth < rp->fontTableDepth)
        rp->inFontTable = false;
    if (rp->curFontSlot >= 0 && rp->depth < rp->fontTableDepth + 1)
        rp->curFontSlot = -1;
}

OSErr ReadDocumentFromRtf(Document *doc, const FSSpec *src)
{
    short refNum;
    long fileLen, count;
    char *buf;
    OSErr err;
    RtfParser rp;
    const char *p, *end;
    Boolean atGroupStart;

    err = FSpOpenDF((FSSpecPtr)src, fsRdPerm, &refNum);
    if (err != noErr)
        return err;
    err = GetEOF(refNum, &fileLen);
    if (err != noErr) { FSClose(refNum); return err; }

    /* This whole file gets malloc'd into one buffer up front (simplest way
       to run a single-pass scanner over it). This sanity ceiling only
       exists to refuse a clearly-doomed attempt (a many-megabyte file on
       this app's much smaller heap - see the SIZE resource); it is
       deliberately NOT trying to predict whether the file's *content* will
       fit within RTF's own ~28,000-character import ceiling
       (kRtfMaxImportChars) - that's enforced separately, per character, as
       text is actually inserted (see the tooLarge handling below), and
       lets a bigger source file still contribute a full, truncated-at-the-
       limit import rather than being rejected outright just for being
       large. If the allocation itself fails despite passing this check,
       the NULL check right after is the real, precise safety net. */
    if (fileLen > 1800000L) {
        FSClose(refNum);
        return kImportTooLargeErr;
    }

    buf = (char *)malloc(fileLen > 0 ? fileLen + 1 : 1);
    if (!buf) {
        FSClose(refNum);
        return memFullErr;
    }
    count = fileLen;
    err = FSRead(refNum, &count, buf);
    FSClose(refNum);
    if (err != noErr) { free(buf); return err; }
    buf[fileLen] = 0;

    if (fileLen < 5 || strncmp(buf, "{\\rtf", 5) != 0) {
        free(buf);
        return paramErr;
    }

    memset(&rp, 0, sizeof(rp));
    rp.doc = doc;
    rp.insertPos = 0;
    rp.cur.bold = false;
    rp.cur.italic = false;
    rp.cur.underline = false;
    rp.cur.fontIdx = 0;
    rp.cur.fontSize = 12;
    rp.pendingState = rp.cur;
    rp.depth = 0;
    rp.skipDepth = -1;
    rp.inFontTable = false;
    rp.fontTableDepth = -1;
    rp.curFontSlot = -1;
    rp.ucSkip = 1;
    DBInit(&rp.pending);

    p = buf;
    end = buf + fileLen;
    atGroupStart = false;

    while (p < end) {
        char c = *p;

        if (rp.tooLarge)
            break;

        if (c == '{') {
            RtfPushGroup(&rp);
            atGroupStart = true;
            p++;
            continue;
        }
        if (c == '}') {
            RtfPopGroup(&rp);
            atGroupStart = false;
            p++;
            continue;
        }
        if (c == '\r' || c == '\n') {
            p++; /* raw source formatting, not document content */
            continue;
        }

        if (c == '\\') {
            const char *wordStart;
            int wlen;
            long num;
            Boolean hasNum, neg;

            p++;
            if (p >= end) break;

            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
                /* control symbol: exactly one char, no delimiter to eat */
                char sym = *p;
                p++;

                if (sym == '\'') {
                    if (p + 2 <= end) {
                        char v = (char)(RtfHexVal(p[0]) * 16 + RtfHexVal(p[1]));
                        p += 2;
                        if (rp.skipDepth < 0) {
                            if (rp.inFontTable && rp.curFontSlot >= 0) {
                                if (rp.fontNameLen[rp.curFontSlot] < kRtfFontNameLen - 1)
                                    rp.fontNames[rp.curFontSlot][rp.fontNameLen[rp.curFontSlot]++] = v;
                            } else {
                                RtfAppendByte(&rp, v);
                            }
                        }
                    }
                } else if (sym == '*') {
                    if (atGroupStart && rp.skipDepth < 0)
                        rp.skipDepth = rp.depth;
                } else if (sym == '\\' || sym == '{' || sym == '}') {
                    if (rp.skipDepth < 0) {
                        if (rp.inFontTable && rp.curFontSlot >= 0) {
                            if (rp.fontNameLen[rp.curFontSlot] < kRtfFontNameLen - 1)
                                rp.fontNames[rp.curFontSlot][rp.fontNameLen[rp.curFontSlot]++] = sym;
                        } else {
                            RtfAppendByte(&rp, sym);
                        }
                    }
                } else if (sym == '~') {
                    if (rp.skipDepth < 0) RtfAppendByte(&rp, ' ');
                } else if (sym == '-' || sym == '_') {
                    if (rp.skipDepth < 0) RtfAppendByte(&rp, '-');
                }
                /* else: unrecognized control symbol, ignore */

                atGroupStart = false;
                continue;
            }

            wordStart = p;
            while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')))
                p++;
            wlen = (int)(p - wordStart);

            neg = false;
            hasNum = false;
            num = 0;
            if (p < end && *p == '-') { neg = true; p++; }
            while (p < end && *p >= '0' && *p <= '9') {
                num = num * 10 + (*p - '0');
                p++;
                hasNum = true;
            }
            if (neg) num = -num;
            if (p < end && *p == ' ')
                p++; /* a single trailing space is the delimiter, consumed */

            if (rp.inFontTable && atGroupStart && RtfWordIs(wordStart, wlen, "f") && hasNum) {
                short slot = (short)num;
                if (slot >= 0 && slot < kRtfMaxFonts) {
                    rp.curFontSlot = slot;
                    rp.fontNameLen[slot] = 0;
                }
            } else if (atGroupStart && RtfWordIs(wordStart, wlen, "fonttbl")) {
                rp.inFontTable = true;
                rp.fontTableDepth = rp.depth;
            } else if (atGroupStart && rp.skipDepth < 0 &&
                       (RtfWordIs(wordStart, wlen, "colortbl") ||
                        RtfWordIs(wordStart, wlen, "stylesheet") ||
                        RtfWordIs(wordStart, wlen, "info") ||
                        RtfWordIs(wordStart, wlen, "footnote") ||
                        RtfWordIs(wordStart, wlen, "pict") ||
                        RtfWordIs(wordStart, wlen, "object") ||
                        RtfWordIs(wordStart, wlen, "header") ||
                        RtfWordIs(wordStart, wlen, "headerf") ||
                        RtfWordIs(wordStart, wlen, "headerl") ||
                        RtfWordIs(wordStart, wlen, "headerr") ||
                        RtfWordIs(wordStart, wlen, "footer") ||
                        RtfWordIs(wordStart, wlen, "footerf") ||
                        RtfWordIs(wordStart, wlen, "footerl") ||
                        RtfWordIs(wordStart, wlen, "footerr") ||
                        RtfWordIs(wordStart, wlen, "generator"))) {
                rp.skipDepth = rp.depth;
            } else if (rp.skipDepth < 0) {
                if (RtfWordIs(wordStart, wlen, "par") || RtfWordIs(wordStart, wlen, "line")) {
                    RtfAppendByte(&rp, '\r');
                } else if (RtfWordIs(wordStart, wlen, "tab")) {
                    RtfAppendByte(&rp, '\t');
                } else if (RtfWordIs(wordStart, wlen, "b")) {
                    rp.cur.bold = (Boolean)(!hasNum || num != 0);
                } else if (RtfWordIs(wordStart, wlen, "i")) {
                    rp.cur.italic = (Boolean)(!hasNum || num != 0);
                } else if (RtfWordIs(wordStart, wlen, "ul")) {
                    rp.cur.underline = true;
                } else if (RtfWordIs(wordStart, wlen, "ulnone")) {
                    rp.cur.underline = false;
                } else if (RtfWordIs(wordStart, wlen, "plain")) {
                    rp.cur.bold = false;
                    rp.cur.italic = false;
                    rp.cur.underline = false;
                    rp.cur.fontSize = 12;
                } else if (RtfWordIs(wordStart, wlen, "fs") && hasNum) {
                    rp.cur.fontSize = (short)(num / 2);
                    if (rp.cur.fontSize < 1) rp.cur.fontSize = 12;
                } else if (RtfWordIs(wordStart, wlen, "f") && hasNum) {
                    rp.cur.fontIdx = (short)num;
                } else if (RtfWordIs(wordStart, wlen, "uc") && hasNum) {
                    rp.ucSkip = (short)num;
                } else if (RtfWordIs(wordStart, wlen, "u") && hasNum) {
                    char mb = UnicodeToMacRomanByte(num);
                    short skip = rp.ucSkip;
                    RtfAppendByte(&rp, mb);
                    while (skip > 0 && p < end) {
                        if (*p == '\\' && p + 1 < end && p[1] == '\'') {
                            p += 4; /* \'hh fallback representation */
                        } else if (*p == '{' || *p == '}' || *p == '\\') {
                            break; /* don't eat real control structure */
                        } else {
                            p++;
                        }
                        skip--;
                    }
                }
                /* else: unrecognized control word, ignore (already consumed) */
            }

            atGroupStart = false;
            continue;
        }

        /* plain literal character */
        if (rp.skipDepth < 0) {
            if (rp.inFontTable && rp.curFontSlot >= 0) {
                if (c == ';') {
                    rp.curFontSlot = -1;
                } else if (rp.fontNameLen[rp.curFontSlot] < kRtfFontNameLen - 1) {
                    rp.fontNames[rp.curFontSlot][rp.fontNameLen[rp.curFontSlot]++] = c;
                }
            } else {
                RtfAppendByte(&rp, c);
            }
        }
        atGroupStart = false;
        p++;
    }

    RtfFlush(&rp);
    DBFree(&rp.pending);
    free(buf);

    if (rp.tooLarge) {
        /* Keep whatever was inserted before the ceiling was hit. The caller
           will warn the user that the import was truncated. */
        TESetSelect(0, 0, doc->body);
        return kImportTooLargeErr;
    }

    TESetSelect(0, 0, doc->body);
    return noErr;
}
