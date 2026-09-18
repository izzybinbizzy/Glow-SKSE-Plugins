# SKSE plugins by izzydoingit

Source code for the SKSE plugins that ship with my Skyrim Special Edition mods.

| folder | plugin | ships with |
|---|---|---|
| `Let There Be Glow` | `LetThereBeGlow.dll` | Let There Be Glow (required) |
| `Aetherial Radiance Sneak` | `AetherialRadianceSneak.dll` | Aetherial Radiance - Spells (optional sneaking page) |
| `Luminous Arcana` | `LuminousArcana.dll` | Luminous Arcana (required) |
| `Dynamic Wards` | `DynamicWards.dll` | Dynamic Wards 2.0 (required) |

## License

GNU General Public License, version 3 or (at your option) any later version. See `LICENSE`.

All four plugins are built against [CommonLibSSE NG](https://github.com/alandtse/CommonLibVR) (release 8.0.1,
commit `d13d10a0ccb4945870eb841bf1ad8a6cf5ed84dd`), which is GPL-3.0-or-later.

## Building

Install git, [xmake](https://xmake.io) 3 and the Visual Studio C++ build tools, then run `build.bat` in a
plugin's folder. It clones CommonLib at the pinned commit into `lib\commonlibsse-ng` and builds the DLL.
All four target Skyrim SE and AE; VR is not supported.
