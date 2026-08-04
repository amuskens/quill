#!/usr/bin/env python3
"""Generates sample/Lorem Ipsum.rtf and sample/Lorem Ipsum.doc: the same
Lorem Ipsum content and heading/quote formatting as sample/Lorem Ipsum.qdoc,
as plain RTF text matching rtf.c's BuildRtfBody output shape (font table,
\\plain\\fN\\b\\i\\ul\\fsN runs, \\par\\pard paragraph breaks). Unlike the
docx sample this replaced, these two are genuinely readable by this app:
File -> Import (rtf.c's ReadDocumentFromRtf) opens both - the .doc file is
byte-identical RTF content under a .doc name, exactly the "doc that's
actually RTF" case DoImport's content-sniffing is built to recognize (see
"Why .doc is actually RTF" in the README). Kept in sync with the *current*
kParaStyleSpecs in wordproc.h (Heading 1 bold, Heading 2 bold+italic,
Heading 3 bold+underline) - not the original non-bold spec these headings
started with earlier in the project.
"""

FONTS = ["Times", "Geneva", "New York", "Helvetica", "Courier", "Monaco", "Palatino"]


def run(face, size_pt, text):
    fs = size_pt * 2
    return f"\\plain\\f0{face}\\fs{fs} {text}\\plain\\f0\\fs24 "


PARAGRAPHS = [
    run("\\b", 14, "Lorem Ipsum"),
    run("", 12, "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do "
                "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
                "enim ad minim veniam, quis nostrud exercitation ullamco laboris "
                "nisi ut aliquip ex ea commodo consequat."),
    run("\\b\\i", 12, "Origins"),
    run("", 12, "Duis aute irure dolor in reprehenderit in voluptate velit esse "
                "cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat "
                "cupidatat non proident, sunt in culpa qui officia deserunt "
                "mollit anim id est laborum."),
    run("\\b\\ul", 12, "A Note on Variations"),
    run("", 12, "Sed ut perspiciatis unde omnis iste natus error sit voluptatem "
                "accusantium doloremque laudantium, totam rem aperiam, eaque "
                "ipsa quae ab illo inventore veritatis et quasi architecto "
                "beatae vitae dicta sunt explicabo."),
    run("\\i", 12, "Neque porro quisquam est, qui dolorem ipsum quia dolor sit "
                   "amet, consectetur, adipisci velit."),
    run("\\b\\i", 12, "Why It's Still Used"),
    run("", 12, "At vero eos et accusamus et iusto odio dignissimos ducimus qui "
                "blanditiis praesentium voluptatum deleniti atque corrupti quos "
                "dolores et quas molestias excepturi sint occaecati cupiditate "
                "non provident."),
]


def build():
    fonttbl = "".join(f"{{\\f{i}\\fnil {name};}}" for i, name in enumerate(FONTS))
    header = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n" f"{{\\fonttbl{fonttbl}}}\n" "\\viewkind4\\pard\\ql\\f0\\fs24\n"
    body = "\\par\\pard\\ql ".join(PARAGRAPHS)
    return header + body + "\\par}"


def main():
    text = build()
    for path in ("sample/Lorem Ipsum.rtf", "sample/Lorem Ipsum.doc"):
        with open(path, "w", newline="\n") as f:
            f.write(text)
        print(f"wrote {path} ({len(text)} bytes)")


if __name__ == "__main__":
    main()
