# cd_check

**Produces:** `cd_check.dll`. **Off by default.**

Ported from the community patcher's `SkipCDCheck`. The call at `0x406439` is redirected to a stub
that returns 1; the engine tests the result with `test eax,eax / jne` and carries on.

The callee at `0x4BD2C0` is **not** modified. Only this one call site is diverted, so anything
else that calls it keeps the original behaviour - which is the whole reason for redirecting a call
rather than patching a function.

Off by default because a No-CD executable does not need it, and that is the usual case for this
game. A patch that is not needed is still a patch that can be wrong.

## Configuration: `[cd_check]`

| Key | Default | |
|---|---|---|
| `Enabled` | `0` | |
