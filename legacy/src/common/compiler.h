/* compiler.h, the small number of places where a newer MSVC has to be told something.
 *
 * This tree targets a 2002 game and was written against an older toolset. Nothing about the game
 * changed; the compiler did. Rather than let those differences accumulate as ad-hoc pragmas
 * scattered through the plugins, they live here with the reasoning attached, so the next person
 * to hit one finds an explanation instead of a bare `#pragma`.
 */

#ifndef COMMON_COMPILER_H
#define COMMON_COMPILER_H

/* Every poll thread in this tree has the same shape: a `for (;;)` that never breaks, and a
 * trailing `return` that exists only because the thread signature demands one. MSVC 19.50 proves
 * the return is unreachable and issues C4702, which `/WX` turns into an error; the toolset this
 * was written against did not.
 *
 * C4702 comes from the code generator rather than the parser, so it is attributed to the end of
 * the function and `#pragma warning(suppress : 4702)` at the return does not reach it. Only a
 * region covering the whole function does, which is what these two are for:
 *
 *     OF_NORETURN_THREAD_BEGIN
 *     static DWORD WINAPI watch_thread(LPVOID parameter) { ... }
 *     OF_NORETURN_THREAD_END
 *
 * The alternative, deleting the return and marking the function `__declspec(noreturn)`, does
 * not work here, because `LPTHREAD_START_ROUTINE` requires the DWORD return and a noreturn
 * function cannot be assigned to it without a cast that hides more than it explains.
 */
#if defined(_MSC_VER)
#define OF_NORETURN_THREAD_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4702))
#define OF_NORETURN_THREAD_END   __pragma(warning(pop))
#else
#define OF_NORETURN_THREAD_BEGIN
#define OF_NORETURN_THREAD_END
#endif

/* MSVC accepts `__thiscall` on a function-pointer typedef in C++ but not in C, under
 * `/permissive-` with C11 it is not a keyword at all, and a declaration using it stops parsing at
 * the `*`. Calling an engine method from C therefore has to spell the convention as `__fastcall`
 * with a dead second parameter, which on x86 is exact rather than approximate:
 *
 *     __thiscall (this, a, b)          this -> ECX, a and b on the stack, callee cleans
 *     __fastcall (this, dead, a, b)    this -> ECX, dead -> EDX, a and b on the stack, callee cleans
 *
 * Same register for `this`, same stack slots for the real arguments, same cleanup. EDX is
 * caller-saved and `__thiscall` never reads it, so the value put there is discarded, pass
 * OF_THISCALL_EDX to say that at the call site rather than leaving a bare NULL to be wondered at.
 *
 * There is no macro for the declaration itself: spelling `__fastcall` out where it is used is
 * clearer than hiding a calling convention behind a name.
 */
#define OF_THISCALL_EDX ((void *)0)

#endif /* COMMON_COMPILER_H */
