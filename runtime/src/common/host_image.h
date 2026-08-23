/* host_image.h: where the host executable is, where its code section is, and which directory
 * it came from. host_image_resolve() is idempotent and must be called before anything else here
 * returns anything useful. See README.md.
 */
#ifndef COMMON_HOST_IMAGE_H
#define COMMON_HOST_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Locates the main module and its first code section. Returns false on anything that is not a
 * 32-bit PE, which is the safe answer: every caller then refuses to patch. */
bool host_image_resolve(void);

uintptr_t host_image_base(void);
uintptr_t host_image_end(void);      /* base + SizeOfImage, exclusive */
uintptr_t host_image_text(void);     /* VA of the first section carrying CNT_CODE */
size_t    host_image_text_size(void);

/* The directory of the host executable, WITH a trailing backslash. Empty string if unknown.
 * This is the game folder: where the shared ini and the shared log live. A plugin DLL itself
 * sits one level down, in <game>\plugins\. */
const char *host_directory(void);

/* Full path of the host executable. Empty string if unknown. */
const char *host_path(void);

#endif /* COMMON_HOST_IMAGE_H */
