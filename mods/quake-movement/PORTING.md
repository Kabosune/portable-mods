# Porting Guide: Quake Movement (`qm_physics`)

You are porting a standalone, toggleable arena/movement-shooter locomotion mod into **[GAME]** (**[ENGINE]** — e.g. C/C++ decomp, Rust recomp, Unity/BepInEx, Unreal/UE4SS, or native DLL mod loader).

---

## 1. Two-Layer Architecture

Keep the mod strictly split into two layers:

1. **Engine-Agnostic Physics Solver (`core/qm_physics.h` / `core/qm_physics.c`)**
   - Pure C11, zero OS or engine dependencies.
   - If **[ENGINE]**'s mod loader supports C/C++ (or FFI to a compiled static/dynamic C library), compile `qm_physics.c` directly instead of rewriting it. Otherwise translate `qm_step()` 1:1 into the target language and mirror the unit tests in `core/test_qm_physics.c`.
   - Deterministic single-tick step:
     ```c
     qm_step(const qm_params_t* params, const qm_input_t* input,
             const qm_state_t* in_state, qm_state_t* out_state);
     ```

2. **[GAME] Engine Adapter (new code you write)**
   - Hooks **[GAME]**'s per-frame or per-physics-tick update.
   - Translates **[GAME]**'s coordinate system, world units, camera orientation, analog/digital inputs, and character state flags into `qm_input_t` and `qm_state_t`.
   - Writes `out_state.vel` back to the player character only when all safety guards pass.
   - Provides a toggle hotkey/chord, preset switcher, config persistence, and optional tuning UI/overlay.

---

## 2. Coordinate System & Unit-Conversion Contract (Critical)

`qm_physics` operates in **right-handed Z-up meters**:
- `+X` = Right (at `cam_yaw_rad = 0`)
- `+Y` = Forward (at `cam_yaw_rad = 0`)
- `+Z` = Up
- `cam_yaw_rad = 0` faces `+Y`; positive yaw rotates counter-clockwise around `+Z`:
  - `forward = (-sin(yaw), cos(yaw), 0)`
  - `right   = ( cos(yaw), sin(yaw), 0)`
  - Given a world horizontal forward vector `(fx, fy)` in Z-up space, `cam_yaw_rad = atan2f(-fx, fy)`.

**Before calling `qm_step()`:**
1. Convert **[GAME]**'s player velocity, position, and floor `slope_normal` into **Z-up meters** using an explicit axis remap and a single `UNIT_SCALE` constant (for example, if **[ENGINE]** is Y-up, map vertical `Y -> Z` while preserving handedness, and divide positions/velocities by `UNITS_PER_METER`).
2. Convert `out_state.vel` back into **[GAME]**'s native axes and units before writing it to the player.
3. Never modify `qm_physics.c` to match a single game's axes or units — keeping the core in Z-up meters ensures all 6 presets remain valid across every port.

---

## 3. Presets & Solver Execution Order

The solver ships with 6 live-switchable presets:

| Preset | Key (`qm_preset_key`) | Key Mechanics |
| :--- | :--- | :--- |
| **Quake VQ3** | `quake_vq3` | Classic Q3A `PM_Friction` + `PM_Accelerate` + uncapped `PM_AirAccelerate` strafe-jumping, auto-bhop (`max_speed=9.5`, `ground_accel=10`, `air_accel=1`, `air_wish_cap=9.5`, `friction=6`, `gravity=20`, `jump_speed=7.0`). |
| **Quake CPMA** | `quake_cpma` | VQ3 strafe-jumping + snappy ground (`ground_accel=15`, `friction=8`) + `CPM_AirControl` (`air_control=6.0`) turning on pure W/S or A/D + pure A/D `cpm_strafe_accel=70` (`cap=1.05`) + `air_stop_accel=2.5` braking. |
| **Half-Life** | `half_life` | GoldSrc/Source low air-wishspeed cap (`0.95 m/s`), high `air_accel=120`, low `friction=4.0`, auto-bhop, and crouch-jump boost (`+0.55 m/s`). |
| **Apex Legends** | `apex_legends` | Sprint-to-crouch flat slide boost (`+3.8 m/s`, cooldown `1.2s`), low-friction downhill slope surfing via `slope_normal`, slide-jump boost (`+1.8 m/s`), post-jump tap-strafe air-lurch window (`0.45s`), and wall-bounce hook. |
| **Deadlock** | `deadlock` | Directional ground/air dash (`13.5 m/s`, 3 stamina pips, `5s` recharge, reduced air gravity during dash), timed dash-jump momentum burst (`+4.2 m/s` horizontal, `+1.0 m/s` vertical in `0.06–0.36s` window), crouch-slide, and wall-bounce. |
| **Custom** | `custom` | Hybrid sandbox exposing all mechanics for live tuning via sliders or config file. |

### Solver Execution Order per Tick (`qm_step`)
1. Guard against non-finite inputs/state; return early without mutating velocity if `dt <= 0` or `dt` is NaN; clamp `dt` to `(0, 0.1]` s; normalize `slope_normal` (default `(0, 0, 1)`).
2. Build horizontal `wishdir` and `wishspeed = max_speed * min(1, |wish|)` from `cam_yaw_rad`, `wish_forward` (`-1..+1`), and `wish_right` (`-1..+1`).
   - Feed analog sticks directly into `wish_forward` / `wish_right` when playing on controller so strafe-jumping and air-control feel natural.
3. Tick cooldowns (`slide_cooldown`, `wall_bounce_timer`, `tap_strafe_timer`, dash timers, stamina recharge), coyote time, and jump buffer.
4. Handle directional dash if `dash_enabled && dash_pressed && dash_timer <= 0 && stamina >= cost`.
5. Handle ground/coyote jump (with jump buffer or `auto_bhop`), applying crouch-jump, slide-jump, or timed dash-jump bonuses. **Set `on_ground = 0` immediately on jump** so ground friction is skipped on the takeoff frame (essential for bunnyhopping).
6. If airborne and `jump_pressed`, run the optional `wall_probe` callback for wall-bounce.
7. **Ground branch:** zero downward `vel[2]`, handle crouch-slide entry/maintenance (low friction + steering + downhill slope tangent acceleration from `slope_normal`), or normal `PM_Friction` + `PM_Accelerate`.
8. **Air branch:** apply tap-strafe air-lurch redirect (if within post-jump window), then `PM_AirAccelerate` (with CPMA pure-strafe / air-stop overrides), then `CPM_AirControl`, then gravity (25% gravity during active dash).
9. Clamp horizontal and fall speeds (`60 m/s` safety cap), integrate predictive position, and update telemetry (`horiz_speed`, `peak_speed`, `mode`, event flags).

---

## 4. Minimum 6 Engine Bindings to Discover Before Writing Code

Inspect **[GAME]** / **[ENGINE]** first and report these 6 bindings before writing gameplay hooks:

1. **Tick Hook:** A per-frame or per-physics-step hook that runs *after* native locomotion computes velocity (or cleanly replaces the locomotion velocity step) and *before* collision/position integration.
2. **Player Velocity (`vec3` R/W):** The player character's current world-space linear velocity.
3. **Player Position (`vec3` R):** World-space position (used for wall-bounce probes and telemetry).
4. **Camera Forward / Yaw (R):** Active gameplay camera's forward vector or yaw angle (camera-relative movement).
5. **Ground & Surface State (R):**
   - `on_ground` (floor contact boolean or grounded locomotion state)
   - `slope_normal` (optional floor normal `vec3` for Apex/Deadlock downhill sliding; fall back to `(0, 0, 1)` if unavailable)
   - Optional raycast/sweep wrapper for `qm_input_t.wall_probe` (may start as `NULL`).
6. **Player Inputs (R, passive observation):**
   - Move stick X/Y or WASD -> `wish_right`, `wish_forward`
   - Jump held + rising-edge press -> `jump_held`, `jump_pressed`
   - Crouch / slide button -> `crouch_held`, `crouch_pressed`
   - Dash button -> `dash_pressed` (avoid colliding with native abilities unless intentionally replacing them)
   - Observe locomotion inputs passively without breaking animations, aiming, or weapon firing; only consume mod-control hotkeys.

---

## 5. Safety-Guard Checklist (Do Not Skip)

Never overwrite player velocity outside normal on-foot locomotion. Immediately skip velocity writes and reset transient solver timers (`sliding`, `dash_timer`, `coyote_timer`, `jump_buffer_timer`) whenever any of the following are active:

- **Death / Ragdoll / Respawn:** Player is dead, dying, ragdolled, or respawning.
- **Cutscenes & UI:** Cutscene, scripted camera, pause menu, inventory/weapon wheel, or level-loading transition.
- **Rails, Climbing & Tethers:** Grind rails, ladders, ledge grabs, mantling, ziplines, grapples/swingshots, or scripted wall-runs.
- **Vehicles & Mounts:** Driving, piloting, riding turrets, hoverboards, mounts, or passenger seats.
- **Swimming & Zero-G:** Underwater, wading/swimming states, or zero-gravity volumes.
- **Native Air / Traversal Abilities:** Double-jump, glide/jetpack, hoverboots, blink/dash, or melee lunge animations. Decide explicitly whether to yield to them or cleanly disable conflicting native air-drag/speed-caps when the mod is toggled on.
- **Startup Default:** Keep the mod **disabled at startup** (`start_enabled = false`) unless the user requests otherwise, with a single hotkey/chord to toggle it on and off cleanly.

---

## 6. Adapter Pitfalls & Porting Lessons

Lessons from multi-engine ports (including `rac1-mods` / `rac-rs`):

- **Hook placement:** Run the adapter *after* the game's input & locomotion state machine and *before* collision/position integration. Hooking earlier lets native air, crouch, or skid states overwrite your velocity every tick.
- **Native button conflicts (auto-bhop & crouch-slide):** If the native engine reacts to held Jump/Crouch buttons, pulse native Jump released while airborne/descending so holding Jump produces a fresh rising edge on landing for auto-bhop, and suppress native Crouch while sliding (or above `slide_min_speed`) so slides don't freeze in a stationary crouch state.
- **Edge tracking across safety guards:** Keep updating `prev_*_held` button state even while safety guards skip `qm_step()`. Otherwise holding Jump or Crouch through a melee attack, cutscene, or swim fires a phantom `*_pressed` edge on the first frame after exiting the guard.
- **Wall-hit / velocity resync debounce:** Require 2+ consecutive blocked ticks before resyncing solver velocity back to engine velocity so single-tick native state transitions or step-ups don't kill slides or bunnyhops.
- **Optional speed scaling:** When porting to games whose level scale differs significantly from Quake, expose a global `speed_scale` multiplier (default `1.0`) in the adapter's tuning UI/config rather than mutating preset defaults in `qm_physics.c`.

---

## 7. Phased Implementation & Verification Plan

1. **Discover & document hooks first:** Confirm **[GAME]**'s coordinate system (Y-up vs Z-up, handedness), unit scale (`UNITS_PER_METER`), and the 6 bindings above before writing code.
2. **Build & test `qm_physics` standalone:** Compile `core/qm_physics.c` and run `core/test_qm_physics.c` (or translated tests) before touching game memory.
3. **Wire read-only telemetry first:** Hook the tick, read velocity, ground state, and camera yaw, and log/display `horiz_speed`, `on_ground`, and `cam_yaw` in-game to confirm axes and unit scale match.
4. **Enable guarded velocity writes:** Start with `Quake VQ3` on flat ground. Verify smooth jumping and strafing without fighting native ground-snap or double-applying gravity (if **[ENGINE]** applies gravity *after* your hook, either hook *after* native gravity or avoid double-integrating `vel[2]`).
5. **Add controls, persistence & live tuning:** Bind toggle, preset-cycle, and dash inputs; save settings to config; optionally expose `Custom` parameters and wall-bounce raycast probes.

---

## 8. Existing Reference Ports

- **`fireball-mods` (`mods/quake_movement/native/`):** Original C11 implementation in a right-handed Z-up meters engine with native HUD overlay, config persistence, and wall-bounce support.
- **`rac1-mods` (`mods/quake-movement/`):** Second-engine port demonstrating coordinate/unit adaptation, controller analog stick wishdir feeding, and platformer-specific safety guards (grind rails, swingshots, hoverboots, swimming, wrench attacks).
