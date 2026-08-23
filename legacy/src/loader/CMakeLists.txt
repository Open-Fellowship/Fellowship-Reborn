# The loader takes the dinput8.dll name, so it is installed beside Fellowship.exe rather than in
# plugins\.
#
# It patches nothing itself. It writes five bytes over the host's entry point, and the stub puts
# them back, loads every DLL in plugins\ and calls each one's fellowship_reborn_install export.

# The export names come from the /EXPORT pragmas inside dinput8_proxy.c, so a caller binding to a
# real dinput8.dll finds the undecorated names it asks for rather than the stdcall-decorated ones.

add_library(dinput8 SHARED
    dll_main.c
    early_trigger.c
    dinput8_proxy.c
    plugin_loader.c
)

target_link_libraries(dinput8 PRIVATE fellowship_reborn_common)

set_target_properties(dinput8 PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${FELLOWSHIP_REBORN_OUTPUT}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${FELLOWSHIP_REBORN_OUTPUT_DEBUG}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${FELLOWSHIP_REBORN_OUTPUT}
)
