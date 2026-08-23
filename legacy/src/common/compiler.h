/* compiler.h: the places where a newer MSVC has to be told something, with the reasoning in
 * README.md, so the next person finds an explanation instead of a bare #pragma.
 */

#ifndef COMMON_COMPILER_H
#define COMMON_COMPILER_H

/* C4702 on the unreachable trailing return of a for(;;) thread. It comes from the code
 * generator, so it is attributed to the end of the function and a suppress pragma at the return
 * does not reach it; only a whole-function region does. See README.md. */
#if defined(_MSC_VER)
#define OF_NORETURN_THREAD_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4702))
#define OF_NORETURN_THREAD_END   __pragma(warning(pop))
#else
#define OF_NORETURN_THREAD_BEGIN
#define OF_NORETURN_THREAD_END
#endif

/* __thiscall is not usable on a function-pointer typedef in C, so engine methods are declared
 * __fastcall with a dead second parameter, which on x86 is exact and not approximate. Pass
 * OF_THISCALL_EDX for that parameter. See README.md. */
#define OF_THISCALL_EDX ((void *)0)

#endif /* COMMON_COMPILER_H */
