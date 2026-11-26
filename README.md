# portable-mods

Engine-agnostic gameplay mod cores and porting blueprints designed to be dropped into or ported across any game or engine (C/C++ decomps, Rust recomps, Unity/BepInEx, Unreal/UE4SS, native DLL mod loaders, etc.).

## Modules

| Module | Core | Summary | Porting Blueprint |
| :--- | :--- | :--- | :--- |
| **`quake-movement`** | Pure C11 (`qm_physics.h` / `.c`) | 6-preset arena/movement-shooter physics solver (Quake VQ3, CPMA, Half-Life, Apex Legends, Deadlock, Custom) in Z-up meters. | [mods/quake-movement/PORTING.md](mods/quake-movement/PORTING.md) |
| **`weighty-aim`** | Pure C11 (`weighty_aim.h` / `.c`) | Per-player stick curves (Classic, Linear, Balanced, Custom 1–3 cubic Bezier), radial deadzones, Look Acceleration turn boost, live curve graph data API, Force Original Aim, and GyroWiki gyro pipeline. | [mods/weighty-aim/PORTING.md](mods/weighty-aim/PORTING.md) |
| **`fpv-drone`** | `libfpv` git submodule (C ABI) | Drop-in Betaflight SimITL 1 kHz FPV quadcopter with hidden SDL3 radio input, Betaflight OSD, ImGui settings overlay, procedural motor audio, and world collision/wind hooks. | [mods/fpv-drone/PORTING.md](mods/fpv-drone/PORTING.md) |

See [docs/PORTING_CONVENTIONS.md](docs/PORTING_CONVENTIONS.md) for cross-mod architecture, discovery-first rules, and commit hygiene.

---

## Cloning

Clone with `--recurse-submodules` to fetch `mods/fpv-drone/vendor/libfpv`:

```sh
git clone --recurse-submodules <repo-url>
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

---

## Porting a Module with an AI Agent

Each module's `PORTING.md` is written as a self-contained porting blueprint:

1. Point your agent at [`docs/PORTING_CONVENTIONS.md`](docs/PORTING_CONVENTIONS.md) and the module's `PORTING.md` (for example, [`mods/weighty-aim/PORTING.md`](mods/weighty-aim/PORTING.md)).
2. Replace `[GAME]` and `[ENGINE]` with your target title and mod setup.
3. Let the agent complete the **Discovery First** step and report hooks, coordinate system, unit scale, and input/sensor paths before writing game adapter code.
4. Link or vendor the module's `core/` C11 files directly whenever your mod loader supports C/C++ or FFI; otherwise translate 1:1 and mirror the unit tests.

---

## Running Core Unit Tests

The pure-C11 core libraries (`quake-movement` and `weighty-aim`) have zero OS or third-party dependencies.

On Windows (PowerShell), auto-detects `clang`, `gcc` (including MSYS2 MinGW64/UCRT64), `zig cc`, or MSVC `cl.exe`:

```powershell
.\scripts\run-tests.ps1
```

Or compile manually with any C11 compiler:

```sh
gcc -std=c11 -Wall -Wextra -Werror mods/quake-movement/core/qm_physics.c mods/quake-movement/core/test_qm_physics.c -lm -o test_qm_physics && ./test_qm_physics
gcc -std=c11 -Wall -Wextra -Werror mods/weighty-aim/core/weighty_aim.c mods/weighty-aim/core/test_weighty_aim.c -lm -o test_weighty_aim && ./test_weighty_aim
```

---

## Credits

- **Author & Maintainer:** KaboWabo/Kabosune (me :3)
- **AI Assistance:** Gemini 4 Neon
- **Upstream Projects:**
  - [`libfpv`](https://github.com/hammerill/libfpv) by hammerill & contributors (GPL-3.0-or-later; client/shared dual-licensed `GPL-3.0-or-later OR MIT`)
  - [`perfect_dark_weighty_aim`](https://github.com/Kabosune/perfect_dark_weighty_aim) (MIT)
