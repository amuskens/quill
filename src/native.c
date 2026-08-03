#include "native.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
   Quill's native ".qdoc" format: a small, custom XML dialect this app both
   writes AND reads back, unlike .docx/.rtf/.doc which are write-only export
   targets. It exists so a writing session can be resumed with full
   fidelity - exact per-run font/size/bold/italic/underline, footnotes
   (anchor position, marker length, number, body text), current zoom, and
   current alignment - none of it derived/approximated the way paragraph
   styles and lists are for export.

   Deliberately NOT a general XML file: the parser below is a small
   hand-rolled scanner tailored to exactly the shape this module itself
   writes (attributes always quoted, no nesting of same-named tags, no
   comments/CDATA/entities beyond the four escaped below). That's a
   reasonable trade given this format's only reader is this same file - a
   general/robust XML parser would be a lot of machinery for a format with
   exactly one producer.

   Shape:
     <?xml version="1.0" encoding="UTF-8"?>
     <quilldoc version="1" zoom="150" align="1">
     <body>
     <p><r f="Times" s="12" b="0" i="0" u="0">Some text</r></p>
     ...
     </body>
     <footnotes>
     <fn n="1" a="42" m="1">Footnote body text</fn>
     ...
     </footnotes>
     </quilldoc>

   Text is stored as raw Mac OS Roman bytes (this file is only ever read
   back by this same app on the same platform, so there's no need for the
   Unicode transcoding docx.c/rtf.c do) with just the four XML metacharacters
   escaped: & < > ". `align` is whatever teJustLeft/teJustCenter/teJustRight
   numerically is (0/1/-1), written straight through.
*/

/* ---- growable output buffer (mirrors docx.c/rtf.c's) ---- */

typedef struct {
    char *data;
    unsigned long len;
    unsigned long cap;
} DynBuf;

static void DBInit(DynBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static void DBFree(DynBuf *b)
{
    if (b->data) free(b->data);
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

static void DBAppendStr(DynBuf *b, const char *s) { DBAppend(b, s, strlen(s)); }

static void DBAppendEscaped(DynBuf *b, const char *s, long n)
{
    long i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '&': DBAppendStr(b, "&amp;"); break;
            case '<': DBAppendStr(b, "&lt;"); break;
            case '>': DBAppendStr(b, "&gt;"); break;
            case '"': DBAppendStr(b, "&quot;"); break;
            default:  DBAppend(b, (char *)&c, 1);
        }
    }
}

/* ---- snapshotting the styled TextEdit run table (mirrors docx.c/rtf.c) ---- */

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

static void EmitRun(DynBuf *out, RunSnap *rs, const char *text, long len)
{
    Str255 fname;
    char cname[256];

    if (len <= 0)
        return;

    GetFontName(rs->font, fname);
    PascalToC((const unsigned char *)fname, cname);

    DBAppendStr(out, "<r f=\"");
    DBAppendEscaped(out, cname, (long)strlen(cname));
    DBAppendStr(out, "\" s=\"");
    { char n[16]; sprintf(n, "%d", (int)rs->size); DBAppendStr(out, n); }
    DBAppendStr(out, "\" b=\"");
    DBAppendStr(out, (rs->face & bold) ? "1" : "0");
    DBAppendStr(out, "\" i=\"");
    DBAppendStr(out, (rs->face & italic) ? "1" : "0");
    DBAppendStr(out, "\" u=\"");
    DBAppendStr(out, (rs->face & underline) ? "1" : "0");
    DBAppendStr(out, "\">");
    DBAppendEscaped(out, text, len);
    DBAppendStr(out, "</r>");
}

static void BuildQuillXml(Document *doc, short zoomPercent, DynBuf *out)
{
    RunSnap *runs;
    long nRuns, textLen;
    char *text;
    long paraStart;
    short i;
    short just = (**doc->body).just;

    runs = SnapshotRuns(doc->body, &nRuns, &textLen, &text);

    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<quilldoc version=\"1\" zoom=\"");
    { char n[16]; sprintf(n, "%d", (int)zoomPercent); DBAppendStr(out, n); }
    DBAppendStr(out, "\" align=\"");
    { char n[16]; sprintf(n, "%d", (int)just); DBAppendStr(out, n); }
    DBAppendStr(out, "\">\n<body>\n");

    paraStart = 0;
    for (;;) {
        long paraEnd = paraStart;
        long segStart = paraStart;
        long runIdx;

        while (paraEnd < textLen && text[paraEnd] != '\r')
            paraEnd++;

        runIdx = 0;
        while (runIdx < nRuns - 1 && runs[runIdx + 1].startChar <= paraStart)
            runIdx++;

        DBAppendStr(out, "<p>");
        {
            long pos = paraStart;
            while (pos < paraEnd) {
                while (runIdx < nRuns - 1 && pos >= runs[runIdx + 1].startChar) {
                    EmitRun(out, &runs[runIdx], text + segStart, pos - segStart);
                    segStart = pos;
                    runIdx++;
                }
                pos++;
            }
            while (runIdx < nRuns - 1 && pos >= runs[runIdx + 1].startChar)
                runIdx++;
            if (pos > segStart)
                EmitRun(out, &runs[runIdx], text + segStart, pos - segStart);
        }
        DBAppendStr(out, "</p>\n");

        if (paraEnd >= textLen)
            break;
        paraStart = paraEnd + 1;
    }

    DBAppendStr(out, "</body>\n<footnotes>\n");
    for (i = 0; i < doc->footnoteCount; i++) {
        Footnote *fn = &doc->footnotes[i];
        char *body;
        long bodyLen;
        char n[16];

        DBAppendStr(out, "<fn n=\"");
        sprintf(n, "%d", (int)fn->number);
        DBAppendStr(out, n);
        DBAppendStr(out, "\" a=\"");
        sprintf(n, "%ld", fn->anchorOffset);
        DBAppendStr(out, n);
        DBAppendStr(out, "\" m=\"");
        sprintf(n, "%d", (int)fn->markerLen);
        DBAppendStr(out, n);
        DBAppendStr(out, "\">");

        HLock(fn->text);
        body = *fn->text;
        bodyLen = GetHandleSize(fn->text);
        DBAppendEscaped(out, body, bodyLen);
        HUnlock(fn->text);

        DBAppendStr(out, "</fn>\n");
    }
    DBAppendStr(out, "</footnotes>\n</quilldoc>\n");

    free(runs);
    free(text);
}

OSErr WriteDocumentAsQuill(Document *doc, const FSSpec *dest, short zoomPercent)
{
    DynBuf b;
    OSErr err;
    short refNum;
    long count;

    DBInit(&b);
    BuildQuillXml(doc, zoomPercent, &b);

    FSpDelete((FSSpecPtr)dest); /* ignore "doesn't exist" error */

    err = FSpCreate((FSSpecPtr)dest, '????', '????', smSystemScript);
    if (err != noErr) { DBFree(&b); return err; }

    err = FSpOpenDF((FSSpecPtr)dest, fsWrPerm, &refNum);
    if (err != noErr) { DBFree(&b); return err; }

    count = (long)b.len;
    err = FSWrite(refNum, &count, b.data);

    FSClose(refNum);
    DBFree(&b);
    return err;
}

/* ---- reader ---- */

static long ParseIntAttr(const char *tag, const char *attrName, long defaultVal)
{
    char pattern[16];
    const char *p;
    sprintf(pattern, "%s=\"", attrName);
    p = strstr(tag, pattern);
    if (!p)
        return defaultVal;
    return atol(p + strlen(pattern));
}

static void ParseStrAttr(const char *tag, const char *attrName, char *out, int outSize)
{
    char pattern[16];
    const char *p, *q;
    long len;

    sprintf(pattern, "%s=\"", attrName);
    out[0] = 0;
    p = strstr(tag, pattern);
    if (!p)
        return;
    p += strlen(pattern);
    q = strchr(p, '"');
    if (!q)
        return;
    len = q - p;
    if (len >= outSize)
        len = outSize - 1;
    memcpy(out, p, len);
    out[len] = 0;
}

/* Copies the tag's attribute region (start..the tag's own closing '>') into
   a bounded local buffer, so attribute lookups can't accidentally match
   something later in the file. */
static void ExtractTag(const char *tagStart, char *out, int outSize)
{
    const char *end = strchr(tagStart, '>');
    long len;
    if (!end) { out[0] = 0; return; }
    len = end - tagStart;
    if (len >= outSize)
        len = outSize - 1;
    memcpy(out, tagStart, len);
    out[len] = 0;
}

static long UnescapeInto(const char *src, long srcLen, char *dst)
{
    long i = 0, o = 0;
    while (i < srcLen) {
        if (src[i] == '&') {
            if (i + 5 <= srcLen && strncmp(src + i, "&amp;", 5) == 0) { dst[o++] = '&'; i += 5; }
            else if (i + 4 <= srcLen && strncmp(src + i, "&lt;", 4) == 0) { dst[o++] = '<'; i += 4; }
            else if (i + 4 <= srcLen && strncmp(src + i, "&gt;", 4) == 0) { dst[o++] = '>'; i += 4; }
            else if (i + 6 <= srcLen && strncmp(src + i, "&quot;", 6) == 0) { dst[o++] = '"'; i += 6; }
            else { dst[o++] = src[i++]; }
        } else {
            dst[o++] = src[i++];
        }
    }
    return o;
}

static long InsertRunFromTag(Document *doc, long insertPos, const char *tagStart, const char *contentStart, long contentLen)
{
    char tagBuf[256];
    char fontName[64];
    long size;
    Boolean b, ital, u;
    Str255 pname;
    short fontID;
    TextStyle ts;
    char *plain;
    long plainLen;

    ExtractTag(tagStart, tagBuf, sizeof(tagBuf));
    ParseStrAttr(tagBuf, "f", fontName, sizeof(fontName));
    size = ParseIntAttr(tagBuf, "s", 12);
    b = (Boolean)(ParseIntAttr(tagBuf, "b", 0) != 0);
    ital = (Boolean)(ParseIntAttr(tagBuf, "i", 0) != 0);
    u = (Boolean)(ParseIntAttr(tagBuf, "u", 0) != 0);

    if (contentLen <= 0)
        return insertPos;

    plain = (char *)malloc(contentLen);
    plainLen = UnescapeInto(contentStart, contentLen, plain);

    TESetSelect(insertPos, insertPos, doc->body);
    TEInsert(plain, plainLen, doc->body);
    free(plain);

    if (fontName[0])
        CToPascal(fontName, pname);
    else
        CToPascal("Times", pname);
    GetFNum(pname, &fontID);

    memset(&ts, 0, sizeof(ts));
    ts.tsFont = fontID;
    ts.tsSize = (short)size;
    ts.tsFace = 0;
    if (b) ts.tsFace |= bold;
    if (ital) ts.tsFace |= italic;
    if (u) ts.tsFace |= underline;

    TESetSelect(insertPos, insertPos + plainLen, doc->body);
    TESetStyle(doFont | doSize | doFace, &ts, false, doc->body);

    return insertPos + plainLen;
}

OSErr ReadDocumentFromQuill(Document *doc, const FSSpec *src, short *outZoomPercent)
{
    short refNum;
    long fileLen;
    char *buf;
    OSErr err;
    long count;
    const char *docTag;
    const char *bodyStart, *bodyEnd;
    const char *fnStart, *fnEnd;
    char tagBuf[256];
    const char *p;
    long insertPos = 0;
    Boolean firstPara = true;

    err = FSpOpenDF((FSSpecPtr)src, fsRdPerm, &refNum);
    if (err != noErr)
        return err;

    err = GetEOF(refNum, &fileLen);
    if (err != noErr) { FSClose(refNum); return err; }

    buf = (char *)malloc(fileLen + 1);
    count = fileLen;
    err = FSRead(refNum, &count, buf);
    FSClose(refNum);
    if (err != noErr) { free(buf); return err; }
    buf[fileLen] = 0;

    docTag = strstr(buf, "<quilldoc");
    bodyStart = strstr(buf, "<body>");
    bodyEnd = bodyStart ? strstr(bodyStart, "</body>") : NULL;
    if (!docTag || !bodyStart || !bodyEnd) {
        free(buf);
        return paramErr;
    }

    ExtractTag(docTag, tagBuf, sizeof(tagBuf));
    *outZoomPercent = (short)ParseIntAttr(tagBuf, "zoom", 100);
    TESetAlignment((short)ParseIntAttr(tagBuf, "align", teJustLeft), doc->body);

    p = bodyStart + strlen("<body>");
    while (p < bodyEnd) {
        const char *pStart = strstr(p, "<p>");
        const char *pEnd;
        const char *rp;

        if (!pStart || pStart >= bodyEnd)
            break;
        pEnd = strstr(pStart, "</p>");
        if (!pEnd || pEnd > bodyEnd)
            break;

        if (!firstPara) {
            TESetSelect(insertPos, insertPos, doc->body);
            TEInsert("\r", 1, doc->body);
            insertPos += 1;
        }
        firstPara = false;

        rp = pStart + strlen("<p>");
        while (rp < pEnd) {
            const char *rStart = strstr(rp, "<r ");
            const char *rTagEnd;
            const char *rContentEnd;

            if (!rStart || rStart >= pEnd)
                break;
            rTagEnd = strchr(rStart, '>');
            if (!rTagEnd || rTagEnd > pEnd)
                break;
            rContentEnd = strstr(rTagEnd, "</r>");
            if (!rContentEnd || rContentEnd > pEnd)
                break;

            insertPos = InsertRunFromTag(doc, insertPos, rStart, rTagEnd + 1, (long)(rContentEnd - (rTagEnd + 1)));
            rp = rContentEnd + strlen("</r>");
        }

        p = pEnd + strlen("</p>");
    }

    fnStart = strstr(bodyEnd, "<footnotes>");
    fnEnd = fnStart ? strstr(fnStart, "</footnotes>") : NULL;
    if (fnStart && fnEnd) {
        const char *fp = fnStart + strlen("<footnotes>");
        while (fp < fnEnd && doc->footnoteCount < kMaxFootnotes) {
            const char *fStart = strstr(fp, "<fn ");
            const char *fTagEnd;
            const char *fContentEnd;
            char fTagBuf[128];
            char *plain;
            long plainLen, contentLen;
            Footnote *fn;

            if (!fStart || fStart >= fnEnd)
                break;
            fTagEnd = strchr(fStart, '>');
            if (!fTagEnd || fTagEnd > fnEnd)
                break;
            fContentEnd = strstr(fTagEnd, "</fn>");
            if (!fContentEnd || fContentEnd > fnEnd)
                break;

            ExtractTag(fStart, fTagBuf, sizeof(fTagBuf));

            fn = &doc->footnotes[doc->footnoteCount];
            fn->number = (short)ParseIntAttr(fTagBuf, "n", doc->footnoteCount + 1);
            fn->anchorOffset = ParseIntAttr(fTagBuf, "a", 0);
            fn->markerLen = (short)ParseIntAttr(fTagBuf, "m", 0);

            contentLen = (long)(fContentEnd - (fTagEnd + 1));
            plain = (char *)malloc(contentLen > 0 ? contentLen : 1);
            plainLen = contentLen > 0 ? UnescapeInto(fTagEnd + 1, contentLen, plain) : 0;
            fn->text = NewHandle(plainLen);
            if (plainLen > 0)
                BlockMove(plain, *fn->text, plainLen);
            free(plain);

            doc->footnoteCount++;
            fp = fContentEnd + strlen("</fn>");
        }
    }

    free(buf);
    return noErr;
}
