# OpenFellowship, legacy layer: fixes for the retail engine of
# The Lord of the Rings: The Fellowship of the Ring (Surreal Software, 2002).
#
# One loader plus one DLL per independent fix. The shared code is a STATIC library linked into
# every DLL, so the artefacts stay independent: there is no runtime DLL that everything else
# needs, and deleting one plugin cannot break another.
#
# 32 BIT ONLY. Every offset, pointer patch and operand repoint here assumes a 32-bit process, and
# src/common/engine_types.h asserts it at compile time. Configure with -A Win32.

cmake_minimum_required(VERSION 3.15)
project(open_fellowship C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "This project targets a 32-bit game. Configure with: cmake -S . -B build -A Win32")
endif()

# The C11 requirement is not decoration: _Static_assert carries the binary layout checks that stop
# a structure from silently disagreeing with the engine's.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    # Warnings are errors. Integer truncation, pointer truncation and signed/unsigned conversions
    # are precisely the mistakes that stay invisible here until the game crashes.
    add_compile_options(/W4 /WX /GS- /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    # Static CRT: these DLLs are dropped into a 2002 game folder and must not need a
    # redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Where the built artefacts land, laid out the way they are installed: the loader beside the
# executable, the fixes in plugins\.
set(OPEN_FELLOWSHIP_OUTPUT ${CMAKE_BINARY_DIR}/dist)

# The output directory is emptied at configure time, and that is a safety property rather than
# tidiness. CMake never deletes the output of a target that has stopped existing, so after a
# plugin is renamed or removed its old DLL simply stays in dist\plugins, gets installed alongside
# the new one, and the loader then applies two generations of the same patch to the same engine
# cells.
file(REMOVE_RECURSE ${OPEN_FELLOWSHIP_OUTPUT}/plugins)

# And the configuration file under its former name, for exactly the same reason. The rename left a
# stale open_fellowship.ini sitting in the output beside the new one, and while the loader prefers
# the new name and would have ignored it, a release folder containing two configuration files is a
# release that gets shipped with the wrong one edited.
file(REMOVE ${OPEN_FELLOWSHIP_OUTPUT}/open_fellowship.ini)

add_subdirectory(src/common)
add_subdirectory(src/loader)
add_subdirectory(src/plugins)

# The configuration file ships next to the executable, not in plugins\. Copied rather than
# remembered, for the same reason the output directory is wiped: a release assembled partly by
# CMake and partly by remembering to copy a file is a release that eventually goes out without it.
add_custom_target(dist_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/dist/fix_enhancers.ini
            ${OPEN_FELLOWSHIP_OUTPUT}/fix_enhancers.ini
    COMMENT "Copying dist/fix_enhancers.ini into dist/"
    VERBATIM
)
