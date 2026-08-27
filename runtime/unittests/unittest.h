#ifndef UNITTEST_H
#define UNITTEST_H

#include <stddef.h>

/* Minimal check harness shared by every test in this directory.
 *
 * The tests are plain console programs that ctest runs: no fixtures, no discovery, no mocking.
 * Almost everything worth testing in this tree is either arithmetic with a known right answer or
 * a byte layout that either matches or does not, and neither needs a framework. What every test
 * does need is the same pass and fail bookkeeping, so that lives here once.
 *
 * Writing a test:
 *
 *     #include "unittest.h"
 *
 *     int main(void)
 *     {
 *         ut_section("displacement math");
 *         ut_check(bytes[0] == 0xE9, "an E9 is emitted first");
 *
 *         return ut_summary("emit");
 *     }
 *
 * Every check prints its line whether it passed or failed. A passing run that lists what it
 * proved is worth more than a silent one, because the check text is often the only place a
 * byte-level assumption is written down in English.
 */

/* Prints a heading and groups the checks that follow it. Optional. */
void ut_section(const char *name);

/* The one check. Records a failure when `condition` is zero and prints the line either way.
 * `what` should read as a claim about the code, not a label: "a short branch that cannot reach
 * marks the buffer overflowed", not "test rel8 overflow". */
void ut_check(int condition, const char *what);

/* Same, with a printf-style message, for checks inside a loop where the case has to be named
 * by its numbers. */
void ut_checkf(int condition, const char *format, ...);

/* Float comparison with an explicit tolerance. NaN on either side fails, including both. */
void ut_near(double actual, double expected, double tolerance, const char *what);

/* Prints the totals and returns the process exit code: 0 when everything passed, 1 otherwise. */
int ut_summary(const char *suite);

/* How many checks have failed so far, for a test that has to stop instead of running on into a
 * state its earlier checks just proved is broken. */
size_t ut_failures(void);

#endif /* UNITTEST_H */
