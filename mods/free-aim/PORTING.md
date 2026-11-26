# Porting Guide: Free-Aim (Unlocked Dynamic Reticle)

You are porting the unlocked free-aim / non-centered dynamic **reticle (crosshair / aim dot)** system (Bodycam / Receiver / Wii-shooter style) into **[GAME]** (**[ENGINE / MOD SETUP]** — e.g. C/C++ decomp, source port, Rust recomp, Unity/BepInEx, or Unreal/UE4SS).

Reference implementation (public, MIT):
- `https://github.com/Kabosune/perfect_dark_weighty_aim` (branch `weighty-aim`) — see `WEIGHTY_AIM_DESIGN.md` sections 3–4 and `port/src/weightyaim.c`.
- Portable pure-C11 core in this repository: [`core/free_aim.h`](core/free_aim.h) and [`core/free_aim.c`](core/free_aim.c) (port the behavior, not N64-specific plumbing).

> **Standalone or Composable:** `free-aim` is intentionally separate from [`mods/weighty-aim/`](../weighty-aim/PORTING.md). Games can adopt `free-aim` standalone using their own raw look inputs, or stack `weighty-aim` (stick curves, deadzones, turn boost, gyro) directly upstream so its output degrees feed `fa_step()`.

---

## 1. Discovery First (Report Before Writing Code)

Before writing any hook or gameplay code, inspect **[GAME]** / **[ENGINE]** and report back all 6 items:

1. **Look-to-camera hook site:** Where **[GAME]** reads mouse, stick, and gyro input and turns it into camera yaw/pitch rotation (and its units: degrees/frame, radians/frame, or speed units).
2. **Reticle draw path:** Where **[GAME]** draws its HUD reticle, whether it assumes screen centre, and whether it snaps to whole pixels (for Smooth Reticle sub-pixel drawing).
3. **Shot ray, spread, and hit markers:** Where hitscan rays or projectile spawn directions are computed, whether recoil/spread is applied in camera space or weapon space, and whether hit markers / damage indicators read screen centre or the reticle position.
4. **Viewmodel position & FOV:** How the first-person weapon model is positioned and rotated, and whether it uses a **separate viewmodel FOV** from the world camera FOV.
   - **Critical:** Always project the reticle with the **world camera FOV** (`fa_project_reticle_ndc`), never the viewmodel FOV, or shots will diverge from the reticle.
5. **Aim assist / magnetism:** Whether **[GAME]** has auto-aim or target magnetism, how it reports a locked target's screen angle, and how to route it into `assist_target_deg` so it pulls the **reticle** instead of snapping the camera.
6. **Perspective & Multiplayer / Anti-Cheat Gate:**
   - **First-person vs. Third-person Gate:** If **[GAME]** is third-person (or has a third-person mode), **stop and ask** how shots should resolve (camera ray through the reticle vs. muzzle ray toward the reticle impact point) before implementing.
   - **Online-Play Gate:** Only target offline / local / single-player / LAN modes unless the project owner explicitly authorizes online play and confirms no anti-cheat conflict.

### FOV Scaling Note
Degree-based free-aim zones (`zone_x`, `zone_y`) change apparent screen size when the player changes FOV or zooms in while aiming. Pass `fa_fov_scale(current_fovy_deg, ref_fovy_deg)` (`tan(fov/2) / tan(ref_fov/2)`) in `fa_input_t.fov_scale` so the elliptical zone covers the exact same screen fraction across FOV settings and ADS zoom levels.

---

## 2. Core Pipeline Overview (Per-Player)

If **[ENGINE]** supports C/C++ or C FFI, compile `core/free_aim.c` directly; otherwise mirror its logic 1:1 and match [`core/test_free_aim.c`](core/test_free_aim.c).

Every frame, `fa_step()` runs in this exact order:

1. **Look input & `last_input` tracking:** Accepts stick (already curved), mouse, and gyro deltas in degrees (`+yaw` right, `+pitch` up), plus stick deflection (`0..1`). Gyro is treated identically to mouse. Stick only claims `last_input` when `stick_deflection >= 0.25` (`FA_EDGE_STICK_TILT`) so resting thumbs or stick drift do not cancel mouse/gyro Edge Auto-Turn.
2. **ADS blend & sensitivity:** Linearly ramps `ads_blend` `0 -> 1` over `ads_time`, applies smoothstep `b^2 * (3 - 2b)` when used, and scales all look inputs by `ads_sens`.
3. **Gameplay guard & FOV scaling:** Runs only while alive, able to look, and in normal gameplay (`gameplay_active != 0`); otherwise resets state (`fa_reset_state`). Scales `zone_x` and `zone_y` by `fov_scale` and `ads_zone`.
4. **Elliptical free-aim zone:**
   - Splits input via `camera_share` (`0..1`): `direct = input * share` rotates the camera immediately; the rest moves `target` scaled by `reticle_speed` (`0.25..3.0`, default `1.0`).
   - Turn drag subtracts `direct * turn_drag` from `display`.
   - Any push past `(x/zx)^2 + (y/zy)^2 > 1` clamps `target` back onto the ellipse and pushes the excess into `over` divided by `reticle_speed` so edge turning stays 1:1. Zone shrinks (e.g., ADS) push newly outside `target` into `over` as well.
   - Preserves exact 1:1 (`camera + target + over == input`) except where deliberately broken (`reticle_speed != 1` or Edge Auto-Turn).
5. **Edge smoothing (`edge_smoothing` s, `0` = rigid):**
   - `soft_cap = 0.25 * (zx + zy)`, `pressure = |over| / soft_cap`, `rate = (1 / edge_smoothing) * (1 + pressure^2)`, `k = 1 - exp(-rate * dt)`.
   - Drains `over * k` into camera turn and subtracts `over * k * (1 + turn_drag)` from `display`.
   - Flushes tiny leftovers (`|over| < 0.01°`) and hard-caps remaining `|over|` at `3 * soft_cap`.
6. **Edge Auto-Turn (`edge_turn_speed` deg/s, `0` = off):**
   - When the reticle sits in the outer `edge_band` of the ellipse and `last_input` is enabled (`edge_mouse`, `edge_gyro`, `edge_stick`), waits `0.15s` dwell then eases in over `0.20s`, turning outward at `speed * smoothstep(depth) * ease * ads_sens_mult` (vertical scaled by `edge_vertical`).
   - While active, manual edge overflow is scaled by `edge_influence`, and Camera Lead / Catch-Up pause.
7. **Camera Lead & Jerk-Free Catch-Up:**
   - Both transfer angular offset from `target` into the camera AND shift `display` by the exact same delta so the weapon stays locked on the same world point.
   - `idle_time` resets on any look input; both pause (`rate = 0`, `lead_ramp = 0`) during edge overflow or Edge Auto-Turn.
   - **Camera Lead (when moving / before `recenter_delay`):** `rate = camera_lead * edgeness * lead_input * smoothstep(lead_ramp)`, where `lead_input` tracks `clamp(input_speed / (0.5 * full_stick_speed), 0, 1)` smoothed with `tau = 0.1s`, and `lead_ramp` eases back over `0.3s` after edge turns.
   - **Jerk-Free Catch-Up (when `idle_time > recenter_delay`):** See Section 2.1 below.
8. **ADS pull (`ads_zone < 1.0`) & exaggeration (`ads_zone > 1.0`):**
   - When `ads_zone < 1.0`, swings the camera toward `target` at `rate = ads * (1 - ads_zone) * 5 / ads_time` so aim does not jump. Above `1.0` (up to `2.0`), exaggerates zone size, reduces camera share, increases drag, and lowers gun response.
9. **Camera & Walk Sway:**
   - Breathing (`0.27 Hz`) + slow drift (`0.61 Hz`) `* camera_sway`, plus footsteps (`0.9 Hz + move_fraction`) `* walk_sway * move_fraction`, scaled by `ads_sway` while aiming.
   - Applied as **delta since last frame only** so aim never drifts; hands lag body (`display -= sway_delta * turn_drag * 0.5`).
10. **Aim Assist:** Eases `assist` (`tau = 0.35s`) toward the locked target's screen angle, pulling the **reticle only** (never rotating the camera).
11. **Damped Spring & Lag Cap:**
    - `display` chases `target + over + assist` via a damped harmonic oscillator (`w = 2*pi*gun_response`, `k = w^2`, `c = 2*gun_damping*w`) in fixed `1/240s` substeps (max 64/frame).
    - Caps lag `|display - goal|` at `0.35 * min(zx, zy) + 1.5°` and scales `vel` by the same ratio.
12. **Outputs & Helpers:**
    - `fa_project_reticle_ndc()` — projects `reticle_deg` using world camera FOV & aspect.
    - `fa_compute_shot_dir()` — unit shot ray matching the projected reticle pixel.
    - `fa_compute_viewmodel_offset()` — ADS barrel alignment & weapon angular offset.
    - `force_original_aim` — global 1:1 bypass returning unmodded camera delta, zero reticle offset, and zero sway/spring without mutating saved settings.

### 2.1 Jerk-Free Catch-Up Math (C1-Continuous, Zero Overshoot)

Naive exponential recentering (`k = 1 - exp(-rate * dt)`) produces a noticeable camera-speed step (jerk) at `idle_time == recenter_delay` whenever `recenter_smooth` is small or transitions abruptly. This core eliminates the onset jerk with an explicit velocity-filtered integrator:

1. **Zero initial velocity:** Whenever Catch-Up is inactive (`idle_time <= recenter_delay`, active input, edge overflow, or Edge Auto-Turn), `catchup_vel = (0, 0)` and `catchup_env = 0`.
2. **Smoothstepped envelope:** Once `t_since = idle_time - recenter_delay > 0`, compute `env = smoothstep(t_since / T)` where `T = max(recenter_smooth, 0.02s)`, giving `env(0) = 0` and `env'(0) = 0`.
3. **Target angular velocity:** `v_des[i] = (recenter_speed * env) * target[i]` (deg/s).
4. **Asymmetric velocity smoothing:** Ease `catchup_vel[i]` toward `v_des[i]` with exponential filter `alpha = 1 - exp(-dt / tau)`:
   - Accelerating (`|v_des| >= |v_prev|`): `tau = max(T * 0.25, 0.01s)` for a C1-smooth ramp-in.
   - Decelerating (`|v_des| < |v_prev|`): `tau = min(tau_accel, 0.25 / max(recenter_speed, 0.1))` so velocity decays ahead of `target -> 0` without slamming into the stop.
5. **Trapezoidal integration & zero-overshoot clamp:**
   - `step[i] = 0.5 * (v_prev + v_new) * dt`, clamped strictly to `[0, target[i]]` (for `target > 0`) or `[target[i], 0]` (for `target < 0`).
   - Apply `target[i] -= step[i]`, `display[i] -= step[i]`, `cam_delta[i] += step[i]`.

Result: camera angular velocity starts strictly at `0 deg/s` at `idle_time == recenter_delay`, accelerates smoothly with zero velocity step, keeps the world aim point invariant, and converges monotonically to `0` with zero overshoot.

---

## 3. Presets & Reticle Profiles

Default preset is **Classic** (mod off). Built-in presets refresh on load (`fa_profile_on_load_refresh_builtin`) so tuning updates reach players; only **Custom 1–3** persist edits (`fa_preset_persists_edits`).

| Preset | Zone (X/Y°) | Share | Lead | Catch-Up (speed / delay / smooth) | Gun (Hz / damp / drag) | Edge Smooth | Sway (cam / walk) | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Classic** | — | — | — | — | — | — | — | Mod off (1:1 original aim) |
| **Weighty** | `7.5 / 4.5` | `0.5` | `0.8` | `1.0 / 0.25s / 0.4s` | `8.5 / 0.70 / 0.35` | `0.10s` | `0.08° / 0.25°` | ADS `1.3x`, `0.18s`, sway `35%`, sens `65%` |
| **Immersive** | `11.0 / 6.5` | `0.5` | `1.4` | `1.3 / 0.15s / 0.6s` | `4.6 / 0.52 / 0.60` | `0.20s` | `0.32° / 0.80°` | No reticle while aiming, ADS `0.26s` |
| **Arcade** | `28.0 / 18.0` | `0.0` | `0.0` | `0.0` (off) | `14.0 / 0.85 / 0.10` | `0.08s` | `0.03° / 0.10°` | Edge Auto-Turn `120°/s`, band `30%`, influence `80%`, own reticle profile (bigger, more opaque, smooth) |
| **Boring** | `0 / 0` | `1.0` | `0.0` | `0.0` (off) | `20.0 / 1.00 / 0.00` | `0.00s` | `0.00° / 0.00°` | Modern centered FPS |
| **Custom 1–3** | Copied | Copied | Copied | Copied | Copied | Copied | Copied | Copies from active preset on first edit; persists edits |

---

## 4. Recommended Menu Layout

- **Main Page**
  - Preset (`Classic`, `Weighty`, `Immersive`, `Arcade`, `Boring`, `Custom 1–3`)
  - **Aim & Camera Feel...**
  - **Reticle...**
  - **Testing & Tuning Tools...** (off by default)
  - **Force Original Aim** (global toggle at bottom; 1:1 bypass without overwriting saved settings)
- **Aim & Camera Feel**
  - *Main sliders (always shown):* Zone Width, Zone Height, Camera Share, Camera Lead, Catch-Up Speed, Catch-Up Delay, Gun Response
  - *Show Advanced Feel* checkbox (persisted) reveals: Catch-Up Smoothing, Gun Damping, Turn Drag, Edge Smoothing, **Free-Aim Reticle Speed (`25%–300%`)**, Camera Sway, Walk Sway, ADS Zoom, ADS Time, ADS Sway, Aim Feel While Aiming (`ads_zone`, `0%–200%`), ADS Sensitivity, Edge Auto-Turn Speed, Edge Band, Edge Input Influence, Edge Vertical, Edge Auto-Turn per-input toggles (Mouse / Gyro / Stick)
- **Reticle**
  - Hip-Fire Reticle (On / Off)
  - Reticle While Aiming (On / Off)
  - Size, Opacity, Smooth Reticle (sub-pixel drawing)
- **Testing & Tuning Tools** (all off by default)
  - Debug Overlay (`fa_get_debug_info`: draws ellipse `zx/zy`, `target`, `display`, `over`, active lead/catch-up rates & `catchup_vel`, `last_input`, `edge_turn_active`, `ads_blend`)
  - Scripted Look Pattern (`fa_scripted_input_sample`: repeatable 4.8s cycle for deterministic A/B tuning)
  - CSV Telemetry Logger (`fa_format_telemetry_csv_header` / `fa_format_telemetry_csv_row`)

---

## 5. Porting Rules

- **Frame-rate independent:** Always pass real `dt_sec`. When paused (`dt_sec <= 0` or non-finite), return zero camera/reticle deltas and preserve runtime state.
- **Minimal upstream footprint:** Keep edits in **[GAME]**'s own files to tiny hook calls; put all mod logic in dedicated source file(s).
- **Persist every setting:** Save live preset, Custom 1–3 slots, reticle profiles, `Show Advanced Feel`, and global `Force Original Aim` in **[GAME]**'s / the mod's config file.
- **Non-destructive additions:** When adding new options later, keep existing defaults and behavior unchanged unless explicitly requested.
- **Confirm before expensive loops:** Before running token-expensive in-game screenshot loops or long build/debug cycles, ask and wait for confirmation.
- **Credits & commit hygiene:** Follow [`docs/PORTING_CONVENTIONS.md`](../../docs/PORTING_CONVENTIONS.md) — no AI mentions in source code comments/headers, credits only in docs/README (`KaboWabo/Kabosune (me :3)` + AI assistance model name), `Co-Authored-By:` trailer in AI-assisted commits, and never include session or conversation links.
