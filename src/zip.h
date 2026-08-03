#ifndef ZIP_H
#define ZIP_H

#include <Types.h>
#include <Files.h>

#define kZipMaxEntries 16

typedef struct {
    char name[64];
    unsigned long crc;
    unsigned long size;
    unsigned long offset;
} ZipEntryInfo;

typedef struct {
    short refNum;
    unsigned long pos;
    short entryCount;
    ZipEntryInfo entries[kZipMaxEntries];
} ZipWriter;

OSErr ZipCreate(const FSSpec *spec, ZipWriter *zw);
OSErr ZipAddEntry(ZipWriter *zw, const char *name, const void *data, unsigned long len);
OSErr ZipClose(ZipWriter *zw);

#endif
