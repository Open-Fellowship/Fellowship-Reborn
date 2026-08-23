# cd_check

**Produces:** `cd_check.dll`. Always on, and it has no configuration.

Ported from the community patcher's `SkipCDCheck`. The call at `0x406439` is redirected to a stub
that returns 1; the engine tests the result with `test eax,eax / jne` and carries on.

The callee at `0x4BD2C0` is **not** modified. Only this one call site is diverted, so anything
else that calls it keeps the original behaviour, which is the whole reason for redirecting a call
rather than patching a function.

A No-CD executable does not need this, and that is the usual case for this game. It runs anyway,
because the opcode check in front of the write is what makes an unnecessary patch harmless: on a
copy where that call has gone, the plugin declines and says so.

## No configuration

There is no `[cd_check]` section and no `Enabled` key.

It used to have one, defaulting to off, on the reasoning that a No-CD executable does not need the
patch and one more patch is one more thing that can be wrong. Both halves of that were the wrong
worry. `patch_redirect_call` verifies the opcode is `E8` before it writes anything, so on a copy
where that call is no longer there the plugin declines and says so in the log. A switch protected
nothing the validation did not already handle, and a key whose only purpose is to disarm a patch
mostly invites turning off the one that was working.

Delete `cd_check.dll` from the plugins folder if you want it gone. That is how every plugin here
is switched off.
