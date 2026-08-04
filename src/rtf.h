#ifndef RTF_H
#define RTF_H

#include "wordproc.h"

/* Writes RTF. Used for both "Save As .rtf" and "Save As .doc" - real binary
   Word (OLE2/CFB) format is out of scope to hand-roll safely, and RTF
   content is auto-detected and opened correctly by Word and virtually every
   other word processor regardless of the file's extension. */
OSErr WriteDocumentAsRtf(Document *doc, const FSSpec *dest);

/* Best-effort RTF reader, used for Import (see app.c's DoImport). Recovers
   plain text plus bold/italic/underline/font/size - not full fidelity
   (no paragraph alignment, no lists/tables, no embedded objects, footnotes
   are dropped rather than reconstructed). Also the path used for importing
   a ".doc" file that's actually RTF content (as this app's own "Save As
   .doc" produces, and as many real .doc files in the wild are) - see
   DoImport's content-sniffing. Genuine binary OLE2 .doc is not handled;
   see the README's "Why .doc is actually RTF". */
OSErr ReadDocumentFromRtf(Document *doc, const FSSpec *src);

#endif
