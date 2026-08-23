# The shared layer. A STATIC library on purpose: each plugin links its own copy, so nothing at
# run time depends on anything else being loaded. Deleting one plugin cannot break another, and
# there is no "core runtime DLL" that has to be version-matched.
#
# The include directory is src/ rather than src/common/, which is why every file in the tree
# writes `#include "common/logging.h"` and no file counts dots to find a header.

add_library(open_fellowship_common STATIC
    host_image.c
    logging.c
    ini.c
    memory.c
    patch.c
    trampoline.c
    emit.c
    module_watch.c
    platform.c
    camera.c
    channel.c
)

target_include_directories(open_fellowship_common PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(open_fellowship_common PUBLIC kernel32 user32)
