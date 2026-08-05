#ifndef RTF_H
#define RTF_H

#include "wordproc.h"

/* Writes RTF. Used for both "Save As .rtf" and "Save As .doc" - real binary
   Word (OLE2/CFB) format is out of scope to hand-roll safely, and RTF
   content is auto-detected and opened correctly by Word and virtually every
   other word processor regardless of the file's extension.

   includeComments: even though .doc and .rtf go through this exact same
   writer, comments are meant to behave differently between them - present
   in .doc, dropped (with a warning shown first) in .rtf. Callers pass
   true/false accordingly rather than this file trying to guess intent from
   the destination filename. When false, comment markers are still spliced
   out of the body (same as when true) - only the annotation data itself is
   omitted, so a stray marker character never leaks into the output. */
OSErr WriteDocumentAsRtf(Document *doc, const FSSpec *dest, Boolean includeComments);

/* Best-effort RTF reader, used for Import (see app.c's DoImport). Recovers
   plain text plus bold/italic/underline/font/size - not full fidelity
   (no paragraph alignment, no lists/tables, no embedded objects, footnotes
   are dropped rather than reconstructed). Also the path used for importing
   a ".doc" file that's actually RTF content (as this app's own "Save As
   .doc" produces, and as many real .doc files in the wild are) - see
   DoImport's content-sniffing. Genuine binary OLE2 .doc goes through
   doc.c's separate reader instead - see its header comment. Returns
   kImportTooLargeErr (defined in wordproc.h, shared with doc.c) if the
   file is (or would produce) too much text for classic TextEdit's
   character limit - see kRtfMaxImportChars in rtf.c. */
OSErr ReadDocumentFromRtf(Document *doc, const FSSpec *src);

#endif
