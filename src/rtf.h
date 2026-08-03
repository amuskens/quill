#ifndef RTF_H
#define RTF_H

#include "wordproc.h"

/* Writes RTF. Used for both "Save As .rtf" and "Save As .doc" - real binary
   Word (OLE2/CFB) format is out of scope to hand-roll safely, and RTF
   content is auto-detected and opened correctly by Word and virtually every
   other word processor regardless of the file's extension. */
OSErr WriteDocumentAsRtf(Document *doc, const FSSpec *dest);

#endif
