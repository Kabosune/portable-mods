# Portable Mods Testing Sandbox

Standalone C/C++20 + CMake testing gym for all four `portable-mods` modules (`quake-movement`, `weighty-aim`, `free-aim`, and `fpv-drone` / `libfpv`), built with **SDL3**, **OpenGL 3.3 Core**, **Dear ImGui**, and **Jolt Physics**.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design, coordinate-space contracts, and dependency graph.

---

## Highlights

- **Dedicated `Sandbox` Options Tab:** Full-auto / semi-auto weapon fire mode, settable **Fire Rate (60–1500 RPM)**, procedural SDL3 gunshot & hit-confirm audio volume/mute, **Reticle Settings** (high-contrast neon-green outlined default, Crosshair+Dot/Crosshair/Dot/Circle+Dot styles, arm/gap/thickness/dot/outline controls, color picker + high-contrast presets & light/dark preview), **Fullscreen** toggle (`F11` / `Alt+Enter`), World & Viewmodel FOV, **Controller Bypasses Menu** (default ON), Quick Teleports, Reset Player/Drone, Aiming Range stats, and local INI settings persistence (`sandbox_settings.ini`, gitignored).
- **Live Stick & Deadzone Visualizer:** Real-time 2D circular stick plot alongside the `weighty-aim` Bezier response curve showing unit circle (`1.0`), `0.98` outer saturation ring, `0.03` inner deadzone circle, raw right-stick vector, and post-`weighty-aim` scaled output — stays live while the Tuning Panel is open via controller bypass!
- **FPV Launch Pads & Position Reset:** Instant `R` drone reset (and buttons in both the `Sandbox` and `FPV Drone` tabs) restoring upright launch-pad pose, zero linear/angular velocity, and disarmed idle throttle, plus quick launch-pad teleports and "Set Drone Spawn Here".

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
.\sandbox\build\portable_mods_sandbox.exe --smoke-frames 10 --ui-open --ui-tab sandbox --debug-ellipse --teleport aim --screenshot sandbox/build/verify_fps_gym.png
.\sandbox\build\portable_mods_sandbox.exe --smoke-frames 10 --ui-open --ui-tab weighty --demo-stick 0.68,0.34 --teleport aim --screenshot sandbox/build/verify_weighty_aim.png
.\sandbox\build\portable_mods_sandbox.exe --smoke-frames 15 --mode fpv --screenshot sandbox/build/verify_fpv_mode.png
```

---

## Controls

| Key / Input | FPS Mode | FPV Drone Mode |
| :--- | :--- | :--- |
| `W / S` | Move Forward / Back | Auto-Hover Throttle Up / Cut (`libfpv` keyboard flight) |
| `A / D` | Strafe Left / Right | Yaw Left / Right |
| `Arrow Keys` | — | Pitch Up/Down, Roll Left/Right |
| `Enter` / `Gamepad South` | — | Toggle Arm / Disarm (`libfpv` / Mode-2 gamepad) |
| `T` / `Gamepad North` | — | Toggle Turtle Mode (crash flip) |
| `B` / `Gamepad East` | — | Beeper (hold) |
| `Mouse` | Look (`free-aim` + camera) | Pointer (when overlay/UI unlocked) |
| `Space` / `Gamepad South` | Jump / Auto-Bhop / Wall-Bounce | — |
| `Ctrl` / `C` / `Gamepad East` | Crouch / Crouch-Slide | — |
| `Left Shift` / `LB` | Dash (Deadlock / Custom) | — |
| `LMB` / `R2` | Fire hitscan (Continuous Full-Auto default, or Semi-Auto) | — |
| `RMB` / `L2` | Aim Down Sights (ADS) | — |
| `F` | Toggle FPS ↔ FPV Drone mode | Toggle FPS ↔ FPV Drone mode |
| `V` | — | Cycle FPV / Chase / LOS camera |
| `R` | Reset player to active teleport | Repair & reset drone upright to launch pad |
| `Tab` / `Esc` / `Gamepad Start` | Unlock mouse & toggle Tuning Panel (gamepad bypasses menu by default) | Unlock mouse & toggle Tuning Panel (gamepad bypasses menu by default) |
| `F11` / `Alt+Enter` | Toggle borderless desktop fullscreen | Toggle borderless desktop fullscreen |
| `F8` | Toggle `libfpv` native overlay (OSD, camera, rates) | Toggle `libfpv` native overlay (OSD, camera, rates) |
| `F1` | Toggle HUD visibility | Toggle HUD visibility |

---

## CLI Options

- `--width <px>` / `--height <px>` — Initial window size (default `1280x720`)
- `--fullscreen` — Launch in borderless desktop fullscreen
- `--teleport hub|movement|aim|fpv` — Initial player spawn wing
- `--mode fps|fpv` — Start in FPS or FPV Drone mode
- `--fpv-view fpv|chase|los` — Initial FPV camera view
- `--ui-open` — Start with the Dear ImGui Tuning Panel unlocked
- `--ui-tab sandbox|move|weighty|freeaim|fpv` — Select a specific Tuning Panel tab on startup
- `--debug-ellipse` — Enable the `free-aim` debug ellipse overlay at startup
- `--demo-stick <x,y>` — Inject a static right-stick test vector for visual curve/visualizer captures
- `--smoke-frames <N>` — Render `N` deterministic frames, check for OpenGL errors, and exit
- `--screenshot <path.png>` — Save the final frame as PNG when `--smoke-frames` completes
- `--fpv-data-dir <dir>` — Custom writable data directory for `libfpv`

---

## Local Settings Persistence

Click **Save Settings** in the `Sandbox` tab to write your local preferences (fullscreen, RPM, fire mode, gun audio volume/mute, hit-confirm sound, FOV, controller menu bypass, `weighty-aim` deadzones/curve, and active presets) to `sandbox_settings.ini` beside the executable. The file is listed in `.gitignore` and auto-loads on interactive launch.

---

## Credits

- **Author & Maintainer:** KaboWabo/Kabosune (me :3)
- **AI Assistance:** Gemini 4 Neon
- **Libraries:** SDL3, Dear ImGui, Jolt Physics, and `libfpv` (Betaflight SimITL)
