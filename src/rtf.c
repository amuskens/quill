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

/* Unlike docx export, list markers are kept as plain visible text here
   (bullet character / "N. ") rather than converted to real RTF list
   numbering (\listtable) - simpler, and RTF is the secondary/legacy export
   target in this app (see README). */
static void BuildRtfBody(Document *doc, DynBuf *out)
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

OSErr WriteDocumentAsRtf(Document *doc, const FSSpec *dest)
{
    DynBuf b;
    OSErr err;
    short refNum;
    long count;

    DBInit(&b);
    BuildRtfBody(doc, &b);

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
