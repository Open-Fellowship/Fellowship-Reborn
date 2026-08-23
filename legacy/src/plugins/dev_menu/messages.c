#include "messages.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================================================================================== the sink
 *
 * The engine prints through one object, held at 0x543784, and always the same way:
 *
 *     004040EB   mov  eax,[0x543784]
 *                push 0x52E720            "RIOT Engine core initialized."
 *                push eax                 the object, as the FIRST STACK ARGUMENT
 *                mov  ecx,[eax]
 *                ff 51 08                 call [ecx+8]
 *
 *     0040B88D   push edx                 an argument
 *                push 0x52E9D8            "Object Totals: (%d objs)"
 *                push eax                 the object again
 *                ff 51 20                 call [ecx+0x20]
 *                add  esp,8               the caller cleans up
 *
 * Two entry points, sixty-five call sites between them. `this` goes on the stack rather than in
 * ecx and the caller cleans up, which is how MSVC compiles a member function that takes varargs,
 * and it is what makes these two hookable with plain C functions, no naked thunks, no assembler.
 *
 *     +0x08   print(self, const char *text)
 *     +0x20   printf(self, const char *format, ...)
 *
 * Slots 0x0C, 0x14 and 0x18 are used elsewhere on the same object and are left alone. This
 * records what goes past and then calls the original, so the engine still does whatever it did.
 */
#define MESSAGE_OBJECT_PTR_VA 0x00543784u
#define SLOT_STATS            (0x00u / 4u)
#define SLOT_PRINT            (0x08u / 4u)
#define SLOT_WARN             (0x0Cu / 4u)
#define SLOT_PRINTF           (0x20u / 4u)

/* SLOT 0 IS THE PER-FRAME ONE, and missing it is why turning on "Display Num Lights" changed
 * nothing in the box while the loading messages arrived perfectly well:
 *
 *     0041398F   mov  eax,[0x543784]
 *                fstp qword ptr [esp]      the frame rate, as a double
 *                push 0x52F838             "FPS: %5.2f"
 *                push eax
 *                call [ecx]                <- slot 0, not 8, not 0x20
 *
 *     00413A0E   push 0x52F818             "TEX: %dkb/%dkb"
 *                push eax
 *                call [edx]
 *
 *     00413A93   push 0x52F7F4             "XYZ: %d,%d,%d"
 *                push esi
 *                call [edi]
 *
 * Every statistics row the debug flags switch on goes through it, once a frame each. It is a
 * printf with the same shape as the other two, and this is the slot that makes those flags
 * visible at all on a build whose own display draws nothing.
 */

/* Slot 0x0C is a SECOND printf on the same object, and it is the one that carries the warnings:
 *
 *     00404070   mov  eax,[0x543784]
 *                push 0x52E740          "Input initialization failed"
 *                push eax
 *                call [ecx+0xC]
 *
 *     0042B6EB   mov  ecx,[0x543784]
 *                push eax               the index
 *                push 0x537A78          "LOAD/SAVE: Invalid Object Pointer Table Index: %d"
 *                push ecx
 *                call [edx+0xC]
 *
 * Same shape as 0x20, same convention, and where "RFL initialization failed!", "Run always list
 * is corrupted" and "An object which has been freed is trying to be saved!" go. Missing it meant
 * missing exactly the lines worth having.
 *
 * Slots 0x14 and 0x18 are NOT text. 0x14 takes two numbers and 0x18 takes nothing; they are the
 * display's own positioning and clearing, and they are left alone.
 */

typedef void (__cdecl *print_fn)(void *self, const char *text);
typedef void (__cdecl *printf_fn)(void *self, const char *format, ...);

static print_fn         g_original_print;
static printf_fn        g_original_printf;
static printf_fn        g_original_warn;
static printf_fn        g_original_stats;
static void           **g_vtable;
static bool             g_installed;
static bool             g_failed;
static bool             g_enabled;

static CRITICAL_SECTION g_lock;
static bool             g_lock_ready;
static char             g_lines[MESSAGE_LINES][MESSAGE_LENGTH];
static bool             g_from_stats[MESSAGE_LINES];

typedef struct live_row {
    char  key[16];
    char  text[MESSAGE_LENGTH];
    DWORD when;
} live_row_t;

static live_row_t g_live[MESSAGE_LIVE_MAX];

typedef struct channel {
    char     key[16];
    unsigned hits;
    bool     enabled;
} channel_t;

static channel_t g_channels[MESSAGE_CHANNELS];
static unsigned  g_channel_count;

/* The key is the text before the colon, which is how this engine writes nearly every line it
 * prints. A line with no colon keys on its first few characters instead, which groups the
 * loading messages sensibly enough and is better than one channel per sentence. */
static void key_of(const char *text, char *key, size_t size)
{
    size_t i = 0;

    while (text[i] != '\0' && text[i] != ':' && i + 1 < size) {
        key[i] = text[i];
        ++i;
    }
    key[i] = '\0';
}

/* Called with the lock held. */
static channel_t *channel_for(const char *text)
{
    char     key[16];
    unsigned i;

    key_of(text, key, sizeof(key));
    if (key[0] == '\0') {
        return NULL;
    }

    for (i = 0; i < g_channel_count; ++i) {
        if (strcmp(g_channels[i].key, key) == 0) {
            return &g_channels[i];
        }
    }
    if (g_channel_count >= MESSAGE_CHANNELS) {
        return NULL;
    }

    _snprintf(g_channels[g_channel_count].key, sizeof(g_channels[0].key), "%s", key);
    g_channels[g_channel_count].key[sizeof(g_channels[0].key) - 1] = '\0';
    g_channels[g_channel_count].hits    = 0;
    g_channels[g_channel_count].enabled = true;      /* new channels start visible */
    return &g_channels[g_channel_count++];
}

unsigned messages_channel_count(void)
{
    return g_channel_count;
}

const char *messages_channel_name(unsigned index)
{
    return (index < g_channel_count) ? g_channels[index].key : NULL;
}

unsigned messages_channel_hits(unsigned index)
{
    return (index < g_channel_count) ? g_channels[index].hits : 0;
}

bool messages_channel_enabled(unsigned index)
{
    return (index < g_channel_count) && g_channels[index].enabled;
}

void messages_channel_set(unsigned index, bool enabled)
{
    if (index < g_channel_count) {
        g_channels[index].enabled = enabled;
    }
}

void messages_channels_all(bool enabled)
{
    unsigned i;

    for (i = 0; i < g_channel_count; ++i) {
        g_channels[i].enabled = enabled;
    }
}

bool messages_text_enabled(const char *text)
{
    char     key[16];
    unsigned i;

    if (text == NULL) {
        return false;
    }
    key_of(text, key, sizeof(key));

    for (i = 0; i < g_channel_count; ++i) {
        if (strcmp(g_channels[i].key, key) == 0) {
            return g_channels[i].enabled;
        }
    }
    return true;                                     /* not seen yet: show it */
}
static bool             g_recording_stats;   /* set around the slot 0 hook only */
static unsigned         g_written;      /* into the ring */
static unsigned         g_seen;         /* everything, filtered or not */
static bool             g_mirror_to_log;

void messages_set_logging(bool enabled)
{
    g_mirror_to_log = enabled;
}

/* The log gets the line before the channels get a say. A muted channel is a line you do not want
 * filling the box on screen; it is not a line you want missing from a file you are reading
 * precisely because the screen never came on. */
static void mirror_to_log(const char *text)
{
    char   line[MESSAGE_LENGTH];
    size_t length;

    if (!g_mirror_to_log || text == NULL) {
        return;
    }

    _snprintf(line, sizeof(line), "%s", text);
    line[sizeof(line) - 1] = '\0';

    length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
    if (line[0] == '\0') {
        return;
    }

    log_info("engine: %s", line);
}

/* The engine prints from whichever thread is loading or running, and the overlay reads on the
 * render thread, so this is the one place in this plugin that genuinely needs a lock. It is held
 * for a memcpy. */
static void record(const char *text)
{
    size_t length;
    char  *slot;

    if (text == NULL || !g_lock_ready) {
        return;
    }

    mirror_to_log(text);

    EnterCriticalSection(&g_lock);

    ++g_seen;

    {
        channel_t *channel = channel_for(text);

        if (channel != NULL) {
            ++channel->hits;

            /* A muted channel is counted and then dropped rather than stored. Otherwise the ring
             * fills with the lines you have just said you do not want to see, and the ones you do
             * scroll off behind them. */
            if (!channel->enabled) {
                LeaveCriticalSection(&g_lock);
                return;
            }
        }
    }

    slot = g_lines[g_written % MESSAGE_LINES];
    g_from_stats[g_written % MESSAGE_LINES] = g_recording_stats;
    length = strlen(text);
    if (length >= MESSAGE_LENGTH) {
        length = MESSAGE_LENGTH - 1;
    }
    memcpy(slot, text, length);
    slot[length] = '\0';

    /* Trailing newlines would draw as a box glyph, and several of these strings carry one. */
    while (length > 0 && (slot[length - 1] == '\n' || slot[length - 1] == '\r')) {
        slot[--length] = '\0';
    }

    ++g_written;

    LeaveCriticalSection(&g_lock);
}

/* ============================================================ formatting, done defensively
 *
 * The first version handed the engine's format string straight to the CRT's vsnprintf. That is
 * not safe here. These strings are the engine's own, several carry a leading control byte, and
 * anything the CRT does not understand the way this engine's own printf understands it means a
 * value read as a pointer and dereferenced. A debug overlay must not be able to take the game
 * down; it is the one thing in this project that runs while everything else is working.
 *
 * So the walk is ours. Known conversions only, one at a time, and every %s pointer is checked
 * for readability before it is touched. Anything unrecognised is copied through as literal text
 * rather than consumed as an argument, which loses a value at worst.
 */
static void append(char *out, size_t size, size_t *used, const char *text)
{
    while (*text != '\0' && *used + 1 < size) {
        out[(*used)++] = *text++;
    }
    out[*used] = '\0';
}

static void format_safely(char *out, size_t size, const char *format, va_list arguments)
{
    size_t used = 0;

    out[0] = '\0';
    if (format == NULL || !memory_is_readable_range((uintptr_t)format, 1)) {
        append(out, size, &used, "(unreadable format string)");
        return;
    }

    while (*format != '\0' && used + 1 < size) {
        char spec[32];
        char piece[MESSAGE_LENGTH];
        int  length = 0;
        bool wide   = false;

        if (*format != '%') {
            out[used++] = *format++;
            out[used]   = '\0';
            continue;
        }

        spec[length++] = *format++;
        if (*format == '%') {
            out[used++] = '%';
            out[used]   = '\0';
            ++format;
            continue;
        }

        /* flags, width, precision, copied verbatim into the spec we hand to snprintf */
        while (*format != '\0' && length < (int)sizeof(spec) - 2 &&
               (strchr("-+ #0123456789.*", *format) != NULL)) {
            spec[length++] = *format++;
        }
        /* length modifiers: noted for wide strings, otherwise dropped */
        while (*format == 'h' || *format == 'l' || *format == 'L' || *format == 'w') {
            if (*format == 'l' || *format == 'w') { wide = true; }
            ++format;
        }

        spec[length++] = *format;
        spec[length]   = '\0';

        switch (*format) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
            _snprintf(piece, sizeof(piece), spec, va_arg(arguments, int));
            break;
        case 'c':
            _snprintf(piece, sizeof(piece), spec, va_arg(arguments, int));
            break;
        case 'f': case 'g': case 'G': case 'e': case 'E':
            _snprintf(piece, sizeof(piece), spec, va_arg(arguments, double));
            break;
        case 'p':
            _snprintf(piece, sizeof(piece), "%08X", (unsigned)(uintptr_t)va_arg(arguments, void *));
            break;
        case 's': {
            void *text = va_arg(arguments, void *);

            if (text == NULL || !memory_is_readable_range((uintptr_t)text, 1)) {
                _snprintf(piece, sizeof(piece), "%s", "(bad string)");
            } else if (wide) {
                char narrow[MESSAGE_LENGTH];
                int  converted = WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)text, -1,
                                                     narrow, (int)sizeof(narrow) - 1, NULL, NULL);
                narrow[(converted > 0) ? converted : 0] = '\0';
                _snprintf(piece, sizeof(piece), "%s", narrow);
            } else {
                _snprintf(piece, sizeof(piece), "%s", (const char *)text);
            }
            break;
        }
        default:
            /* Not a conversion this understands. Copied as text, and crucially NO argument is
             * taken: consuming one on a guess is how the rest of the list turns into garbage. */
            _snprintf(piece, sizeof(piece), "%s", spec);
            break;
        }

        piece[sizeof(piece) - 1] = '\0';
        append(out, size, &used, piece);
        ++format;
    }
}

static void __cdecl hooked_print(void *self, const char *text)
{
    if (text != NULL && memory_is_readable_range((uintptr_t)text, 1)) {
        record(text);
    }
    if (g_original_print != NULL) {
        g_original_print(self, text);
    }
}

/* Keyed on the text before the colon, so "FPS: 57.14" replaces "FPS: 61.02" rather than joining
 * it. A line with no colon keys on its first few characters, which is close enough for the
 * handful that have none. */
static void record_live(const char *text)
{
    size_t     i;
    char       key[16];
    size_t     length = 0;
    DWORD      now    = GetTickCount();
    live_row_t *oldest = NULL;

    if (text == NULL || text[0] == '\0' || !g_lock_ready) {
        return;
    }

    while (text[length] != '\0' && text[length] != ':' && length < sizeof(key) - 1) {
        key[length] = text[length];
        ++length;
    }
    key[length] = '\0';

    EnterCriticalSection(&g_lock);

    {
        channel_t *channel = channel_for(text);
        if (channel != NULL) { ++channel->hits; }
    }

    for (i = 0; i < MESSAGE_LIVE_MAX; ++i) {
        if (g_live[i].key[0] != '\0' && strcmp(g_live[i].key, key) == 0) {
            _snprintf(g_live[i].text, sizeof(g_live[i].text), "%s", text);
            g_live[i].text[sizeof(g_live[i].text) - 1] = '\0';
            g_live[i].when = now;
            LeaveCriticalSection(&g_lock);
            return;
        }
        if (g_live[i].key[0] == '\0') {
            oldest = &g_live[i];
            break;
        }
        if (oldest == NULL || (now - g_live[i].when) > (now - oldest->when)) {
            oldest = &g_live[i];
        }
    }

    if (oldest != NULL) {
        _snprintf(oldest->key, sizeof(oldest->key), "%s", key);
        _snprintf(oldest->text, sizeof(oldest->text), "%s", text);
        oldest->key[sizeof(oldest->key) - 1]   = '\0';
        oldest->text[sizeof(oldest->text) - 1] = '\0';
        oldest->when = now;
    }

    LeaveCriticalSection(&g_lock);
}

unsigned messages_live_count(void)
{
    DWORD    now   = GetTickCount();
    unsigned count = 0;
    unsigned i;

    for (i = 0; i < MESSAGE_LIVE_MAX; ++i) {
        if (g_live[i].key[0] != '\0' && (now - g_live[i].when) < LIVE_TIMEOUT_MS) {
            ++count;
        }
    }
    return count;
}

const char *messages_live(unsigned index)
{
    DWORD    now  = GetTickCount();
    unsigned seen = 0;
    unsigned i;

    for (i = 0; i < MESSAGE_LIVE_MAX; ++i) {
        if (g_live[i].key[0] == '\0' || (now - g_live[i].when) >= LIVE_TIMEOUT_MS) {
            continue;
        }
        if (seen == index) {
            return g_live[i].text;
        }
        ++seen;
    }
    return NULL;
}

static void __cdecl hooked_stats(void *self, const char *format, ...)
{
    char    buffer[MESSAGE_LENGTH];
    va_list arguments;

    va_start(arguments, format);
    format_safely(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    /* The live table only.
     *
     * Putting these in the ring as well was tried and taken back out: it just prints the same
     * eight values twice, once as live rows and once as a waterfall underneath, and 15,000 of the
     * 15,554 lines in the ring were the frame rate. The channels can mute them, but the default
     * has to be the useful one.
     *
     * The cost is a one-off line arriving through this slot living only as long as the live row,
     * a second and a half. Nothing seen so far arrives that way; if something does, this is the
     * comment that explains where it went. */
    record_live(buffer);

    if (g_original_stats != NULL) {
        g_original_stats(self, "%s", buffer);
    }
}

static void __cdecl hooked_warn(void *self, const char *format, ...)
{
    char    buffer[MESSAGE_LENGTH];
    va_list arguments;

    va_start(arguments, format);
    format_safely(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    record(buffer);

    if (g_original_warn != NULL) {
        g_original_warn(self, "%s", buffer);
    }
}

static void __cdecl hooked_printf(void *self, const char *format, ...)
{
    char    buffer[MESSAGE_LENGTH];
    va_list arguments;

    va_start(arguments, format);
    format_safely(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    record(buffer);

    /* Forwarded as already-formatted text. C gives no portable way to pass varargs along, and
     * re-running the engine's own formatter on our output would be a second chance to go wrong
     * for no gain. */
    if (g_original_printf != NULL) {
        g_original_printf(self, "%s", buffer);
    }
}

bool messages_installed(void)
{
    return g_installed;
}

void messages_set_enabled(bool enabled)
{
    if (enabled == g_enabled) {
        return;
    }
    g_enabled = enabled;

    if (!enabled && g_installed) {
        DWORD protection = 0;

        /* Put the engine's own two entries back. Safe because nothing else in this project hooks
         * this object, and because the originals were saved rather than recomputed. */
        if (VirtualProtect(g_vtable, (SLOT_PRINTF + 1u) * sizeof(void *), PAGE_READWRITE,
                           &protection)) {
            g_vtable[SLOT_STATS]  = (void *)g_original_stats;
            g_vtable[SLOT_PRINT]  = (void *)g_original_print;
            g_vtable[SLOT_WARN]   = (void *)g_original_warn;
            g_vtable[SLOT_PRINTF] = (void *)g_original_printf;
            VirtualProtect(g_vtable, (SLOT_PRINTF + 1u) * sizeof(void *), protection, &protection);
            g_installed = false;
            log_info("engine messages: hooks removed");
        }
    }
}

bool messages_enabled(void)
{
    return g_enabled;
}

bool messages_install(void)
{
    uintptr_t object = 0;
    uintptr_t vtable = 0;
    DWORD     protection = 0;

    if (!g_enabled)  { return false; }
    if (g_installed) { return true; }
    if (g_failed)    { return false; }

    if (!host_image_resolve()) {
        return false;
    }
    if (!memory_read_u32(exe_site(MESSAGE_OBJECT_PTR_VA), (uint32_t *)&object) || object == 0) {
        return false;                          /* not built yet; try again next frame */
    }
    if (!memory_is_readable_range(object, 4) ||
        !memory_read_u32(object, (uint32_t *)&vtable) || vtable == 0) {
        return false;
    }
    if (!memory_is_readable_range(vtable, (SLOT_PRINTF + 1u) * 4u)) {
        return false;
    }

    g_vtable = (void **)vtable;

    /* Both entries have to look like code in the executable before either is replaced. A vtable
     * that passed the reads above but dispatches somewhere else is not this class. */
    if (!memory_is_inside_image((uintptr_t)g_vtable[SLOT_STATS], 1) ||
        !memory_is_inside_image((uintptr_t)g_vtable[SLOT_PRINT], 1) ||
        !memory_is_inside_image((uintptr_t)g_vtable[SLOT_WARN], 1) ||
        !memory_is_inside_image((uintptr_t)g_vtable[SLOT_PRINTF], 1)) {
        g_failed = true;
        log_error("the message object's vtable does not point into Fellowship.exe, not hooking");
        return false;
    }

    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }

    if (!VirtualProtect(g_vtable, (SLOT_PRINTF + 1u) * sizeof(void *), PAGE_READWRITE,
                        &protection)) {
        g_failed = true;
        log_error("the message vtable could not be made writable, not hooking");
        return false;
    }

    g_original_stats  = (printf_fn)g_vtable[SLOT_STATS];
    g_original_print  = (print_fn)g_vtable[SLOT_PRINT];
    g_original_warn   = (printf_fn)g_vtable[SLOT_WARN];
    g_original_printf = (printf_fn)g_vtable[SLOT_PRINTF];
    g_vtable[SLOT_STATS]  = (void *)hooked_stats;
    g_vtable[SLOT_PRINT]  = (void *)hooked_print;
    g_vtable[SLOT_WARN]   = (void *)hooked_warn;
    g_vtable[SLOT_PRINTF] = (void *)hooked_printf;

    VirtualProtect(g_vtable, (SLOT_PRINTF + 1u) * sizeof(void *), protection, &protection);

    g_installed = true;
    log_info("engine messages: object %08X vtable %08X, stats %08X, print %08X, warn %08X, "
             "printf %08X", (unsigned)object, (unsigned)vtable,
             (unsigned)(uintptr_t)g_original_stats, (unsigned)(uintptr_t)g_original_print,
             (unsigned)(uintptr_t)g_original_warn, (unsigned)(uintptr_t)g_original_printf);
    return true;
}

unsigned messages_total(void)
{
    return g_seen;
}

unsigned messages_kept(void)
{
    return g_written;
}

const char *messages_line_ex(unsigned age, bool *from_stats)
{
    unsigned written = g_written;
    unsigned slot;

    if (age >= MESSAGE_LINES || age >= written) {
        return NULL;
    }

    slot = (written - 1u - age) % MESSAGE_LINES;
    if (from_stats != NULL) {
        *from_stats = g_from_stats[slot];
    }
    return g_lines[slot];
}

const char *messages_line(unsigned age)
{
    return messages_line_ex(age, NULL);
}

void messages_clear(void)
{
    if (!g_lock_ready) {
        return;
    }
    EnterCriticalSection(&g_lock);
    g_written = 0;
    memset(g_lines, 0, sizeof(g_lines));
    LeaveCriticalSection(&g_lock);
}
