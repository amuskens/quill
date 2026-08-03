#include "zip.h"
#include <string.h>

static unsigned long Crc32(const unsigned char *buf, unsigned long len)
{
    unsigned long crc = 0xFFFFFFFFUL;
    unsigned long i, j;
    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void PutLE16(unsigned char *p, unsigned short v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void PutLE32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static OSErr WriteRaw(ZipWriter *zw, const void *data, unsigned long len)
{
    long cnt = (long)len;
    OSErr err;
    if (len == 0)
        return noErr;
    err = FSWrite(zw->refNum, &cnt, data);
    if (err == noErr)
        zw->pos += len;
    return err;
}

OSErr ZipCreate(const FSSpec *spec, ZipWriter *zw)
{
    OSErr err;
    short refNum;

    memset(zw, 0, sizeof(ZipWriter));

    FSpDelete((FSSpecPtr)spec); /* ignore "doesn't exist" error */

    err = FSpCreate((FSSpecPtr)spec, '????', '????', smSystemScript);
    if (err != noErr)
        return err;

    err = FSpOpenDF((FSSpecPtr)spec, fsWrPerm, &refNum);
    if (err != noErr)
        return err;

    zw->refNum = refNum;
    zw->pos = 0;
    zw->entryCount = 0;
    return noErr;
}

OSErr ZipAddEntry(ZipWriter *zw, const char *name, const void *data, unsigned long len)
{
    unsigned char header[30];
    unsigned short nameLen = (unsigned short)strlen(name);
    unsigned long crc = Crc32((const unsigned char *)data, len);
    unsigned long entryOffset = zw->pos;
    OSErr err;

    if (zw->entryCount >= kZipMaxEntries)
        return paramErr;

    PutLE32(header + 0, 0x04034b50UL);
    PutLE16(header + 4, 20);   /* version needed */
    PutLE16(header + 6, 0);    /* flags */
    PutLE16(header + 8, 0);    /* method: stored */
    PutLE16(header + 10, 0);   /* mod time */
    PutLE16(header + 12, 0x21); /* mod date: 1980-01-01 */
    PutLE32(header + 14, crc);
    PutLE32(header + 18, len); /* compressed size */
    PutLE32(header + 22, len); /* uncompressed size */
    PutLE16(header + 26, nameLen);
    PutLE16(header + 28, 0);   /* extra field length */

    err = WriteRaw(zw, header, sizeof(header));
    if (err != noErr)
        return err;
    err = WriteRaw(zw, name, nameLen);
    if (err != noErr)
        return err;
    err = WriteRaw(zw, data, len);
    if (err != noErr)
        return err;

    {
        ZipEntryInfo *e = &zw->entries[zw->entryCount++];
        strncpy(e->name, name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = 0;
        e->crc = crc;
        e->size = len;
        e->offset = entryOffset;
    }

    return noErr;
}

OSErr ZipClose(ZipWriter *zw)
{
    unsigned long centralStart = zw->pos;
    unsigned long centralSize = 0;
    short i;
    OSErr err;

    for (i = 0; i < zw->entryCount; i++) {
        ZipEntryInfo *e = &zw->entries[i];
        unsigned short nameLen = (unsigned short)strlen(e->name);
        unsigned char header[46];
        unsigned long before = zw->pos;

        PutLE32(header + 0, 0x02014b50UL);
        PutLE16(header + 4, 20);  /* version made by */
        PutLE16(header + 6, 20);  /* version needed */
        PutLE16(header + 8, 0);   /* flags */
        PutLE16(header + 10, 0);  /* method: stored */
        PutLE16(header + 12, 0);  /* mod time */
        PutLE16(header + 14, 0x21); /* mod date */
        PutLE32(header + 16, e->crc);
        PutLE32(header + 20, e->size);
        PutLE32(header + 24, e->size);
        PutLE16(header + 28, nameLen);
        PutLE16(header + 30, 0);  /* extra length */
        PutLE16(header + 32, 0);  /* comment length */
        PutLE16(header + 34, 0);  /* disk number start */
        PutLE16(header + 36, 0);  /* internal attrs */
        PutLE32(header + 38, 0);  /* external attrs */
        PutLE32(header + 42, e->offset);

        err = WriteRaw(zw, header, sizeof(header));
        if (err != noErr)
            return err;
        err = WriteRaw(zw, e->name, nameLen);
        if (err != noErr)
            return err;

        centralSize += (zw->pos - before);
    }

    {
        unsigned char eocd[22];
        PutLE32(eocd + 0, 0x06054b50UL);
        PutLE16(eocd + 4, 0);
        PutLE16(eocd + 6, 0);
        PutLE16(eocd + 8, (unsigned short)zw->entryCount);
        PutLE16(eocd + 10, (unsigned short)zw->entryCount);
        PutLE32(eocd + 12, centralSize);
        PutLE32(eocd + 16, centralStart);
        PutLE16(eocd + 20, 0);

        err = WriteRaw(zw, eocd, sizeof(eocd));
        if (err != noErr)
            return err;
    }

    FSClose(zw->refNum);
    return noErr;
}
