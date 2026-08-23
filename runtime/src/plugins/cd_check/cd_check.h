/* cd_check.h: make the disc check succeed without a disc.
 *
 * Ported from the community patcher ([Options] SkipCDCheck). The call at 0x406439 is redirected
 * to a stub returning 1. The callee at 0x4BD2C0 is not modified. See README.md.
 */
#ifndef CD_CHECK_H
#define CD_CHECK_H

void cd_check_install(void);

#endif /* CD_CHECK_H */
