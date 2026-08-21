/* channel.h: one small block of memory that plugins can agree on.
 *
 * Plugins are deliberately independent DLLs. Nothing loads anything else, nothing exports
 * anything to anything else, and deleting one cannot break another - that is the whole point of
 * the layout. It is also a problem the first time two of them want the same engine field.
 *
 * `dev_menu` is that first time. Its field-of-view slider and the `field_of_view` plugin both
 * want to write the camera's focal length, and whichever ran last would win: drag the slider and
 * 400 milliseconds later the poll thread puts it back.
 *
 * So they do not both write. `dev_menu` PUBLISHES a target here and `field_of_view` PREFERS it
 * over its own ini value when one is present, keeping its re-apply so the number still survives
 * a level load. One writer for the camera, one writer for the request.
 *
 * The block is a named file mapping rather than an exported symbol, because a mapping needs no
 * load-order relationship between the two DLLs: either can create it, either can open it, and a
 * plugin whose partner is not installed simply reads a block nobody ever writes to. The name
 * carries the process id, so two copies of the game running at once do not talk to each other.
 */
#ifndef COMMON_CHANNEL_H
#define COMMON_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#define CHANNEL_MAGIC   0x4843464Fu   /* 'OFCH' */
#define CHANNEL_VERSION 2u

/* The mapping is a whole page rather than sizeof(channel_block_t).
 *
 * It used to be the structure's own size, and adding the frame rate field to it is what showed
 * why that was wrong: CreateFileMappingA on a name that already exists fails outright when the
 * requested size is larger than the existing object, so a new DLL next to an old one could not
 * open the block at all. Both sides then fell back to "no partner", which is a safe failure but
 * an avoidable one. A fixed page means every field added after this one costs nothing. */
#define CHANNEL_MAPPING_SIZE 4096u

typedef struct channel_block {
    uint32_t magic;
    uint32_t version;

    /* Vertical field of view in degrees, or 0 for "nobody is asking". `serial` is bumped after
     * every write, so a reader can tell a fresh request from a stale one without a lock: read
     * serial, read the value, read serial again, and try later if it moved. */
    volatile float    field_of_view_degrees;
    volatile uint32_t field_of_view_serial;

    /* Frames per second that fps_limit should aim for, published by the dev menu's slider.
     *
     * Zero is a REQUEST here, not a withdrawal: it means uncapped. That is the opposite of the
     * field of view convention above, and deliberately so, because "no cap" is a thing a person
     * chooses and "no opinion about the field of view" is not. A serial of zero is what means
     * nobody has ever published. */
    volatile float    frame_target_fps;
    volatile uint32_t frame_target_serial;
} channel_block_t;

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

#endif /* COMMON_CHANNEL_H */
