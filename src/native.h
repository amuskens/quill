#ifndef NATIVE_H
#define NATIVE_H

#include "wordproc.h"

/* Quill's own .qdoc format - see native.c for the format description. */
OSErr WriteDocumentAsQuill(Document *doc, const FSSpec *dest, short zoomPercent);
OSErr ReadDocumentFromQuill(Document *doc, const FSSpec *src, short *outZoomPercent);

#endif
