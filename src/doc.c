#include "doc.h"
#include <stdlib.h>
#include <string.h>

/*
   Best-effort reader for real binary .doc/.dot files (Word 97-2003, which
   is also what modern Word still writes when you "Save As Word 97-2003
   Document"). Two independent formats are involved:

   1. OLE2 / Compound File Binary Format (CFBF) - the container. It's a
      whole small filesystem embedded in one file: 512-byte sectors, a FAT
      (File Allocation Table, an array of "next sector" links - the same
      linked-list-of-blocks idea as a real disk FAT) locating a directory
      stream, and the directory stream itself listing named streams (here,
      the ones that matter are "WordDocument" and "0Table"/"1Table") each
      with their own starting sector and size. Streams below a cutoff size
      (mini_stream_cutoff, always 4096 for files this reader was tested
      against) live in a "mini stream" with its own, smaller-sector FAT
      instead of the main one - deliberately NOT supported here (see
      kOle2MinStreamSize below), since a real document's WordDocument/table
      streams are essentially always well above that cutoff, and adding
      mini-stream random access would roughly double this file's size for
      a case that's never actually hit in practice.

   2. Word's own binary format within the "WordDocument" stream: a FIB
      (File Information Block) header, and - reached through it - a piece
      table (an array of "pieces", each a contiguous run of text stored
      either as 8-bit "compressed" (Windows-1252) or 16-bit Unicode
      (UTF-16LE), at some byte offset elsewhere in the same stream).
      Paragraph marks are literal \r (0x0D) characters *within* the piece
      text - the same convention this app's own TE buffers already use,
      which is why plain-text extraction doesn't need to do anything
      special for paragraph breaks.

   This reader recovers plain text ONLY: no bold/italic/underline/font, no
   paragraph styles, no footnotes/comments/fields/tables/pictures. Fields
   (mail-merge-style codes wrapped in 0x13/0x14/0x15 marker bytes),
   footnote/endnote reference marks, and other structural control
   characters are passed through as literal characters rather than
   specially interpreted - acceptable for "best effort, text only", not
   attempted to be hidden or resolved to their displayed values, which
   would need tracking Word's field-nesting rules on top of everything
   else here.

   All of the field offsets and struct layouts below (the FIB's
   fibRgFcLcb97 array index for fcClx/lcbClx in particular - the single
   most error-prone number in this whole file, since it's an index into a
   long, mostly-irrelevant-to-us array of (fc,lcb) pairs) were verified
   against a real .dot file's actual bytes with a Python prototype before
   being ported here, rather than trusted from memory alone - OLE2/MS-DOC
   are notoriously easy to get subtly wrong, and there was no way to test
   the ported C directly without an emulator.
*/

/* ---- little-endian byte readers (the file is little-endian; this target is 68k/big-endian) ---- */

static unsigned short ReadLE16(const unsigned char *p)
{
    return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned long ReadLE32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* Sector chain "next" links and directory sibling/child links are read as
   signed 32-bit: the sentinel values (ENDOFCHAIN=-2, FREESECT=-1, etc.)
   and "no sibling/child" (-1) are most naturally compared as small
   negative numbers this way, matching how the verified Python prototype
   read them. */
static long ReadLE32Signed(const unsigned char *p)
{
    return (long)ReadLE32(p);
}

/* ---- OLE2 / CFBF container ---- */

#define kOle2Signature0 0xD0
#define kOle2Signature1 0xCF
#define kOle2Signature2 0x11
#define kOle2Signature3 0xE0

#define kOle2EndOfChain  (-2)
#define kOle2FreeSect    (-1)
#define kOle2FatSect     (-3)
#define kOle2DifSect     (-4)

#define kOle2HeaderSize   512
#define kOle2DifatHeadCount 109
#define kOle2DirEntrySize 128
#define kOle2MinStreamSize 4096 /* streams smaller than this live in the mini-stream, not supported - see header comment */

typedef struct {
    long sectorSize;
    long firstDirSector;
    long miniStreamCutoff;
    long firstDifatSector;
    unsigned long numDifatSectors;
    long difatHead[kOle2DifatHeadCount];
} Ole2Header;

static OSErr Ole2ParseHeader(const unsigned char *buf, long len, Ole2Header *hdr)
{
    short i;
    unsigned short sectorShift;

    if (len < kOle2HeaderSize)
        return paramErr;
    if (buf[0] != kOle2Signature0 || buf[1] != kOle2Signature1 ||
        buf[2] != kOle2Signature2 || buf[3] != kOle2Signature3)
        return paramErr;

    sectorShift = ReadLE16(buf + 30);
    if (sectorShift < 7 || sectorShift > 16) /* sanity: 128 bytes .. 64K */
        return paramErr;
    hdr->sectorSize = 1L << sectorShift;

    hdr->firstDirSector = ReadLE32Signed(buf + 48);
    hdr->miniStreamCutoff = (long)ReadLE32(buf + 56);
    hdr->firstDifatSector = ReadLE32Signed(buf + 68);
    hdr->numDifatSectors = ReadLE32(buf + 72);

    for (i = 0; i < kOle2DifatHeadCount; i++)
        hdr->difatHead[i] = ReadLE32Signed(buf + 76 + i * 4);

    return noErr;
}

static long Ole2SectorOffset(const Ole2Header *hdr, long sector)
{
    return kOle2HeaderSize + sector * hdr->sectorSize;
}

/* Builds the full FAT as one array of signed "next sector" links, indexed
   by sector number. The header carries the first 109 FAT-sector locations
   directly (difatHead); larger files chain additional "DIFAT" sectors
   (each a page of more FAT-sector locations, plus a link to the next
   DIFAT sector in its last slot) to hold the rest. */
static OSErr Ole2BuildFat(const unsigned char *buf, long len, const Ole2Header *hdr,
                           long **outFat, long *outCount)
{
    long *fatSectorIds;
    long fatSectorCount = 0;
    long fatSectorCap = kOle2DifatHeadCount;
    long entriesPerSector = hdr->sectorSize / 4;
    long difatSec;
    long *fat;
    long fatEntryCount, fatWritePos;
    long i, s;

    fatSectorIds = (long *)malloc(sizeof(long) * fatSectorCap);
    if (!fatSectorIds)
        return memFullErr;

    for (i = 0; i < kOle2DifatHeadCount; i++) {
        if (hdr->difatHead[i] != kOle2FreeSect)
            fatSectorIds[fatSectorCount++] = hdr->difatHead[i];
    }

    difatSec = hdr->firstDifatSector;
    while (difatSec >= 0) {
        long off = Ole2SectorOffset(hdr, difatSec);
        if (off < 0 || off + hdr->sectorSize > len) { free(fatSectorIds); return paramErr; }
        if (fatSectorCount + entriesPerSector > fatSectorCap) {
            long newCap = fatSectorCap * 2 + entriesPerSector;
            long *grown = (long *)realloc(fatSectorIds, sizeof(long) * newCap);
            if (!grown) { free(fatSectorIds); return memFullErr; }
            fatSectorIds = grown;
            fatSectorCap = newCap;
        }
        for (i = 0; i < entriesPerSector - 1; i++) {
            long v = ReadLE32Signed(buf + off + i * 4);
            if (v != kOle2FreeSect)
                fatSectorIds[fatSectorCount++] = v;
        }
        difatSec = ReadLE32Signed(buf + off + (entriesPerSector - 1) * 4);
    }

    fatEntryCount = fatSectorCount * entriesPerSector;
    if (fatEntryCount <= 0) { free(fatSectorIds); return paramErr; }
    fat = (long *)malloc(sizeof(long) * fatEntryCount);
    if (!fat) { free(fatSectorIds); return memFullErr; }

    fatWritePos = 0;
    for (s = 0; s < fatSectorCount; s++) {
        long off = Ole2SectorOffset(hdr, fatSectorIds[s]);
        if (off < 0 || off + hdr->sectorSize > len) { free(fatSectorIds); free(fat); return paramErr; }
        for (i = 0; i < entriesPerSector; i++)
            fat[fatWritePos++] = ReadLE32Signed(buf + off + i * 4);
    }

    free(fatSectorIds);
    *outFat = fat;
    *outCount = fatEntryCount;
    return noErr;
}

/* Reads exactly rangeLen bytes starting at rangeOffset within a FAT-chained
   stream, into a caller-provided buffer - without ever materializing the
   whole stream. Used for the (potentially large) WordDocument stream so
   peak memory stays bounded to one piece's worth of text at a time,
   rather than the whole stream (which routinely holds far more than just
   the plain text - fonts, compatibility data, revision history). */
static OSErr Ole2ReadRange(const unsigned char *buf, long len, const Ole2Header *hdr,
                            const long *fat, long fatCount, long startSector,
                            long rangeOffset, long rangeLen, unsigned char *outBuf)
{
    long sec = startSector;
    long skipSectors = rangeOffset / hdr->sectorSize;
    long offsetInSector = rangeOffset % hdr->sectorSize;
    long i, copied = 0;

    for (i = 0; i < skipSectors; i++) {
        if (sec < 0 || sec >= fatCount) return paramErr;
        sec = fat[sec];
    }

    while (copied < rangeLen) {
        long chunk = hdr->sectorSize - offsetInSector;
        long fileOff;
        if (sec < 0) return paramErr;
        if (chunk > rangeLen - copied) chunk = rangeLen - copied;
        fileOff = Ole2SectorOffset(hdr, sec) + offsetInSector;
        if (fileOff < 0 || fileOff + chunk > len) return paramErr;
        memcpy(outBuf + copied, buf + fileOff, chunk);
        copied += chunk;
        offsetInSector = 0;
        if (copied < rangeLen) {
            if (sec < 0 || sec >= fatCount) return paramErr;
            sec = fat[sec];
        }
    }
    return noErr;
}

/* Materializes a whole FAT-chained stream - only used for streams known to
   be small (the directory stream, and 0Table/1Table, which hold formatting
   tables and the piece table, not the document's raw text). */
static OSErr Ole2ReadWhole(const unsigned char *buf, long len, const Ole2Header *hdr,
                            const long *fat, long fatCount, long startSector, long size,
                            unsigned char **outData)
{
    unsigned char *data;
    OSErr err;

    if (size < 0 || size > 2000000L)
        return paramErr;
    data = (unsigned char *)malloc(size > 0 ? size : 1);
    if (!data)
        return memFullErr;
    if (size > 0) {
        err = Ole2ReadRange(buf, len, hdr, fat, fatCount, startSector, 0, size, data);
        if (err != noErr) { free(data); return err; }
    }
    *outData = data;
    return noErr;
}

typedef struct {
    char name[32];
    unsigned char type; /* 0=unused/unknown, 1=storage, 2=stream, 5=root storage */
    long left, right, child;
    long startSector;
    long size; /* low 32 bits only - fine, every stream this reader cares about is well under 4GB */
} Ole2DirEntry;

static OSErr Ole2ParseDirectory(const unsigned char *buf, long len, const Ole2Header *hdr,
                                 const long *fat, long fatCount,
                                 Ole2DirEntry **outEntries, long *outCount)
{
    unsigned char *raw;
    OSErr err;
    long rawLen = 0;
    long sec = hdr->firstDirSector;
    long sectorCount = 0;
    Ole2DirEntry *entries;
    long n, i;

    /* Directory size isn't given directly for major version 3 files - walk
       the chain once just to count sectors (bounded generously so a
       malformed/circular chain can't loop forever), then read it for real. */
    while (sec >= 0 && sectorCount < 4096) {
        if (sec >= fatCount) return paramErr;
        sectorCount++;
        sec = fat[sec];
    }
    if (sectorCount == 0)
        return paramErr;
    rawLen = sectorCount * hdr->sectorSize;

    raw = (unsigned char *)malloc(rawLen);
    if (!raw)
        return memFullErr;
    err = Ole2ReadRange(buf, len, hdr, fat, fatCount, hdr->firstDirSector, 0, rawLen, raw);
    if (err != noErr) { free(raw); return err; }

    n = rawLen / kOle2DirEntrySize;
    entries = (Ole2DirEntry *)malloc(sizeof(Ole2DirEntry) * (n > 0 ? n : 1));
    if (!entries) { free(raw); return memFullErr; }

    for (i = 0; i < n; i++) {
        const unsigned char *e = raw + i * kOle2DirEntrySize;
        unsigned short nameLenBytes = ReadLE16(e + 64);
        short nameChars, j;

        entries[i].type = e[66];
        entries[i].name[0] = 0;

        if (nameLenBytes >= 2) {
            /* UTF-16LE name, best-effort ASCII-only transcode (every name
               this reader looks for - WordDocument, 0Table/1Table - is
               plain ASCII, so this doesn't need real Unicode handling). */
            nameChars = (short)((nameLenBytes - 2) / 2);
            if (nameChars > 31) nameChars = 31;
            for (j = 0; j < nameChars; j++) {
                unsigned short ch = ReadLE16(e + j * 2);
                entries[i].name[j] = (char)((ch < 0x80) ? ch : '?');
            }
            entries[i].name[nameChars] = 0;
        }

        entries[i].left = ReadLE32Signed(e + 68);
        entries[i].right = ReadLE32Signed(e + 72);
        entries[i].child = ReadLE32Signed(e + 76);
        entries[i].startSector = ReadLE32Signed(e + 116);
        entries[i].size = (long)ReadLE32(e + 120); /* low 32 bits of a 64-bit size */
    }

    free(raw);
    *outEntries = entries;
    *outCount = n;
    return noErr;
}

/* Directory entries form a red-black tree (per-storage sibling links plus
   a child pointer into that storage's own children) rather than a flat
   list - walked here as a plain binary tree (ignoring the red/black
   balancing bits, which only matter for insertion/deletion, not lookup)
   since all that's needed is finding a stream by name. */
static long Ole2FindByName(const Ole2DirEntry *entries, long count, long idx, const char *name)
{
    long r;
    if (idx < 0 || idx >= count)
        return -1;
    r = Ole2FindByName(entries, count, entries[idx].left, name);
    if (r >= 0) return r;
    if (strcmp(entries[idx].name, name) == 0)
        return idx;
    return Ole2FindByName(entries, count, entries[idx].right, name);
}

/* ---- Windows-1252 -> Unicode -> Mac OS Roman ---- */

/* cp1252's 0xA0-0xFF range is identical to Latin-1 / Unicode at those same
   code points, so only the 0x80-0x9F range (where cp1252 diverges from
   Latin-1 with the smart quotes/dashes/etc.) needs an explicit table. */
static const unsigned short kCp1252Low[32] = {
    0x20AC, 0x003F, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, /* 80-87 */
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x003F, 0x017D, 0x003F, /* 88-8F */
    0x003F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, /* 90-97 */
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x003F, 0x017E, 0x0178  /* 98-9F */
};

static unsigned long Cp1252ByteToUnicode(unsigned char b)
{
    if (b < 0x80) return b;
    if (b < 0xA0) return kCp1252Low[b - 0x80];
    return b; /* 0xA0-0xFF: Latin-1, same as the Unicode code point */
}

static char UnicodeToMacRoman(unsigned long cp)
{
    short i;
    if (cp < 0x80)
        return (char)cp;
    for (i = 0; i < 128; i++) {
        if (kMacRomanHigh[i] == (unsigned short)cp)
            return (char)(0x80 + i);
    }
    return '?';
}

/* ---- FIB (File Information Block) + piece table ---- */

#define kFibIdent 0xA5EC

/* Index of the (fc,lcb) pair for the piece table's "clx" structure within
   fibRgFcLcb97, the FIB's variable-length array of (fc,lcb) pairs
   (fibRgFcLcb97 itself starts right after a 2-byte count field at FIB
   offset 152). Verified against a real file's actual bytes - see this
   file's header comment. */
#define kFibClxIndex 33
#define kFibRgFcLcbOffset 154

#define kDocMaxImportChars 28000 /* matches rtf.c's kRtfMaxImportChars */

typedef struct {
    long cpStart, cpEnd;
    Boolean compressed;
    long byteOff;
} DocPiece;

static OSErr ExtractPieceTable(const unsigned char *clx, long clxLen, DocPiece **outPieces, long *outCount)
{
    long pos = 0;
    const unsigned char *pcdt = NULL;
    long pcdtLen = 0;
    long n, i;
    DocPiece *pieces;

    /* clx is a sequence of "Prc" property-list blocks (marker byte 1,
       2-byte length, that many bytes - skipped, since formatting isn't
       recovered here) followed by exactly one "Pcdt" (marker byte 2,
       4-byte length, then the piece table itself). */
    while (pos < clxLen) {
        unsigned char marker = clx[pos];
        if (marker == 1) {
            unsigned short cb;
            if (pos + 3 > clxLen) return paramErr;
            cb = ReadLE16(clx + pos + 1);
            pos += 3 + cb;
        } else if (marker == 2) {
            if (pos + 5 > clxLen) return paramErr;
            pcdtLen = (long)ReadLE32(clx + pos + 1);
            pcdt = clx + pos + 5;
            if (pcdtLen < 0 || pos + 5 + pcdtLen > clxLen) return paramErr;
            break;
        } else {
            return paramErr;
        }
    }
    if (!pcdt)
        return paramErr;

    if (pcdtLen < 4) return paramErr;
    n = (pcdtLen - 4) / 12;
    if (n <= 0) return paramErr;

    pieces = (DocPiece *)malloc(sizeof(DocPiece) * n);
    if (!pieces) return memFullErr;

    for (i = 0; i < n; i++) {
        long cpStart = (long)ReadLE32(pcdt + i * 4);
        long cpEnd = (long)ReadLE32(pcdt + (i + 1) * 4);
        const unsigned char *pcd = pcdt + 4 * (n + 1) + i * 8;
        unsigned long fcRaw = ReadLE32(pcd + 2);
        Boolean compressed = (Boolean)((fcRaw & 0x40000000L) != 0);
        long fc = (long)(fcRaw & 0x3FFFFFFFL);

        pieces[i].cpStart = cpStart;
        pieces[i].cpEnd = cpEnd;
        pieces[i].compressed = compressed;
        pieces[i].byteOff = compressed ? (fc / 2) : fc;
    }

    *outPieces = pieces;
    *outCount = n;
    return noErr;
}

OSErr ReadDocumentFromDoc(Document *doc, const FSSpec *src)
{
    short refNum;
    long fileLen;
    unsigned char *fileBuf = NULL;
    long count;
    OSErr err;
    Ole2Header hdr;
    long *fat = NULL;
    long fatCount = 0;
    Ole2DirEntry *entries = NULL;
    long entryCount = 0;
    long wdocIdx, tblIdx;
    unsigned char fibHead[160 + (kFibClxIndex + 1) * 8];
    unsigned short flags1;
    Boolean whichTblStm;
    long fcClx, lcbClx;
    unsigned char *tblData = NULL;
    DocPiece *pieces = NULL;
    long pieceCount = 0;
    long insertPos = 0;
    long p;
    Boolean tooLarge = false;
    TextStyle ts;

    err = FSpOpenDF((FSSpecPtr)src, fsRdPerm, &refNum);
    if (err != noErr)
        return err;
    err = GetEOF(refNum, &fileLen);
    if (err != noErr) { FSClose(refNum); return err; }

    /* Whole file is read into one buffer, same tradeoff as rtf.c's reader -
       simplest way to run the container parser, at the cost of needing
       the file to fit in one contiguous block. A real .doc's byte size is
       dominated by internal structure overhead (fonts, compatibility
       data, revision history), not the plain text it'll produce, so this
       ceiling is deliberately much more generous than the eventual
       character-count limit, and independent of it. */
    if (fileLen > 1500000L) { FSClose(refNum); return kImportTooLargeErr; }

    fileBuf = (unsigned char *)malloc(fileLen > 0 ? fileLen : 1);
    if (!fileBuf) { FSClose(refNum); return memFullErr; }
    count = fileLen;
    err = FSRead(refNum, &count, fileBuf);
    FSClose(refNum);
    if (err != noErr) { free(fileBuf); return err; }

    err = Ole2ParseHeader(fileBuf, fileLen, &hdr);
    if (err != noErr) { free(fileBuf); return err; }

    err = Ole2BuildFat(fileBuf, fileLen, &hdr, &fat, &fatCount);
    if (err != noErr) { free(fileBuf); return err; }

    err = Ole2ParseDirectory(fileBuf, fileLen, &hdr, fat, fatCount, &entries, &entryCount);
    if (err != noErr) { free(fat); free(fileBuf); return err; }

    if (entryCount == 0) { free(entries); free(fat); free(fileBuf); return paramErr; }

    wdocIdx = Ole2FindByName(entries, entryCount, entries[0].child, "WordDocument");
    if (wdocIdx < 0 || entries[wdocIdx].size < kOle2MinStreamSize) {
        free(entries); free(fat); free(fileBuf);
        return paramErr; /* not a Word document, or too small/oddly-structured to be one this reader handles */
    }

    /* Read just enough of the FIB header to reach fibRgFcLcb97[kFibClxIndex]. */
    err = Ole2ReadRange(fileBuf, fileLen, &hdr, fat, fatCount, entries[wdocIdx].startSector,
                         0, sizeof(fibHead), fibHead);
    if (err != noErr) { free(entries); free(fat); free(fileBuf); return err; }

    if (ReadLE16(fibHead) != kFibIdent) {
        free(entries); free(fat); free(fileBuf);
        return paramErr;
    }
    flags1 = ReadLE16(fibHead + 10);
    whichTblStm = (Boolean)((flags1 & 0x0200) != 0);

    tblIdx = Ole2FindByName(entries, entryCount, entries[0].child, whichTblStm ? "1Table" : "0Table");
    if (tblIdx < 0 || entries[tblIdx].size < kOle2MinStreamSize) {
        free(entries); free(fat); free(fileBuf);
        return paramErr;
    }

    fcClx = (long)ReadLE32(fibHead + kFibRgFcLcbOffset + kFibClxIndex * 8);
    lcbClx = (long)ReadLE32(fibHead + kFibRgFcLcbOffset + kFibClxIndex * 8 + 4);
    if (lcbClx <= 0 || lcbClx > 200000L || fcClx < 0 || fcClx + lcbClx > entries[tblIdx].size) {
        free(entries); free(fat); free(fileBuf);
        return paramErr;
    }

    err = Ole2ReadWhole(fileBuf, fileLen, &hdr, fat, fatCount, entries[tblIdx].startSector,
                         entries[tblIdx].size, &tblData);
    if (err != noErr) { free(entries); free(fat); free(fileBuf); return err; }

    err = ExtractPieceTable(tblData + fcClx, lcbClx, &pieces, &pieceCount);
    free(tblData);
    if (err != noErr) { free(entries); free(fat); free(fileBuf); return err; }

    memset(&ts, 0, sizeof(ts));
    ts.tsFont = systemFont; /* see wordproc.h's ParaStyleSpec.systemFont comment - imported plain text isn't Times-styled Normal */
    ts.tsSize = 12;
    ts.tsFace = 0;
    ts.tsColor.red = 0;
    ts.tsColor.green = 0;
    ts.tsColor.blue = 0;

    for (p = 0; p < pieceCount && !tooLarge; p++) {
        DocPiece *pc = &pieces[p];
        long nChars = pc->cpEnd - pc->cpStart;
        long byteLen = pc->compressed ? nChars : nChars * 2;
        unsigned char *raw;
        char *plain;
        long i;

        if (nChars <= 0)
            continue;
        if (insertPos + nChars > kDocMaxImportChars) {
            tooLarge = true;
            break;
        }

        raw = (unsigned char *)malloc(byteLen > 0 ? byteLen : 1);
        plain = (char *)malloc(nChars);
        if (!raw || !plain) {
            if (raw) free(raw);
            if (plain) free(plain);
            err = memFullErr;
            break;
        }

        err = Ole2ReadRange(fileBuf, fileLen, &hdr, fat, fatCount, entries[wdocIdx].startSector,
                             pc->byteOff, byteLen, raw);
        if (err != noErr) { free(raw); free(plain); break; }

        if (pc->compressed) {
            for (i = 0; i < nChars; i++)
                plain[i] = UnicodeToMacRoman(Cp1252ByteToUnicode(raw[i]));
        } else {
            for (i = 0; i < nChars; i++) {
                unsigned short u = ReadLE16(raw + i * 2);
                plain[i] = (u < 0x80) ? (char)u : UnicodeToMacRoman(u);
            }
        }
        free(raw);

        TESetSelect(insertPos, insertPos, doc->body);
        TEInsert(plain, nChars, doc->body);
        TESetSelect(insertPos, insertPos + nChars, doc->body);
        TESetStyle(doFont | doSize | doFace | doColor, &ts, false, doc->body);
        free(plain);

        insertPos += nChars;
    }

    free(pieces);
    free(entries);
    free(fat);
    free(fileBuf);

    /* A mid-extraction failure or hitting the size ceiling still leaves
       whatever was inserted before it in the window - the caller (DoImport)
       treats any non-noErr return as "discard and start over" (TEDelete +
       DoNew), same as the RTF reader, so a partial/confusing result never
       lingers on screen. */
    if (err != noErr)
        return err;
    if (tooLarge)
        return kImportTooLargeErr;

    TESetSelect(0, 0, doc->body);
    return noErr;
}
