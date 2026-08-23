/* messages.h: catch what the engine prints, so it has somewhere to go.
 *
 * The engine hands its messages to an object whose own display draws nothing on this build. This
 * hooks that object's vtable, keeps the last few hundred lines in a ring, and passes every
 * message on to the original afterwards. Nothing is swallowed. See README.md.
 */
#ifndef DEV_MENU_MESSAGES_H
#define DEV_MENU_MESSAGES_H

#include <stdbool.h>

#define MESSAGE_LINES  512
#define MESSAGE_LENGTH 160

/* Capture starts as early as the engine has an object to hook, NOT when the box is opened:
 * nearly everything this engine prints, it prints while loading. `enabled` is whether to capture
 * at all; whether the box is on screen is dev_menu's business. */
void messages_set_enabled(bool enabled);
bool messages_enabled(void);

/* Mirrors every captured line into fix_enhancers.log as well as the ring.
 *
 * The ring is for reading on screen, which is no use at all on a machine that never draws
 * anything, and a game that stops drawing is the case where what the engine said last matters
 * most. Off by default because it is a great deal of text, and the file is the only place it can
 * go when the screen is black. Per-frame statistics are NOT mirrored; they would be the whole
 * file within a minute. */
void messages_set_logging(bool enabled);

/* Idempotent, and safe to call every frame until it succeeds: the message object does not exist
 * at the entry point and appears during start-up. Does nothing while disabled. */
bool messages_install(void);

bool messages_installed(void);

/* Everything that has come past, including the lines whose channel is switched off. */
unsigned messages_total(void);

/* How many are actually in the ring, which is the ones whose channels were on when they arrived. */
unsigned messages_kept(void);

/* `age` 0 is the newest line. Returns NULL past the end of what is held.
 *
 * `from_stats` distinguishes the per-frame statistics rows, which arrive once a frame each and
 * would otherwise push a loading warning out of the ring within a second, from everything else.
 * Pass NULL if the distinction does not matter.
 */
const char *messages_line(unsigned age);
const char *messages_line_ex(unsigned age, bool *from_stats);

/* THE LIVE ROWS. The statistics are not events: the engine prints the whole information block
 * every frame whatever the flags say, so as a scrolling list they are thousands of copies of the
 * same eight things. Kept as a table keyed on the text before the colon, replaced in place, and
 * dropped after LIVE_TIMEOUT_MS so a flag going off removes its row. */
#define MESSAGE_LIVE_MAX   24
#define LIVE_TIMEOUT_MS    1500u

unsigned    messages_live_count(void);
const char *messages_live(unsigned index);

/* THE CHANNELS. Every line is filed under the text before its colon, and that key becomes a
 * channel with its own switch. The list builds itself from what the engine actually prints, so
 * the capture can stay indiscriminate. */
#define MESSAGE_CHANNELS 48

unsigned    messages_channel_count(void);
const char *messages_channel_name(unsigned index);
unsigned    messages_channel_hits(unsigned index);
bool        messages_channel_enabled(unsigned index);
void        messages_channel_set(unsigned index, bool enabled);
void        messages_channels_all(bool enabled);

/* For the box: is this line's channel switched on? */
bool messages_text_enabled(const char *text);

void messages_clear(void);

#endif /* DEV_MENU_MESSAGES_H */
