# Dynamic Wards - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `../LICENSE`.

Ships with Dynamic Wards 2.0, which gives every rank of ward its own art with no plugin file of its own.

2.0 exists because 1.0 did this from a Papyrus script, and that script registered for the Loading Menu
through SKSE. That registration is written into the co-save under a handle whose type reads `0xFFFF` - an
id no form has - so a save carrying it loads with a dead entry in the menu list. A DLL registers nothing
with SKSE's Papyrus event system and leaves nothing behind.

This build is step 1 of the rework and changes nothing in the game. It reads which vanilla art records the
wards wear, and tries to make an art object, the empty art and a reference effect in memory through the
game's own form factory, reporting which succeeded.

`src/DevBenchAPI.h` and `src/DevBenchAPI.cpp` are devbench's own interface files, MIT licensed by their
author so that any plugin may carry them (see `src/DevBenchAPI.LICENSE.txt`). They are copied unchanged,
and let this plugin hand its findings to devbench when it is present. Nothing about the mod needs devbench.

| file | what it holds |
|---|---|
| `src/main.cpp` | the SKSE entry point, the log, and small text helpers |
| `src/Plugin.h` | what the files share |
| `src/PCH.h` | the headers every file includes, compiled once |
| `src/CopyProbe.cpp` | the probe: what the wards wear, and what can be made in memory |
| `src/DevBench.cpp` | hands the probe's findings to devbench, if devbench is there |
