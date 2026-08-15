# template_plugin

**Produces:** `template_plugin.dll`, goes in `plugins\`.

It changes nothing about the game. It exists for two reasons:

1. **It is the copy-paste starting point for a real plugin.** Two files, the smallest thing that
   satisfies the loader contract.
2. **It is the loader's own test.** When a real plugin does not appear to work, the first
   question is whether it was loaded at all. If this one's lines are in
   `open_fellowship.log`, the loader, the plugin folder, the entry-point export and the ini
   reader are all working, and the fault is in the plugin.

## What it writes

```
[template_plugin] installed
[template_plugin]   host image   00400000 .. 00606000
[template_plugin]   code section 00401000 + 0011B000
[template_plugin]   Fellowship.rfl not loaded yet - as expected at entry-point time
[template_plugin]   Greeting = hello from plugins\
```

The line about `Fellowship.rfl` is the one worth understanding before writing a plugin of your
own. Plugins install at the host's **entry point**, which is before the CRT has run and long
before the game has loaded the rfl. A plugin that patches `Fellowship.exe` can do it immediately.
A plugin that patches `Fellowship.rfl` must wait for the module to exist.

## Configuration: `[template_plugin]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0` and the plugin returns without doing anything |
| `Greeting` | `hello from plugins\` | Echoed to the log, so you can see the ini being read |
