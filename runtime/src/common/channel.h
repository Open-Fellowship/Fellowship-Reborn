/* channel.h: one small block of memory that plugins can agree on.
 *
 * Plugins are independent DLLs, so when two of them want the same engine field one PUBLISHES a
 * request and the other PREFERS it: one writer for the engine, one writer for the request.
 *
 * A named file mapping, not an exported symbol, so neither DLL has to load before the other.
 * See README.md.
 */
#ifndef COMMON_CHANNEL_H
#define COMMON_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#define CHANNEL_MAGIC   0x4843464Fu   /* 'OFCH' */
#define CHANNEL_VERSION 2u

/* A whole page, NOT sizeof(channel_block_t): CreateFileMappingA on an existing name fails when
 * the requested size is larger than the existing object, so a new DLL beside an old one could
 * not open the block at all. Every field added after this one is free. */
#define CHANNEL_MAPPING_SIZE 4096u

typedef struct channel_block {
    uint32_t magic;
    uint32_t version;

    /* Vertical field of view in degrees, or 0 for "nobody is asking". `serial` is bumped after
     * every write, so a reader can tell a fresh request from a stale one without a lock: read
     * serial, read the value, read serial again, and try later if it moved. */
    volatile float    field_of_view_degrees;
    volatile uint32_t field_of_view_serial;

    /* Zero is a REQUEST here, not a withdrawal: it means uncapped. That is the opposite of
     * the field of view convention above, on purpose. A serial of zero means nobody has ever
     * published. */
    volatile float    frame_target_fps;
    volatile uint32_t frame_target_serial;

    /* view_distance's five controls, published together because they are one decision and a
     * reader that saw three of them applied would draw a world the other two disagree with.
     * `cells` and `fade` are the two floats its patched instructions already read; `flags`
     * carries the three that are branches, not numbers. Serial 0 means nobody is
     * asking, so a block created before these existed reads as released. */
    volatile float    view_distance_cells;
    volatile float    view_distance_fade;
    volatile uint32_t view_distance_flags;
    volatile uint32_t view_distance_serial;
} channel_block_t;

#define VIEW_DISTANCE_FLAG_FAR_PLANE  0x1u
#define VIEW_DISTANCE_FLAG_FADE_CAP   0x2u
#define VIEW_DISTANCE_FLAG_PRELOAD    0x4u

/* Creates the block if it does not exist and maps it. Returns NULL if the mapping could not be
 * made, which every caller must treat as "carry on without a partner" rather than as an error. */
channel_block_t *channel_open(void);

/* Publish, then bump the serial. Degrees of 0 withdraws the request. */
void channel_publish_field_of_view(channel_block_t *block, float degrees);

/* Reads the request, returning false when there is none or when the block is torn mid-write.
 * A false return means "use your own value", never "something is wrong". */
bool channel_read_field_of_view(const channel_block_t *block, float *degrees);

/* Publish a frame rate target. 0 means uncapped; anything else is clamped to 10..1000 by the
 * reader, not here, for the same reason ini.h validates nothing. */
void channel_publish_frame_target(channel_block_t *block, float fps);

/* False when nobody has published, when the block is torn, or when the value is not one this
 * could have written. `fps` is set to 0 for uncapped. */
bool channel_read_frame_target(const channel_block_t *block, float *fps);

/* Publishes all five of view_distance's controls as one request. Cells of 0 withdraws it and
 * hands the plugin back to its ini values. */
void channel_publish_view_distance(channel_block_t *block, float cells, float fade,
                                   uint32_t flags);

/* True when a request is live, with the three values written through the pointers. */
bool channel_read_view_distance(const channel_block_t *block, float *cells, float *fade,
                                uint32_t *flags);

#endif /* COMMON_CHANNEL_H */
