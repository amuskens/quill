#include "Types.r"
#include "Dialogs.r"
#include "Processes.r"
#include "icon.r"

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    512 * 1024,
    1536 * 1024
};

resource 'MBAR' (128) {
    { 128, 129, 130, 131, 133, 134, 135, 132 }
};

resource 'MENU' (128, preload) {
    128, textMenuProc,
    allEnabled,
    enabled,
    apple,
    {
        "About Quill\0xc9", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (129, preload) {
    129, textMenuProc,
    allEnabled,
    enabled,
    "File",
    {
        "New Document/N", noIcon, noKey, noMark, plain;
        "Open\0xc9/O", noIcon, noKey, noMark, plain;
        "Import (.rtf/.doc)\0xc9", noIcon, noKey, noMark, plain;
        "Save/S", noIcon, noKey, noMark, plain;
        "Save As Quill Document\0xc9", noIcon, noKey, noMark, plain;
        "Save As Word Document (.docx)\0xc9", noIcon, noKey, noMark, plain;
        "Save As Rich Text (.rtf)\0xc9", noIcon, noKey, noMark, plain;
        "Save As Legacy Word (.doc)\0xc9", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Quit Quill/Q", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (130, preload) {
    130, textMenuProc,
    allEnabled,
    enabled,
    "Edit",
    {
        "Undo/Z", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Cut/X", noIcon, noKey, noMark, plain;
        "Copy/C", noIcon, noKey, noMark, plain;
        "Paste/V", noIcon, noKey, noMark, plain;
        "Clear", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (131, preload) {
    131, textMenuProc,
    allEnabled,
    enabled,
    "Format",
    {
        "Bold/B", noIcon, noKey, noMark, plain;
        "Italic/I", noIcon, noKey, noMark, plain;
        "Underline/U", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Times", noIcon, noKey, noMark, plain;
        "Geneva", noIcon, noKey, noMark, plain;
        "New York", noIcon, noKey, noMark, plain;
        "Helvetica", noIcon, noKey, noMark, plain;
        "Courier", noIcon, noKey, noMark, plain;
        "Monaco", noIcon, noKey, noMark, plain;
        "Palatino", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "9 Point", noIcon, noKey, noMark, plain;
        "10 Point", noIcon, noKey, noMark, plain;
        "12 Point", noIcon, noKey, noMark, plain;
        "14 Point", noIcon, noKey, noMark, plain;
        "18 Point", noIcon, noKey, noMark, plain;
        "24 Point", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (132, preload) {
    132, textMenuProc,
    allEnabled,
    enabled,
    "Insert",
    {
        "Insert Footnote\0xc9", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (133, preload) {
    133, textMenuProc,
    allEnabled,
    enabled,
    "Style",
    {
        "Normal", noIcon, noKey, noMark, plain;
        "Heading 1", noIcon, noKey, noMark, plain;
        "Heading 2", noIcon, noKey, noMark, plain;
        "Heading 3", noIcon, noKey, noMark, plain;
        "Heading 4", noIcon, noKey, noMark, plain;
        "Quote", noIcon, noKey, noMark, plain;
        "Bibliography", noIcon, noKey, noMark, plain;
        "-", noIcon, noKey, noMark, plain;
        "Bullet List", noIcon, noKey, noMark, plain;
        "Numbered List", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (134, preload) {
    134, textMenuProc,
    allEnabled,
    enabled,
    "Align",
    {
        "Whole Document:", noIcon, noKey, noMark, plain;
        "Left", noIcon, noKey, noMark, plain;
        "Center", noIcon, noKey, noMark, plain;
        "Right", noIcon, noKey, noMark, plain;
        "Justify", noIcon, noKey, noMark, plain;
    }
};

resource 'MENU' (135, preload) {
    135, textMenuProc,
    allEnabled,
    enabled,
    "Zoom",
    {
        "50%", noIcon, noKey, noMark, plain;
        "100%", noIcon, noKey, noMark, plain;
        "150%", noIcon, noKey, noMark, plain;
        "200%", noIcon, noKey, noMark, plain;
        "300%", noIcon, noKey, noMark, plain;
        "400%", noIcon, noKey, noMark, plain;
    }
};

resource 'DLOG' (200) {
    { 100, 100, 220, 400 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    200,
    "Insert Footnote",
    centerMainScreen
};

resource 'DITL' (200) {
    {
        { 90, 220, 110, 290 },
        Button { enabled, "OK" };

        { 90, 130, 110, 200 },
        Button { enabled, "Cancel" };

        { 10, 10, 26, 290 },
        StaticText { enabled, "Footnote text:" };

        { 30, 10, 75, 290 },
        EditText { enabled, "" };
    }
};

resource 'ALRT' (201) {
    { 80, 80, 210, 420 },
    201,
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    centerMainScreen
};

resource 'DITL' (201) {
    {
        { 100, 250, 120, 320 },
        Button { enabled, "OK" };

        { 10, 10, 90, 320 },
        StaticText { enabled, "Quill - A clean, modern-feeling word processor for classic Mac OS: rich text, paragraph styles, lists, footnotes, alignment, zoom, and .docx/.rtf/.doc export. Made by Anders Muskens" };
    }
};

resource 'ALRT' (202) {
    { 100, 80, 190, 400 },
    202,
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    centerMainScreen
};

resource 'DITL' (202) {
    {
        { 60, 230, 80, 300 },
        Button { enabled, "OK" };

        { 10, 10, 50, 300 },
        StaticText { enabled, "^0" };
    }
};

resource 'ALRT' (203) {
    { 100, 60, 220, 420 },
    203,
    {
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
        OK, visible, silent;
    },
    centerMainScreen
};

resource 'DITL' (203) {
    {
        { 90, 270, 110, 340 },
        Button { enabled, "OK" };

        { 10, 10, 80, 340 },
        StaticText { enabled, "^0" };
    }
};

resource 'DLOG' (204) {
    { 100, 60, 210, 380 },
    dBoxProc,
    visible,
    noGoAway,
    0,
    204,
    "Save changes?",
    centerMainScreen
};

resource 'DITL' (204) {
    {
        { 70, 20, 90, 110 },
        Button { enabled, "Don\0xd5t Save" };

        { 70, 130, 90, 200 },
        Button { enabled, "Cancel" };

        { 70, 220, 90, 300 },
        Button { enabled, "Save" };

        { 10, 10, 55, 300 },
        StaticText { enabled, "Save changes to \0xd2^0\0xd3 before closing?" };
    }
};
