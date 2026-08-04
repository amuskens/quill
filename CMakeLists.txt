cmake_minimum_required(VERSION 3.10)
project(Quill C)

add_application(Quill
    src/main.c
    src/app.c
    src/docx.c
    src/rtf.c
    src/native.c
    src/zip.c
    src/main.r
    CREATOR "Quil"
)

set_target_properties(Quill PROPERTIES COMPILE_OPTIONS -ffunction-sections)
if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    set_target_properties(Quill PROPERTIES LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-single")
else()
    set_target_properties(Quill PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
endif()
