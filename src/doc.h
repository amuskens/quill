#ifndef DOC_H
#define DOC_H

#include "wordproc.h"

/* Best-effort binary .doc reader (Word 97-2003 / OLE2-Compound-File
   format), used for Import when content-sniffing detects the real OLE2
   signature rather than RTF text. Plain text only - no bold/italic/
   underline/font, no paragraph styles, no footnotes/comments/fields/
   tables/pictures. Recovering formatting would mean parsing Word's CHPX/
   PAPX property structures (SPRM opcodes) on top of the piece table this
   already needs - a substantial second undertaking on top of what's here,
   deliberately out of scope for this pass. See doc.c's header comment for
   the format itself and what's/isn't handled within "plain text". */
OSErr ReadDocumentFromDoc(Document *doc, const FSSpec *src);

#endif
