# Portable Mods Testing Sandbox — Architecture

A standalone C/C++20 + CMake testing harness for all four `portable-mods` modules (`quake-movement`, `weighty-aim`, `free-aim`, and `fpv-drone` / `libfpv`) running inside a single unified greybox **Testing Gym** with **SDL3**, **OpenGL 3.3 Core**, **Dear ImGui**, and **Jolt Physics**.

---

## 1. Design Principles

1. **Zero Duplicated Mod Code (Direct Core Linking):**
   - `mods/quake-movement/core/qm_physics.c`
   - `mods/weighty-aim/core/weighty_aim.c`
   - `mods/free-aim/core/free_aim.c`
   - `mods/fpv-drone/vendor/libfpv` (CMake subdirectory target `libfpv::fpv`)
   All four cores are compiled and linked directly from `../mods/`. No mod logic is copied into `sandbox/`.
2. **Single Source of Truth for Geometry:**
   - `GymBuilder` procedurally generates the entire Testing Gym once in canonical **Z-up meters** as indexed triangles with vertex colors, normals, and surface tags (flat floor, slopes, walls, targets, FPV gates).
   - The exact same triangle stream feeds:
     1. **OpenGL 3.3 Core** static scene VAO/VBO/EBO (with procedural checkerboard/grid shading in the fragment shader),
     2. **Jolt Physics** static `MeshShape` (via an explicit determinant-`+1` axis map at the Jolt boundary), and
     3. **`libfpv`** static collision (`fpv_static_surfaces_load()`).
3. **Single SDL3 Instance (No HID/Device Contention):**
   - `libfpv` is configured with `-DLIBFPV_WITH_SDL=OFF -DLIBFPV_WITH_OVERLAY=ON -DLIBFPV_BUILD_TESTS=OFF -DLIBFPV_BUILD_EXAMPLES=OFF`.
   - The sandbox builds `SDL3::SDL3-static` once from `../mods/fpv-drone/vendor/libfpv/third_party/SDL` (zero duplicate SDL3 linkage) and owns all video, audio, mouse, keyboard, gamepad, and motion sensor (`SDL_SENSOR_GYRO` / `SDL_SENSOR_ACCEL`) devices.
   - In FPV mode, the sandbox feeds keyboard scancodes (`fpv_input_key`), mouse/overlay events (`fpv_overlay_*`), and host-mapped gamepad/UI channels (`fpv_input_set_channels`) into `libfpv`, preventing Windows HIDAPI contention on DualSense/Switch Pro controllers.
4. **Native Windows Toolchain Choice:**
   - `libfpv` embeds Betaflight SITL, which uses GNU linker script section markers (`sitl.ld`) and GCC bitfield layout (`-mno-ms-bitfields`).
   - On Windows, the sandbox builds cleanly in one CMake + Ninja pass using **MSYS2 MinGW-w64 GCC (`C:/msys64/mingw64/bin`)** with static C/C++ runtime flags (`-static-libgcc -static-libstdc++ -static`) and a post-build copy of `libfpv.dll` beside the executables so the built `.exe` runs directly from any shell or Explorer without PATH tweaks.

---

## 2. Directory Layout & CMake Dependency Graph

```text
sandbox/
├── ARCHITECTURE.md            # This document
├── README.md                  # Build, run, controls, and test guide
├── CMakeLists.txt             # Top-level CMake project (C11 + C++20)
├── src/
│   ├── main.cpp               # App entry, CLI flags (--smoke-frames, --screenshot, --ui-tab), fullscreen & loop
│   ├── math_types.h           # Vec2/Vec3/Mat4/Quat helpers + Z-up camera basis + boundary transforms
│   ├── control_helpers.h      # WeaponFireController, MenuInputRouter, AimStats, InputEdgeLatch, FPV RC mapper, sandbox WA defaults
│   ├── weapon_audio.h / .cpp  # Pure C++20 48kHz stereo procedural gunshot + hit-confirm synthesizer (soft-limited)
│   ├── sandbox_settings.h/.cpp# Plain-text INI serializer/deserializer for local sandbox_settings.ini
│   ├── gl_loader.h            # Minimal OpenGL 3.3 Core loader (loaded via SDL_GL_GetProcAddress)
│   ├── gym_map.h / .cpp       # Unified procedural Testing Gym + dynamic aiming targets + teleports
│   ├── physics_world.h / .cpp # Jolt Physics world, static gym MeshShape, capsule collide-and-slide, raycasts
│   ├── input_manager.h / .cpp # SDL3 raw mouse, keyboard, un-deadzoned gamepad sticks, Start button & gyro/accel sensors
│   ├── renderer.h / .cpp      # OpenGL 3.3 Core 3D world, dynamic targets, viewmodel + muzzle flash, tracers, FBO, PNG writer
│   ├── fpv_bridge.h / .cpp    # libfpv lifecycle, launch pad resets, static surfaces, camera/OSD/overlay GL3, SDL3 audio stream
│   ├── sandbox_app.h / .cpp   # Fixed-timestep FPS+FPV simulation state wiring all 4 mods + weapon audio together
│   └── ui_hud.h / .cpp        # Dear ImGui HUD (speedometer, aiming stats, reticle, ellipse) + Sandbox/Mod Tuning Panel
└── tests/
    └── smoke_tests.cpp        # Headless automated verification executable (sandbox_smoke_tests)
```

### Dependency Graph

```text
                ┌──────────────────────────┐
                │  portable_mods_sandbox   │ (C++20)
                └────┬────┬────┬────┬──────┘
                     │    │    │    │
       ┌─────────────┘    │    │    └──────────────────┐
       ▼                  ▼    ▼                       ▼
┌─────────────┐   ┌──────────────┐   ┌──────────┐   ┌────────────────┐
│ sandbox_sim │   │ SDL3-static  │   │  ImGui   │   │  libfpv::fpv   │
│ (static lib)│   │ (libfpv 3p)  │   │(libfpv3p)│   │ (SHARED DLL,   │
└──────┬──────┘   └──────────────┘   └──────────┘   │  WITH_SDL=OFF) │
       │                                            └────────────────┘
       ├──► ../mods/quake-movement/core/qm_physics.c   (C11, direct)
       ├──► ../mods/weighty-aim/core/weighty_aim.c     (C11, direct)
       ├──► ../mods/free-aim/core/free_aim.c           (C11, direct)
       └──► Jolt (FetchContent v5.2.0, static, double-precision OFF)

                ┌──────────────────────────┐
                │   sandbox_smoke_tests    │ (links sandbox_sim + libfpv::fpv)
                └──────────────────────────┘
```

Splitting the headless simulation + gym + Jolt + mod cores into a reusable `sandbox_sim` static library allows `sandbox_smoke_tests` to exercise the **exact** production code paths (gym generation, Jolt static mesh & capsule sweep & raycast, `qm_physics`, `weighty_aim`, `free_aim`, and `libfpv`) with zero graphics context required.

---

## 3. Coordinate System & Boundary Contracts

### Canonical Sandbox Space (Right-Handed Z-Up, Meters)

Matches `qm_physics.h` 1:1:
- **`+X`** = Right (East) at `cam_yaw_rad = 0`
- **`+Y`** = Forward (North) at `cam_yaw_rad = 0`
- **`+Z`** = Up
- **Units:** `1.0` world unit = `1.0` meter.

### Camera & Mod Angle Conventions

- `cam_yaw_rad = 0` looks toward `+Y`. Positive `cam_yaw_rad` rotates **counter-clockwise around `+Z`** (turning left):
  - `forward_xy = (-sin(yaw),  cos(yaw), 0)`
  - `right_xy   = ( cos(yaw),  sin(yaw), 0)`
- `cam_pitch_rad` in `[-89°, +89°]`: positive pitches **up**.
  - `cam_forward = (-sin(yaw) * cos(pitch), cos(yaw) * cos(pitch), sin(pitch))`
  - `cam_right   = ( cos(yaw),             sin(yaw),              0)`
  - `cam_up      = cross(cam_right, cam_forward)`
- `weighty-aim` and `free-aim` use `+yaw_deg` = turn **right**, `+pitch_deg` = pitch **up**:
  - `cam_yaw_rad   -= deg2rad(fa_out.cam_delta_deg[0])`
  - `cam_pitch_rad += deg2rad(fa_out.cam_delta_deg[1])`
- `fa_compute_shot_dir(cam_forward, cam_right, cam_up, fa_out.reticle_deg, shot_dir)` operates directly on the Z-up camera basis and yields a Z-up unit ray.

### Boundary Conversions

| Boundary | Target Space | Exact Map | Handedness / Winding |
| :--- | :--- | :--- | :--- |
| **`qm_physics`** | Z-up RH meters | Identity `(x, y, z)` | Preserved |
| **`libfpv`** | Configurable `fpv_coord_system` | `{ right = POS_X, up = POS_Z, forward = POS_Y, units_per_meter = 1.0f }` | Preserved internally by `libfpv` |
| **Jolt Physics** | Y-up RH meters | `to_jolt(x, y, z) = (x, z, -y)`<br>`from_jolt(x, y, z) = (x, -z, y)` | `det = +1` (strictly right-handed; preserves triangle winding, normals, and lengths) |
| **OpenGL 3.3** | Clip space (view looks down `-Z`) | World vertices stay Z-up; `viewFromBasis(eye, right, up, forward)` maps `(right, up, -forward)` into view `(X, Y, Z)` | Preserved (`GL_CCW` front faces) |

---

## 4. Simulation Loop, Physics & Input Stack

### Fixed-Timestep FPS Simulation
- Selectable physics tick rate in the Tuning Panel: **60 / 120 / 240 Hz** (default **120 Hz**), stepped via an accumulator capped at `0.1s` frame delta (`max 8 substeps/frame`) to prevent spiral-of-death.
- Each tick in FPS mode:
  1. **Aiming pipeline (`weighty-aim` → `free-aim`):**
     - Raw SDL3 mouse deltas (`SDL_EVENT_MOUSE_MOTION` in relative mode) convert to degrees (`mouse_sens`).
     - Un-deadzoned right gamepad stick (`[-1, +1]`, zero SDL deadzone) passes through `wa_process_stick()` → multiplied by `full_stick_deg_per_sec * dt`.
     - Gamepad `SDL_SENSOR_GYRO` (converted from rad/s to deg/s) and `SDL_SENSOR_ACCEL` pass through `wa_process_gyro()` → degrees directly.
     - Combined `{stick_deg, mouse_deg, gyro_deg}` feed `fa_step()` with `fov_scale = fa_fov_scale(current_fovy_deg, base_world_fovy_deg)`.
     - `fa_output.cam_delta_deg` updates `(cam_yaw_rad, cam_pitch_rad)`.
     - `fa_output.reticle_deg` drives: (a) 2D sub-pixel reticle via `fa_project_reticle_ndc()` using **world camera FOV** (never viewmodel FOV), (b) hitscan ray via `fa_compute_shot_dir()`, and (c) procedural 3D weapon viewmodel offset via `fa_compute_viewmodel_offset()` rendered in a separate depth-cleared viewmodel pass at its own `viewmodel_fovy_deg`.
  2. **Movement pipeline (`qm_physics` + Jolt capsule):**
     - Crouch height smoothly switches capsule half-height (`1.80m` standing / `1.10m` crouching) with an overhead clearance check in Jolt before uncrouching.
     - Ground probe (short downward Jolt capsule cast) supplies `on_ground` and exact `slope_normal` (in Z-up meters).
     - Wall-bounce callback (`qm_input_t.wall_probe`) wraps a Jolt sphere/ray sweep from torso height to return outward wall normals in Z-up meters.
     - `qm_step()` computes updated velocity (with optional `speed_scale` applied cleanly at the adapter boundary without mutating presets).
     - Jolt `ExtendedUpdate` / multi-iteration capsule collide-and-slide (`sweep + clip velocity against hit normals + stair step-up`) integrates the player's position cleanly against the static gym `MeshShape`, preventing tunneling on ramps, corners, and low crouch tunnels.
  3. **Hitscan, Continuous Auto-Fire & Procedural Audio:**
     - `WeaponFireController` supports **Continuous / Full-Auto** (default, configurable `60–1500 RPM`, default `600 RPM`) and **Semi-Auto**, preserving cooldown across release/re-press with zero dropped first shot.
     - Each shot fires along `fa_compute_shot_dir()` from eye position against Jolt static world geometry and the Aiming Range's static + oscillating/strafing targets, triggers damped spring viewmodel recoil + emissive muzzle flash starburst + fading 3D laser tracer, records `AimStats` (shots, hits, accuracy %, streaks), and synthesizes a soft-limited 48 kHz stereo gunshot (plus a crisp hit-confirm tick on target hits) via `WeaponAudioSynth` over an independent SDL3 `SDL_AudioStream` that coexists safely with `libfpv`'s motor audio stream.
  4. **Controller Menu Bypass & `Start` Toggle:**
     - `MenuInputRouter` separates KB+Mouse gating (`allow_kbm`) from Gamepad gating (`allow_gamepad`). When `ui_open == true` and `controller_bypasses_menu == true` (default ON), mouse and keyboard drive Dear ImGui while gamepad sticks, triggers, buttons, and gyro/accel keep driving live FPS/FPV gameplay and the live stick/deadzone visualizer.
     - Rising-edge `SDL_GAMEPAD_BUTTON_START` toggles `ui_open` cleanly in any mode and updates relative mouse capture immediately.

### FPV Drone Mode (`libfpv`)
- Toggle seamlessly via **`F`** or UI button (`FPS` ↔ `FPV`).
- While in FPV mode:
  - FPS player locomotion is safely paused (`qm_physics` timers preserved/frozen, `fpv_drone_set_pilot_position()` tracks the player avatar's feet).
  - `fpv_drone_tick()` steps Betaflight SimITL presentation and supplies `frame.camera`, `frame.chase_camera`, `frame.los_camera`, `frame.motors[4]`, `frame.osd`, and `frame.video_feed`.
  - The 3D scene renders into an offscreen RGBA8/D24 FBO; in `FPV_VIEW_FPV`, `fpv_video_gl3_draw()` applies lens distortion / video feed / OSD (falling back cleanly to `fpv_gl3_render()` OSD if needed), and `fpv_gl3_render()` renders `libfpv`'s own F8 overlay when opened.
  - Procedural motor & beeper audio pulls stereo 48 kHz float PCM from `fpv_audio_read()` inside an `SDL_AudioStream` callback (gracefully no-oping if no audio playback device is available).
  - Keyboard (`WASD`/arrows/Enter/Space/etc. forwarded via `fpv_input_key`), gamepad (mapped to AETR + AUX1 arm / AUX2 angle / AUX3 beeper / AUX4 turtle via `fpv_input_set_channels`), and Tuning Panel buttons (Arm/Disarm, Camera View, Reset/Repair, Camera Tilt/Rates via `fpv_cli_exec`, Volume/Mute, OSD toggle) all work out of the box.

---

## 5. Unified Greybox Testing Gym Layout (Z-Up Meters)

Centered around a hub plaza at `(0, 0, 0)` with clear sightlines and color-coded wings:

```text
                      +Y (North)
                 ┌──────────────────┐
                 │   AIMING RANGE   │  Y: +12 .. +76m, X: -18 .. +18m
                 │ Near / Mid / Far │  Static silhouettes + 3 strafing
                 │  + Moving Targets│  targets + range distance markers
                 └────────┬─────────┘
                          │
┌────────────────────┐    │    ┌──────────────────────────────┐
│   MOVEMENT WING    │    │    │          FPV YARD            │
│  X: -62 .. -12m    ├────┼────┤     X: +18 .. +98m           │
│  Y: -28 .. +42m    │HUB │    │     Y: -40 .. +45m           │
│ • Strafe runway    │0,0 │    │ • Open flight yard           │
│ • Bhop platforms   │    │    │ • 5 colorful slalom gates    │
│ • 20/25/30/35°     │    │    │ • Twin fly-through arches    │
│   surf ramps       │    │    │ • 18m dive tower w/ openings │
│ • Wall-bounce lane │    │    │ • Launch pad + low ceiling   │
│ • Crouch tunnel    │    │    └──────────────────────────────┘
│ • Dash-jump gaps   │
└────────────────────┘
```

### Teleport Presets
1. **Hub Spawn:** `(0, -6, 0.05)`, facing `+Y` (Aiming Range ahead, Movement Wing left, FPV Yard right)
2. **Movement Wing:** `(-18, -20, 0.05)`, facing `+Y` down the strafe runway & ramps
3. **Aiming Range:** `(0, 14, 0.05)`, facing `+Y` down the target lane
4. **FPV Yard:** `(32, -8, 0.05)`, facing `+X/+Y` toward the slalom gates and dive tower

---

## 6. Controls, HUD & Dear ImGui Layout

### Controls
| Input | FPS Mode | FPV Mode |
| :--- | :--- | :--- |
| `W / A / S / D` | Move (camera-relative) | Throttle/Yaw (forwarded to `libfpv`) |
| `Mouse` | Look (`free-aim` + camera) | Overlay pointer when unlocked |
| `Space` | Jump / Auto-bhop / Wall-bounce | Forwarded to `libfpv` |
| `Ctrl` / `C` | Crouch / Crouch-slide | Forwarded to `libfpv` |
| `Left Shift` | Dash (Deadlock/Custom) | Forwarded to `libfpv` |
| `LMB` / `R2` | Fire hitscan (Full-Auto default / Semi-Auto) | — |
| `RMB` / `L2` | Hold ADS (or toggle in UI) | — |
| `F` | Toggle FPS ↔ FPV Drone mode | Toggle FPS ↔ FPV Drone mode |
| `V` | — | Cycle FPV / Chase / LOS camera |
| `R` | Reset player to active spawn | Repair & reset drone upright to launch pad |
| `F11` / `Alt+Enter` | Toggle borderless desktop fullscreen | Toggle borderless desktop fullscreen |
| `F8` | Toggle `libfpv` native overlay | Toggle `libfpv` native overlay |
| `Tab` / `Esc` / `Start` | Unlock mouse & toggle Tuning Panel | Unlock mouse & toggle Tuning Panel |
| `F1` | Toggle HUD visibility | Toggle HUD visibility |

### HUD & Tuning Panel
- **Always-Visible HUD (toggleable with `F1`):**
  - Bottom-center **Quake Speedometer**: horizontal speed in `m/s` and Quake `UPS` (`m/s * 33.68`), peak speed, mode badge (`GROUND` / `AIR` / `SLIDE` / `DASH`), slope angle readout, stamina pips, and color-coded horizontal speed bar.
  - **`free-aim` Sub-Pixel Reticle:** drawn at exact screen pixel coordinates from `fa_project_reticle_ndc()`, honoring `show_reticle`, `size`, `opacity`, and `smooth_reticle`, plus hit-marker flash on target impact.
  - **Optional `free-aim` Debug Ellipse:** renders `zone_x/zone_y` ellipse, `target`, `display`, `over`, `lead_rate`, `catchup_rate`, `catchup_vel`, `last_input`, and `edge_turn_active` from `fa_get_debug_info()`.
  - Top-left compact **Mode & Control Hint Bar** (`FPS` / `FPV`, active presets, fire mode/RPM, controller bypass badge) and top-right **Aiming Range Stats** badge (shots, hits, accuracy %, current/best hit streak).
- **Tuning Panel (collapsible tabbed ImGui window when unlocked):**
  - **`Sandbox`:** Dedicated Sandbox Options tab grouping **Weapon** (Full-Auto/Semi-Auto, Fire Rate RPM, Gun Audio Volume/Mute, Hit-Confirm Sound), **Display** (Fullscreen, World/Viewmodel FOV, Mouse Sens, HUD/ADS toggles), **Controls** (Controller Bypasses Menu default ON, gamepad/sensor status, `Start` toggle hint), and **Gym & Reset** (Quick Teleports, Reset Player/Drone, Aiming Stats + Reset, and Save/Load/Reset `sandbox_settings.ini`).
  - **`Quake Move`:** All 6 presets, tick-rate selector (`60/120/240 Hz`), `speed_scale`, and live sliders for core ground/air, slide, tap-strafe, dash, and wall-bounce parameters.
  - **`Weighty Aim`:** Curve selector (`Classic`..`Custom 3`), sandbox right-stick deadzone defaults (`0.03` inner / `0.98` outer saturation), interactive **live Bezier response curve plot** (`wa_build_curve_graph()`) side-by-side with a **live 2D circular stick & deadzone visualizer** (unit circle, `0.98` outer ring, `0.03` inner deadzone, raw stick & post-`weighty-aim` output vectors), deadzone/sensitivity/turn-boost sliders, Force Original Aim toggle, gyro mode/sensitivity/space/calibration controls, live gyro & accel sensor readout.
  - **`Free-Aim`:** Preset picker (`Classic`..`Custom 3`), Force Original Aim toggle, zone/share/lead/catch-up/spring/sway/ADS/edge-auto-turn sliders, reticle profile controls, debug ellipse toggle, scripted look pattern toggle, and CSV telemetry logger (`fa_format_telemetry_csv_header/row`) with file export.
  - **`FPV Drone`:** FPS/FPV mode toggle, prominent **Reset Drone Position (`R`)**, quick **Launch Pads** (FPV Yard, Hub Plaza, Movement Wing, Aiming Range, plus "Set Drone Spawn Here"), camera view (`FPV`/`Chase`/`LOS`), Arm/Disarm switch, camera tilt / rate adjustment, motor/beep volume & mute, OSD toggle, `libfpv` F8 overlay trigger, and live telemetry (RPM, battery voltage, altitude, speed km/h, armed/arming flags).

---

## 7. Verification Strategy

1. **Automated Headless Verification (`sandbox_smoke_tests`):**
   - Gym mesh generation non-empty, finite vertices, valid indices, wing bounds present.
   - Jolt Physics static `MeshShape` creation, downward raycasts onto flat floor (`n ≈ (0,0,1)`), 30° surf ramp (`n` tilted ~30°), wall-bounce wall (`|n.x| ≈ 1`), and hitscan raycasts hitting static & moving targets.
   - Jolt player capsule sweep & ground/slope/wall probe verification.
   - `qm_physics` stepping across all 6 presets (strafe acceleration, Apex downhill 30° slope surf speedup, Deadlock dash + dash-jump, wall-bounce via real Jolt wall probe).
   - `weighty_aim` → `free_aim` composition, `wa_build_curve_graph()`, `fa_get_debug_info()`, `fa_project_reticle_ndc()`, `fa_compute_shot_dir()`, CSV telemetry formatting.
   - `libfpv` manual-timing init (`FPV_TIMING_MANUAL`), `fpv_static_surfaces_load()` with the exact Testing Gym triangles, `fpv_drone_create()`, `fpv_step()` + `fpv_drone_tick()`, and `fpv_audio_read()`.
2. **Real OpenGL + SDL3 + ImGui Visual Smoke Verification:**
   - CLI options `--smoke-frames <N> --screenshot <path.png> [--mode fps|fpv] [--ui-open] [--teleport hub|movement|aim|fpv]` launch the real SDL3 + OpenGL 3.3 Core + Dear ImGui binary, compile all shaders, render real frames, verify zero GL errors/crashes, and write PNG screenshots for visual inspection.
