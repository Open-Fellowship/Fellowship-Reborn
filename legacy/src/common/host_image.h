/* host_image.h: the geometry and the location of the process that hosts us.
 *
 * One responsibility: answer "where is the main executable, where is its code section, and which
 * directory did it come from". Everything that scans, range-checks or writes engine memory needs
 * those answers, and nothing else in `common` should be computing them a second time.
 *
 * host_image_resolve() is idempotent and must be called before any other function here returns
 * anything useful. The loader does that once, from DllMain, because it only reads PE headers
 * that are already mapped and is therefore safe under the loader lock.
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
