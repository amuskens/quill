#!/usr/bin/env python3
"""Generates sample/Lorem Ipsum.docx: a valid, standalone OOXML Word
document with the same Lorem Ipsum content and heading/quote formatting as
sample/Lorem Ipsum.qdoc, for testing how Quill's export format looks when
opened in real Word/Pages/LibreOffice. This isn't Quill's own docx.c writer
run host-side (there's no way to invoke 68k code here) - it's a standard,
valid OOXML package built with Python's zipfile, matching the *content and
formatting intent* (same headings/quote/paragraph text as the .qdoc sample)
rather than being a byte-identical replica of docx.c's serialization.
"""
import zipfile
import io

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>"""

ROOT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

DOC_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>"""

STYLES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:pPr><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="28"/><w:szCs w:val="28"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:pPr><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:i/><w:sz w:val="24"/><w:szCs w:val="24"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading3"><w:name w:val="heading 3"/><w:basedOn w:val="Normal"/><w:pPr><w:outlineLvl w:val="2"/></w:pPr><w:rPr><w:b/><w:u w:val="single"/><w:sz w:val="24"/><w:szCs w:val="24"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Quote"><w:name w:val="Quote"/><w:basedOn w:val="Normal"/><w:rPr><w:i/><w:sz w:val="24"/><w:szCs w:val="24"/></w:rPr></w:style>
</w:styles>"""


def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace('"', "&quot;"))


def para(style_id, text, rpr=""):
    ppr = f'<w:pPr><w:pStyle w:val="{style_id}"/></w:pPr>' if style_id else ""
    run_rpr = f"<w:rPr>{rpr}</w:rPr>" if rpr else ""
    return (f'<w:p>{ppr}<w:r>{run_rpr}'
            f'<w:t xml:space="preserve">{esc(text)}</w:t></w:r></w:p>')


PARAGRAPHS = [
    ("Heading1", "Lorem Ipsum", ""),
    (None, "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do "
           "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
           "enim ad minim veniam, quis nostrud exercitation ullamco laboris "
           "nisi ut aliquip ex ea commodo consequat.", ""),
    ("Heading2", "Origins", ""),
    (None, "Duis aute irure dolor in reprehenderit in voluptate velit esse "
           "cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat "
           "cupidatat non proident, sunt in culpa qui officia deserunt "
           "mollit anim id est laborum.", ""),
    ("Heading3", "A Note on Variations", ""),
    (None, "Sed ut perspiciatis unde omnis iste natus error sit voluptatem "
           "accusantium doloremque laudantium, totam rem aperiam, eaque "
           "ipsa quae ab illo inventore veritatis et quasi architecto "
           "beatae vitae dicta sunt explicabo.", ""),
    ("Quote", "Neque porro quisquam est, qui dolorem ipsum quia dolor sit "
              "amet, consectetur, adipisci velit.", ""),
    ("Heading2", "Why It's Still Used", ""),
    (None, "At vero eos et accusamus et iusto odio dignissimos ducimus qui "
           "blanditiis praesentium voluptatum deleniti atque corrupti quos "
           "dolores et quas molestias excepturi sint occaecati cupiditate "
           "non provident.", ""),
]

body = "".join(para(style, text, rpr) for style, text, rpr in PARAGRAPHS)

DOCUMENT = ("""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>""" + body + "</w:body></w:document>")


def main():
    out_path = "sample/Lorem Ipsum.docx"
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_STORED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", ROOT_RELS)
        z.writestr("word/_rels/document.xml.rels", DOC_RELS)
        z.writestr("word/styles.xml", STYLES)
        z.writestr("word/document.xml", DOCUMENT)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
