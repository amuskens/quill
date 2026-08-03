#include "docx.h"
#include "zip.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- growable output buffer ---- */

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

static void AppendUTF8CodePoint(DynBuf *b, unsigned long cp)
{
    unsigned char buf[3];
    if (cp < 0x80) {
        buf[0] = (unsigned char)cp;
        DBAppend(b, (char *)buf, 1);
    } else if (cp < 0x800) {
        buf[0] = (unsigned char)(0xC0 | (cp >> 6));
        buf[1] = (unsigned char)(0x80 | (cp & 0x3F));
        DBAppend(b, (char *)buf, 2);
    } else {
        buf[0] = (unsigned char)(0xE0 | (cp >> 12));
        buf[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (unsigned char)(0x80 | (cp & 0x3F));
        DBAppend(b, (char *)buf, 3);
    }
}

/* Appends text as escaped, UTF-8 encoded XML character data. */
static void DBAppendXMLText(DynBuf *b, const char *s, long n)
{
    long i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '&': DBAppendStr(b, "&amp;"); break;
            case '<': DBAppendStr(b, "&lt;"); break;
            case '>': DBAppendStr(b, "&gt;"); break;
            case '"': DBAppendStr(b, "&quot;"); break;
            default:
                if (c < 0x20) {
                    /* skip other control chars; \r is handled by the caller as a paragraph break */
                } else if (c < 0x80) {
                    DBAppend(b, (char *)&c, 1);
                } else {
                    AppendUTF8CodePoint(b, kMacRomanHigh[c - 0x80]);
                }
        }
    }
}

/* ---- snapshotting the styled TextEdit run table into plain memory ---- */

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

static void EmitRunProps(DynBuf *out, RunSnap *rs)
{
    Str255 fname;
    char cname[256];
    char num[32];

    DBAppendStr(out, "<w:rPr>");
    if (rs->face & bold) DBAppendStr(out, "<w:b/>");
    if (rs->face & italic) DBAppendStr(out, "<w:i/>");
    if (rs->face & underline) DBAppendStr(out, "<w:u w:val=\"single\"/>");

    GetFontName(rs->font, fname);
    PascalToC((const unsigned char *)fname, cname);
    if (cname[0]) {
        DBAppendStr(out, "<w:rFonts w:ascii=\"");
        DBAppendXMLText(out, cname, (long)strlen(cname));
        DBAppendStr(out, "\" w:hAnsi=\"");
        DBAppendXMLText(out, cname, (long)strlen(cname));
        DBAppendStr(out, "\"/>");
    }

    sprintf(num, "%d", (int)(rs->size * 2));
    DBAppendStr(out, "<w:sz w:val=\"");
    DBAppendStr(out, num);
    DBAppendStr(out, "\"/><w:szCs w:val=\"");
    DBAppendStr(out, num);
    DBAppendStr(out, "\"/>");
    DBAppendStr(out, "</w:rPr>");
}

static void EmitTextRun(DynBuf *out, RunSnap *rs, const char *text, long len)
{
    if (len <= 0)
        return;
    DBAppendStr(out, "<w:r>");
    EmitRunProps(out, rs);
    DBAppendStr(out, "<w:t xml:space=\"preserve\">");
    DBAppendXMLText(out, text, len);
    DBAppendStr(out, "</w:t></w:r>");
}

static void EmitFootnoteReferenceRun(DynBuf *out, short number)
{
    char num[16];
    sprintf(num, "%d", (int)number);
    DBAppendStr(out, "<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr>"
                      "<w:footnoteReference w:id=\"");
    DBAppendStr(out, num);
    DBAppendStr(out, "\"/></w:r>");
}

static short FindRunIndexAt(RunSnap *runs, long nRuns, long offset)
{
    short idx = 0;
    while (idx < nRuns - 1 && runs[idx + 1].startChar <= offset)
        idx++;
    return idx;
}

/* Emits <w:r> runs for [start,end), splicing in real footnote references
   in place of the in-body marker text. Assumes an enclosing <w:p> is
   already open. */
static void EmitParagraphContent(DynBuf *out, Document *doc, RunSnap *runs, long nRuns,
                                  char *text, long start, long end)
{
    long pos = start;
    long segStart = start;
    short runIdx = FindRunIndexAt(runs, nRuns, start);

    while (pos < end) {
        Footnote *fn;

        while (runIdx < nRuns - 1 && pos >= runs[runIdx + 1].startChar) {
            EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);
            segStart = pos;
            runIdx++;
        }

        fn = FindFootnoteAtOffset(doc, pos);
        if (fn) {
            EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);
            EmitFootnoteReferenceRun(out, fn->number);
            pos += fn->markerLen;
            segStart = pos;
            continue;
        }

        pos++;
    }

    while (runIdx < nRuns - 1 && pos >= runs[runIdx + 1].startChar)
        runIdx++;
    if (pos > segStart)
        EmitTextRun(out, &runs[runIdx], text + segStart, pos - segStart);
}

/* alignWord: "left"/"center"/"right", or NULL for the document default (left) */
static void EmitParagraphProps(DynBuf *out, ParaStyleKind styleKind, ListKind listKind, const char *alignWord)
{
    if (styleKind == pStyleNormal && listKind == kListNone && alignWord == NULL)
        return;

    DBAppendStr(out, "<w:pPr>");
    if (styleKind != pStyleNormal) {
        DBAppendStr(out, "<w:pStyle w:val=\"");
        DBAppendStr(out, kParaStyleIds[styleKind]);
        DBAppendStr(out, "\"/>");
    }
    if (listKind != kListNone) {
        DBAppendStr(out, "<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"");
        DBAppendStr(out, listKind == kListBullet ? "1" : "2");
        DBAppendStr(out, "\"/></w:numPr>");
    }
    if (alignWord != NULL) {
        DBAppendStr(out, "<w:jc w:val=\"");
        DBAppendStr(out, alignWord);
        DBAppendStr(out, "\"/>");
    }
    DBAppendStr(out, "</w:pPr>");
}

/* Paragraph-oriented pass over the document: each paragraph gets its own
   <w:pPr> (heading/quote/bibliography style, list numbering) derived from
   a literal marker prefix (lists) and the run style at its content start
   (headings/quote/bibliography) - see the comment in wordproc.h. */
static void BuildDocumentXml(Document *doc, DynBuf *out)
{
    RunSnap *runs;
    long nRuns, textLen;
    char *text;
    long paraStart;
    short just = (**doc->body).just;
    const char *alignWord = (just == teJustCenter) ? "center" : (just == teJustRight) ? "right" : NULL;

    runs = SnapshotRuns(doc->body, &nRuns, &textLen, &text);

    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    DBAppendStr(out, "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    DBAppendStr(out, "<w:body>");

    paraStart = 0;
    for (;;) {
        long paraEnd = paraStart;
        long markerLen;
        long contentStart;
        ListKind listKind;
        ParaStyleKind styleKind;
        short runIdx;

        while (paraEnd < textLen && text[paraEnd] != '\r')
            paraEnd++;

        listKind = DetectListMarker(text + paraStart, paraEnd - paraStart, &markerLen);
        contentStart = paraStart + markerLen;

        runIdx = FindRunIndexAt(runs, nRuns, contentStart < paraEnd ? contentStart : paraStart);
        styleKind = DetectParaStyle(runs[runIdx].size,
                                     (Boolean)((runs[runIdx].face & bold) != 0),
                                     (Boolean)((runs[runIdx].face & italic) != 0),
                                     (Boolean)((runs[runIdx].face & underline) != 0));

        DBAppendStr(out, "<w:p>");
        EmitParagraphProps(out, styleKind, listKind, alignWord);
        EmitParagraphContent(out, doc, runs, nRuns, text, contentStart, paraEnd);
        DBAppendStr(out, "</w:p>");

        if (paraEnd >= textLen)
            break;
        paraStart = paraEnd + 1;
    }

    DBAppendStr(out, "<w:sectPr/>");
    DBAppendStr(out, "</w:body></w:document>");

    free(runs);
    free(text);
}

static void BuildNumberingXml(DynBuf *out)
{
    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    DBAppendStr(out, "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");

    DBAppendStr(out, "<w:abstractNum w:abstractNumId=\"0\"><w:lvl w:ilvl=\"0\">"
                      "<w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"\xE2\x80\xA2\"/>"
                      "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"360\" w:hanging=\"360\"/></w:pPr>"
                      "<w:rPr><w:rFonts w:ascii=\"Symbol\" w:hAnsi=\"Symbol\" w:hint=\"default\"/></w:rPr>"
                      "</w:lvl></w:abstractNum>");
    DBAppendStr(out, "<w:abstractNum w:abstractNumId=\"1\"><w:lvl w:ilvl=\"0\">"
                      "<w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.\"/>"
                      "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"360\" w:hanging=\"360\"/></w:pPr>"
                      "</w:lvl></w:abstractNum>");

    DBAppendStr(out, "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>");
    DBAppendStr(out, "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>");

    DBAppendStr(out, "</w:numbering>");
}

static void BuildFootnotesXml(Document *doc, DynBuf *out)
{
    short i;

    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    DBAppendStr(out, "<w:footnotes xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    DBAppendStr(out, "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>");
    DBAppendStr(out, "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>");

    for (i = 0; i < doc->footnoteCount; i++) {
        Footnote *fn = &doc->footnotes[i];
        char idbuf[16];
        char *body;
        long bodyLen;

        HLock(fn->text);
        body = *fn->text;
        bodyLen = GetHandleSize(fn->text);

        sprintf(idbuf, "%d", (int)fn->number);
        DBAppendStr(out, "<w:footnote w:id=\"");
        DBAppendStr(out, idbuf);
        DBAppendStr(out, "\"><w:p><w:pPr><w:pStyle w:val=\"FootnoteText\"/></w:pPr>");
        DBAppendStr(out, "<w:r><w:rPr><w:rStyle w:val=\"FootnoteReference\"/></w:rPr><w:footnoteRef/></w:r>");
        DBAppendStr(out, "<w:r><w:t xml:space=\"preserve\"> ");
        DBAppendXMLText(out, body, bodyLen);
        DBAppendStr(out, "</w:t></w:r></w:p></w:footnote>");

        HUnlock(fn->text);
    }

    DBAppendStr(out, "</w:footnotes>");
}

static void BuildStylesXml(DynBuf *out)
{
    short i;

    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    DBAppendStr(out, "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">");
    DBAppendStr(out, "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>");
    DBAppendStr(out, "<w:style w:type=\"character\" w:styleId=\"FootnoteReference\">"
                      "<w:name w:val=\"footnote reference\"/><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr></w:style>");
    DBAppendStr(out, "<w:style w:type=\"paragraph\" w:styleId=\"FootnoteText\">"
                      "<w:name w:val=\"footnote text\"/><w:rPr><w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/></w:rPr></w:style>");

    for (i = 1; i < kParaStyleCount; i++) {
        char num[16];

        DBAppendStr(out, "<w:style w:type=\"paragraph\" w:styleId=\"");
        DBAppendStr(out, kParaStyleIds[i]);
        DBAppendStr(out, "\"><w:name w:val=\"");
        DBAppendStr(out, kParaStyleNames[i]);
        DBAppendStr(out, "\"/><w:basedOn w:val=\"Normal\"/>");

        if (i >= pStyleHeading1 && i <= pStyleHeading4) {
            sprintf(num, "%d", (int)(i - pStyleHeading1));
            DBAppendStr(out, "<w:pPr><w:outlineLvl w:val=\"");
            DBAppendStr(out, num);
            DBAppendStr(out, "\"/></w:pPr>");
        }

        DBAppendStr(out, "<w:rPr>");
        if (kParaStyleSpecs[i].bold) DBAppendStr(out, "<w:b/>");
        if (kParaStyleSpecs[i].italic) DBAppendStr(out, "<w:i/>");
        if (kParaStyleSpecs[i].underline) DBAppendStr(out, "<w:u w:val=\"single\"/>");
        sprintf(num, "%d", (int)(kParaStyleSpecs[i].size * 2));
        DBAppendStr(out, "<w:sz w:val=\"");
        DBAppendStr(out, num);
        DBAppendStr(out, "\"/><w:szCs w:val=\"");
        DBAppendStr(out, num);
        DBAppendStr(out, "\"/></w:rPr></w:style>");
    }

    DBAppendStr(out, "</w:styles>");
}

static void BuildContentTypesXml(Document *doc, DynBuf *out)
{
    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    DBAppendStr(out, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">");
    DBAppendStr(out, "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>");
    DBAppendStr(out, "<Default Extension=\"xml\" ContentType=\"application/xml\"/>");
    DBAppendStr(out, "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>");
    DBAppendStr(out, "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>");
    DBAppendStr(out, "<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>");
    if (doc->footnoteCount > 0)
        DBAppendStr(out, "<Override PartName=\"/word/footnotes.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml\"/>");
    DBAppendStr(out, "</Types>");
}

static void BuildRootRelsXml(DynBuf *out)
{
    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    DBAppendStr(out, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    DBAppendStr(out, "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>");
    DBAppendStr(out, "</Relationships>");
}

static void BuildDocumentRelsXml(Document *doc, DynBuf *out)
{
    DBAppendStr(out, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    DBAppendStr(out, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    DBAppendStr(out, "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>");
    DBAppendStr(out, "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" Target=\"numbering.xml\"/>");
    if (doc->footnoteCount > 0)
        DBAppendStr(out, "<Relationship Id=\"rId3\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes\" Target=\"footnotes.xml\"/>");
    DBAppendStr(out, "</Relationships>");
}

OSErr WriteDocumentAsDocx(Document *doc, const FSSpec *dest)
{
    ZipWriter zw;
    DynBuf b;
    OSErr err;

    err = ZipCreate(dest, &zw);
    if (err != noErr)
        return err;

    DBInit(&b);
    BuildContentTypesXml(doc, &b);
    err = ZipAddEntry(&zw, "[Content_Types].xml", b.data, b.len);
    DBFree(&b);
    if (err != noErr) { ZipClose(&zw); return err; }

    DBInit(&b);
    BuildRootRelsXml(&b);
    err = ZipAddEntry(&zw, "_rels/.rels", b.data, b.len);
    DBFree(&b);
    if (err != noErr) { ZipClose(&zw); return err; }

    DBInit(&b);
    BuildDocumentRelsXml(doc, &b);
    err = ZipAddEntry(&zw, "word/_rels/document.xml.rels", b.data, b.len);
    DBFree(&b);
    if (err != noErr) { ZipClose(&zw); return err; }

    DBInit(&b);
    BuildStylesXml(&b);
    err = ZipAddEntry(&zw, "word/styles.xml", b.data, b.len);
    DBFree(&b);
    if (err != noErr) { ZipClose(&zw); return err; }

    DBInit(&b);
    BuildNumberingXml(&b);
    err = ZipAddEntry(&zw, "word/numbering.xml", b.data, b.len);
    DBFree(&b);
    if (err != noErr) { ZipClose(&zw); return err; }

    if (doc->footnoteCount > 0) {
        DBInit(&b);
        BuildFootnotesXml(doc, &b);
        err = ZipAddEntry(&zw, "word/footnotes.xml", b.data, b.len);
        DBFree(&b);
        if (err != noErr) { ZipClose(&zw); return err; }
    }

    DBInit(&b);
    BuildDocumentXml(doc, &b);
    err = ZipAddEntry(&zw, "word/document.xml", b.data, b.len);
    DBFree(&b);
    if (err != noErr) { ZipClose(&zw); return err; }

    return ZipClose(&zw);
}
