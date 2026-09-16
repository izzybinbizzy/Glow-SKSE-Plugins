# Let There Be Glow - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `../LICENSE`.

The in-game alternative to the Glowified Patcher xEdit script. When the game has loaded its plugins it
reads the Let There Be Glow and CS Light configs under `Data\LightPlacer\` and, in memory:

1. removes the game's own casting light from magic effects whose casting art those configs light;
2. does the same for projectiles, explosions and hazards whose model is lit, keeping cone and flame
   projectiles lit and always removing poison spray lights;
3. gives the Dragonborn poison rune the casting art its lit hand needs;
4. with the Spray Lights option installed, gives each spray projectile a stretched copy of its light,
   colored from the installer's marker files;
5. leaves only the first light on an enchantment that carries two or more lit shaders (the originals are
   restored while the Crafting Menu is open, so crafted items never store an in-memory copy).

It does nothing if `GlowifiedSkyrim.esp` is loaded. Every change is written to `LetThereBeGlow.log`.
