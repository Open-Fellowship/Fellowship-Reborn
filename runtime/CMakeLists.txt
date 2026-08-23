# Fellowship Reborn, runtime layer: fixes for the retail engine of
# The Lord of the Rings: The Fellowship of the Ring (Surreal Software, 2002).
#
# One loader plus one DLL per independent fix. The shared code is a STATIC library linked into
# every DLL, so the artefacts stay independent: there is no runtime DLL that everything else
# needs, and deleting one plugin cannot break another.
#
# 32 BIT ONLY. Every offset, pointer patch and operand repoint here assumes a 32-bit process, and
# src/common/engine_types.h asserts it at compile time. Configure with -A Win32.

cmake_minimum_required(VERSION 3.15)
project(fellowship_reborn C)

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
    #
    # /GS- turns off stack security cookies. These DLLs are dropped into a 2002 game and several
    # of them emit and execute their own code; a /GS cookie check that fails calls
    # __report_gsfailure and terminates the process, which is the opposite of how everything else
    # here fails, by validating, declining and writing a line. The tree does compile cleanly with
    # /GS, so revisiting this is a play-test rather than a build.
    add_compile_options(/W4 /WX /GS- /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    # Static CRT: these DLLs are dropped into a 2002 game folder and must not need a
    # redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Where the built artefacts land, laid out the way they are installed: the loader beside the
# executable, the fixes in plugins\.
set(FELLOWSHIP_REBORN_OUTPUT ${CMAKE_BINARY_DIR}/dist)

# Debug goes somewhere else entirely. Both configurations used to write here, so building
# Debug after Release replaced every DLL with a build eight times the size and left 25 .pdb
# files beside them, all of it looking exactly like something ready to copy into the game.
set(FELLOWSHIP_REBORN_OUTPUT_DEBUG ${CMAKE_BINARY_DIR}/dist-debug)

# The output directory is emptied at configure time, and that is a safety property rather than
# tidiness. CMake never deletes the output of a target that has stopped existing, so after a
# plugin is renamed or removed its old DLL simply stays in dist\plugins, gets installed alongside
# the new one, and the loader then applies two generations of the same patch to the same engine
# cells.
file(REMOVE_RECURSE ${FELLOWSHIP_REBORN_OUTPUT}/plugins)

# And the configuration file under its former name, for exactly the same reason. The rename left a
# stale fellowship_reborn.ini sitting in the output beside the new one, and while the loader prefers
# the new name and would have ignored it, a release folder containing two configuration files is a
# release that gets shipped with the wrong one edited.
file(REMOVE ${FELLOWSHIP_REBORN_OUTPUT}/open_fellowship.ini
            ${FELLOWSHIP_REBORN_OUTPUT}/fix_enhancers.ini)

add_subdirectory(src/common)
add_subdirectory(src/loader)
add_subdirectory(src/plugins)

# The configuration file ships next to the executable, not in plugins\. Copied rather than
# remembered, for the same reason the output directory is wiped: a release assembled partly by
# CMake and partly by remembering to copy a file is a release that eventually goes out without it.
add_custom_target(dist_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/dist/fellowship_reborn.ini
            ${FELLOWSHIP_REBORN_OUTPUT}/fellowship_reborn.ini
    COMMENT "Copying dist/fellowship_reborn.ini into dist/"
    VERBATIM
)
