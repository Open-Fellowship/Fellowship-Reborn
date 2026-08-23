# OpenFellowship, engine layer: replacing the retail Riot Engine module.
#
# Separate from legacy/ on purpose. legacy/ patches the retail engine from
# outside and shares a static library between its plugins; this layer replaces
# the engine module itself and must not depend on any of that, because the two
# have different lifetimes - legacy/ becomes unnecessary exactly as this layer
# becomes complete.
#
# 32 BIT ONLY, for the same reason as legacy/: the retail module is a 32-bit DLL
# and this one loads it into the same process. Configure with -A Win32.

cmake_minimum_required(VERSION 3.15)
project(fellowship_reborn_engine C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "This project targets a 32-bit game. Configure with: cmake -S . -B build -A Win32")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /WX /GS- /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    # Static CRT: this DLL is dropped into a 2002 game folder and must not need
    # a redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(FELLOWSHIP_REBORN_ENGINE_OUTPUT ${CMAKE_BINARY_DIR}/dist)

# The output is named Fellowship.rfl because that is what the host looks for.
# The retail module is renamed to Fellowship.orig.rfl and loaded by this one;
# see engine/proxy/README.md.
# Everything under objectdef/ ending in _table.c is GENERATED - the engine's four
# static registries - and regenerating them is how they get fixed; never edit
# them. They are checked into the tree rather than produced at build time because
# generating them needs a pristine Fellowship.rfl, which a build must not
# require:
#
#   python decomp/tools/objdefgen.py --emit   engine/objectdef
#   python decomp/tools/objdefgen.py --verify engine/objectdef
#
# --verify compiles them and compares every non-pointer field against the retail
# image. It passes on 9,334 fields.
add_library(fellowship_rfl SHARED
    proxy/proxy.c
    proxy/predicates.c
    objectdef/objectdef_table.c
    objectdef/objtype_table.c
    objectdef/landtype_table.c
    objectdef/message_table.c
)

target_include_directories(fellowship_rfl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/proxy
    ${CMAKE_CURRENT_SOURCE_DIR}/objectdef)

set_target_properties(fellowship_rfl PROPERTIES
    PREFIX ""
    OUTPUT_NAME "Fellowship"
    SUFFIX ".rfl"
    RUNTIME_OUTPUT_DIRECTORY ${FELLOWSHIP_REBORN_ENGINE_OUTPUT}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${FELLOWSHIP_REBORN_ENGINE_OUTPUT}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${FELLOWSHIP_REBORN_ENGINE_OUTPUT}
)

if(MSVC)
    # The .def is what keeps the exported names undecorated. Without it __cdecl
    # exports carry a leading underscore, GetProcAddress fails, and the host
    # reports "RFL initialization fail" with nothing to say which name was
    # wrong.
    set_target_properties(fellowship_rfl PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/proxy/fellowship_rfl.def\"")
endif()
