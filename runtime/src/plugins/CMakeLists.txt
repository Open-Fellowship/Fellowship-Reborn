# One DLL per independent behaviour, all of them landing in plugins\.
#
# add_fellowship_plugin(<name> <sources...>) builds <name>.dll from <name>/<source>. The directory
# name, the DLL name, the ini section and the log prefix are all the same word, which is what lets
# a player match a line in the log to a file on disk without a lookup table.

function(add_fellowship_plugin name)
    set(sources "")
    foreach(source ${ARGN})
        list(APPEND sources ${name}/${source})
    endforeach()

    add_library(${name} SHARED ${sources})
    target_link_libraries(${name} PRIVATE fellowship_reborn_common)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${name})
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY ${FELLOWSHIP_REBORN_OUTPUT}/plugins
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${FELLOWSHIP_REBORN_OUTPUT_DEBUG}/plugins
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${FELLOWSHIP_REBORN_OUTPUT}/plugins
    )
endfunction()


# Not a fix. It exercises the whole contract, loaded, entry point called, ini read, log written,
# so that when a real plugin misbehaves it is possible to tell whether the loader is at fault.
add_fellowship_plugin(template_plugin
    dll_main.c
    template_plugin.c
)

# ------------------------------------------------------------------------------ Fellowship.exe

# The only genuine engine BUG in this set. Ships on, and can be switched off in the ini like
# anything else; black_screen and cd_check are the two that have no switch at all. Harmless at
# 640x480, where nothing was ever outside the guard rect.
add_fellowship_plugin(edge_popin
    dll_main.c
    edge_popin.c
)

# Five preferences about draw distance in one DLL, because they are one decision with one failure
# mode: the world draws further and the frame rate drops.
add_fellowship_plugin(view_distance
    dll_main.c
    view_distance.c
)

add_fellowship_plugin(model_lod
    dll_main.c
    model_lod.c
)

# The one interactive fix. F1, because the game's own README binds F5 to F12 to its cheats.
add_fellowship_plugin(fog_toggle
    dll_main.c
    fog_toggle.c
)

# Sets the focal length rather than the engine's focal NUMERATOR, which is what keeps the
# inventory icons where they belong. See its README for the measurement.
add_fellowship_plugin(field_of_view
    dll_main.c
    field_of_view.c
)

# ------------------------------------------------- ported from the community patcher
#
# Fellowship.dll's own options, decoded out of its installer table rather than guessed at. Its
# CameraFieldOfView is deliberately NOT ported: field_of_view above does the same job without
# moving every inventory icon out of its cell.

add_fellowship_plugin(resolution_unlock
    dll_main.c
    resolution_unlock.c
)

add_fellowship_plugin(game_speed
    dll_main.c
    game_speed.c
)

add_fellowship_plugin(cd_check
    dll_main.c
    cd_check.c
)

# A one-dword guard, not a fix on the build here. A stock executable answers D3DFMT_P8 for 8-bit
# textures, NVIDIA dropped paletted support, and the game hangs on a black screen at load, on
# AMD it does not. Reads before it writes and says which situation it found.
add_fellowship_plugin(black_screen
    dll_main.c
    black_screen.c
)

# The hook is ported from EnableFPSLimiter; the waiting is written here. winmm is for
# timeBeginPeriod, without which Sleep(1) can be Sleep(15).
add_fellowship_plugin(fps_limit
    dll_main.c
    fps_limit.c
)
target_link_libraries(fps_limit PRIVATE winmm)

# Not ported from anywhere: the engine's frame clock is GetTickCount, which cannot measure a
# modern frame at all, and this moves the Timer class onto QueryPerformanceCounter. It is the
# reason game_speed's constant was ever needed, and the one of these three that actually makes
# the picture smooth. See its header for the fourteen call sites and why only those fourteen.
add_fellowship_plugin(frame_timing
    dll_main.c
    frame_timing.c
)

# Wants the opposite of resolution_unlock at one site, exactly as the original pair does.
add_fellowship_plugin(windowed_res
    dll_main.c
    windowed_res.c
)

# ------------------------------------------------------------------------------ Fellowship.rfl
#
# These four install from a module watch rather than at the entry point, because the rfl is a
# DLL the game loads later and does not exist when the loader calls us.

add_fellowship_plugin(hud_scaling
    dll_main.c
    hud_scaling.c
)

# The same texel-into-pixel assignment, on the GUI texture control rather than the meter. One
# class, one site, and the destination size is a separate field from the source rectangle, which
# is why this one can be fixed where the in-game HUD geometry still cannot. See its README.
# A DIAGNOSTIC for the same class. Reads what the draw computed and prints it, because three
# attempts at scaling this path failed on inference where a measurement was needed.
add_fellowship_plugin(texture_probe
    dll_main.c
    texture_probe.c
)

add_fellowship_plugin(texture_scaling
    dll_main.c
    texture_scaling.c
)

add_fellowship_plugin(text_scaling
    dll_main.c
    text_scaling.c
)

# Two bytes, and the first plugin here that finds its site by SIGNATURE rather than by address,
# the retail rfl and the one this project targets put the same branch 0x430 apart. New Game opens
# the game's own level list, which is a finished screen nobody ever reaches. See its README.
add_fellowship_plugin(level_select
    dll_main.c
    level_select.c
)

# Three bytes that send the movie player's per-frame Update down the engine's own "this movie is
# not going to play" path, so the completion callback fires and the media manager forgets it. The
# opening movies go through the Windows Media Format runtime, which Wine only stubs, so under
# Proton or Lutris the movie neither plays nor fails and the game waits for it behind a black
# screen with a perfectly healthy Direct3D device. On under Wine, off on Windows, where the
# movies work; the ini overrides either way.
add_fellowship_plugin(movie_skip
    dll_main.c
    movie_skip.c
)

# ------------------------------------------------------- Direct3D 8, and the diagnostics
#
# Everything below reaches the running process, never a file on disk up front. Most of
# them wrap the calls the game makes into Direct3D; the probes only read and write down what
# they found. They are grouped here because none of them owns a site in the exe or the rfl.

# A DIAGNOSTIC that changes nothing: what platform this is, which Direct3D 8 is in the process,
# and what the game asked CreateDevice for and got back. Off by default. Turn it on when a
# machine you cannot see is misbehaving, and turn it off again afterwards.
add_fellowship_plugin(env_probe
    dll_main.c
    env_probe.c
)

# Rewrites the presentation parameters on their way into CreateDevice and Reset so the device is
# never exclusive full screen, and restyles the window to a borderless popup filling the screen.
# Exclusive full screen hands the window's fate to the focus, and this engine draws nothing while
# its window is down, which under Wine is a black screen rather than an alt-tab annoyance.
add_fellowship_plugin(borderless
    dll_main.c
    borderless.c
)

# The crudest diagnostic here, and the only one that PAINTS OVER THE GAME: it clears the back
# buffer to red, green and blue in turn, every frame. When frames are being presented and the
# screen is black, this is the one run that separates "the game is drawing nothing" from "nothing
# reaches the display", and it needs no keyboard, which matters on a handheld.
add_fellowship_plugin(screen_test
    dll_main.c
    screen_test.c
)

# A DIAGNOSTIC, not a fix, and off by default. Reads the engine's own frame mode word and frame
# counter from a thread of its own, and answers the question Direct3D cannot: when nothing is
# being presented, is the engine running its full frame and failing to show it, or is it taking
# the branch of its per-frame function that returns without drawing at all?
add_fellowship_plugin(frame_state
    dll_main.c
    frame_state.c
)

# A DIAGNOSTIC, not a fix, and off by default. It hooks the engine's universal property getter in
# Fellowship.exe and records which code asks for which authored value, the tool for finding the
# in-game HUD's draw path. See HUD-FINDING.md in hud_scaling.
add_fellowship_plugin(hud_probe
    dll_main.c
    hud_probe.c
)

# ------------------------------------------------------------------------------- interactive
#
# The only plugin that draws. It hooks IDirect3DDevice8::EndScene through the device vtable, and
# it does not install that hook until the toggle key is pressed for the first time, so a player
# who never opens it is running a DLL that read an ini and started a thread. gdi32 is for
# rasterising the font once, at that first press, on our own thread and never in the draw path.
add_fellowship_plugin(dev_menu
    dll_main.c
    dev_menu.c
    overlay.c
    cheats.c
    flags.c
    messages.c
    player.c
    timing.c
)
target_link_libraries(dev_menu PRIVATE gdi32)

# ------------------------------------------------------------------------------ Fellowship.rfl
# Off by default: only useful alongside a FOV mod that rewrites the focal numerator.
add_fellowship_plugin(inventory_icons
    dll_main.c
    inventory_icons.c
)
