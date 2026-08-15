/* cd_check.h: make the disc check succeed without a disc.
 *
 * Ported from the community patcher ([Options] SkipCDCheck). One call site:
 *
 *     0x406439   call 0x4BD2C0        result tested with `test eax,eax / jne`
 *
 * The patcher redirects that call's displacement at a function of its own that returns 1. This
 * does the same thing, which is worth stating precisely: the callee at 0x4BD2C0 is NOT modified,
 * so any other caller it has keeps the original behaviour. Only this one site is diverted.
 */
#ifndef CD_CHECK_H
#define CD_CHECK_H

void cd_check_install(void);

#endif /* CD_CHECK_H */
