#include "common/channel.h"

#include <windows.h>

#include <stdio.h>

static channel_block_t *g_block;
static HANDLE           g_mapping;

channel_block_t *channel_open(void)
{
    char             name[64];
    channel_block_t *view;

    if (g_block != NULL) {
        return g_block;
    }

    /* Local\ rather than Global\, and the process id in the name: this is a conversation between
     * DLLs inside one game, not between games and not across sessions. */
    sprintf(name, "Local\\FellowshipReborn.%lu", (unsigned long)GetCurrentProcessId());

    g_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                   CHANNEL_MAPPING_SIZE, name);
    if (g_mapping == NULL) {
        return NULL;
    }

    view = (channel_block_t *)MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                            CHANNEL_MAPPING_SIZE);
    if (view == NULL) {
        CloseHandle(g_mapping);
        g_mapping = NULL;
        return NULL;
    }

    /* A fresh mapping is zero-filled, so whoever gets here first stamps it and everyone after
     * finds it already stamped. Writing the magic unconditionally is harmless and avoids caring
     * which of the two plugins loaded first. */
    if (view->magic != CHANNEL_MAGIC) {
        view->field_of_view_degrees = 0.0f;
        view->field_of_view_serial  = 0;
        view->frame_target_fps      = 0.0f;
        view->frame_target_serial   = 0;
        view->version               = CHANNEL_VERSION;
        view->magic                 = CHANNEL_MAGIC;
    }

    g_block = view;
    return g_block;
}

void channel_publish_field_of_view(channel_block_t *block, float degrees)
{
    if (block == NULL) {
        return;
    }
    block->field_of_view_degrees = degrees;

    /* Bumped last, and after the value, so a reader that sees a new serial is guaranteed to be
     * looking at the value that goes with it. Aligned 32-bit stores are atomic on x86; the
     * ordering is what needs saying out loud, not the atomicity. */
    block->field_of_view_serial = block->field_of_view_serial + 1u;
}

bool channel_read_field_of_view(const channel_block_t *block, float *degrees)
{
    uint32_t before;
    uint32_t after;
    float    value;

    if (block == NULL || degrees == NULL) {
        return false;
    }
    if (block->magic != CHANNEL_MAGIC || block->version != CHANNEL_VERSION) {
        return false;
    }

    before = block->field_of_view_serial;
    value  = block->field_of_view_degrees;
    after  = block->field_of_view_serial;

    if (before != after || before == 0u) {
        return false;   /* torn, or nobody has ever published */
    }
    if (!(value >= 1.0f && value <= 179.0f)) {
        return false;   /* withdrawn, or nonsense: either way, use your own value */
    }

    *degrees = value;
    return true;
}

void channel_publish_frame_target(channel_block_t *block, float fps)
{
    if (block == NULL) {
        return;
    }
    block->frame_target_fps = fps;

    /* Value first, serial last, exactly as above: a reader that sees a new serial is guaranteed
     * to be looking at the value that goes with it. */
    block->frame_target_serial = block->frame_target_serial + 1u;
}

bool channel_read_frame_target(const channel_block_t *block, float *fps)
{
    uint32_t before;
    uint32_t after;
    float    value;

    if (block == NULL || fps == NULL) {
        return false;
    }
    if (block->magic != CHANNEL_MAGIC || block->version != CHANNEL_VERSION) {
        return false;
    }

    before = block->frame_target_serial;
    value  = block->frame_target_fps;
    after  = block->frame_target_serial;

    if (before != after || before == 0u) {
        return false;   /* torn, or nobody has ever published */
    }

    /* Exactly 0 is uncapped and is a request. Anything else has to be a rate this could have
     * meant; a NaN fails both comparisons and lands here, which is the point of writing it as a
     * range test rather than as its negation. */
    if (value == 0.0f) {
        *fps = 0.0f;
        return true;
    }
    if (value >= 10.0f && value <= 1000.0f) {
        *fps = value;
        return true;
    }
    return false;
}
