# Portable Mods Testing Sandbox

Standalone C/C++20 + CMake testing gym for all four `portable-mods` modules (`quake-movement`, `weighty-aim`, `free-aim`, and `fpv-drone` / `libfpv`), built with **SDL3**, **OpenGL 3.3 Core**, **Dear ImGui**, and **Jolt Physics**.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design, coordinate-space contracts, and dependency graph.

---

## Prerequisites

1. **Submodules initialized:**
   ```powershell
   git submodule update --init
   git -C mods/fpv-drone/vendor/libfpv submodule update --init
   ```
2. **Windows Toolchain:** MSYS2 MinGW-w64 (`C:\msys64\mingw64\bin` with `gcc`, `g++`, `cmake`, and `ninja`) is used because `libfpv` embeds Betaflight SITL (GNU linker script + GCC bitfield layout). The final executables link the C/C++ runtimes statically and copy `libfpv.dll` beside the `.exe`, so they launch directly from any terminal or Explorer.

---

## Build & Run (Windows PowerShell)

```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
cmake -S sandbox -B sandbox/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build sandbox/build -j
```

Launch the sandbox:

```powershell
.\sandbox\build\portable_mods_sandbox.exe
```

Run the automated headless verification suite (CTest):

```powershell
ctest --test-dir sandbox/build --output-on-failure
# or directly:
.\sandbox\build\sandbox_smoke_tests.exe
```

Capture headless/automated visual verification screenshots:

```powershell
.\sandbox\build\portable_mods_sandbox.exe --smoke-frames 10 --ui-open --debug-ellipse --teleport aim --screenshot sandbox/build/smoke_fps.png
.\sandbox\build\portable_mods_sandbox.exe --smoke-frames 15 --mode fpv --screenshot sandbox/build/smoke_fpv.png
```

---

## Controls

| Key / Input | FPS Mode | FPV Drone Mode |
| :--- | :--- | :--- |
| `W / A / S / D` | Move (camera-relative) | Throttle / Yaw (forwarded to `libfpv`) |
| `Mouse` | Look (`free-aim` + camera) | Pointer (when overlay/UI unlocked) |
| `Space` / `Gamepad South` | Jump / Auto-Bhop / Wall-Bounce | Forwarded to `libfpv` |
| `Ctrl` / `C` / `Gamepad East` | Crouch / Crouch-Slide | Forwarded to `libfpv` |
| `Left Shift` / `LB` | Dash (Deadlock / Custom) | Forwarded to `libfpv` |
| `LMB` / `R2` | Fire hitscan shot | — |
| `RMB` / `L2` | Aim Down Sights (ADS) | — |
| `F` | Toggle FPS ↔ FPV Drone mode | Toggle FPS ↔ FPV Drone mode |
| `V` | — | Cycle FPV / Chase / LOS camera |
| `R` | Reset player to active teleport | Repair & reset drone to launch pad |
| `Tab` / `Esc` | Unlock mouse & open Tuning Panel | Unlock mouse & open Tuning Panel |
| `F8` | Toggle `libfpv` native overlay | Toggle `libfpv` native overlay |
| `F1` | Toggle HUD visibility | Toggle HUD visibility |

---

## CLI Options

- `--width <px>` / `--height <px>` — Initial window size (default `1280x720`)
- `--teleport hub|movement|aim|fpv` — Initial player spawn wing
- `--mode fps|fpv` — Start in FPS or FPV Drone mode
- `--fpv-view fpv|chase|los` — Initial FPV camera view
- `--ui-open` — Start with the Dear ImGui Tuning Panel unlocked
- `--debug-ellipse` — Enable the `free-aim` debug ellipse overlay at startup
- `--smoke-frames <N>` — Render `N` deterministic frames, check for OpenGL errors, and exit
- `--screenshot <path.png>` — Save the final frame as PNG when `--smoke-frames` completes
- `--fpv-data-dir <dir>` — Custom writable data directory for `libfpv`

---

## Credits

- **Author & Maintainer:** KaboWabo/Kabosune (me :3)
- **AI Assistance:** Gemini 4 Neon
- **Libraries:** SDL3, Dear ImGui, Jolt Physics, and `libfpv` (Betaflight SimITL)
