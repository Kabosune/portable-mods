# Porting Guide: Weighty Aim (Stick Response, Turn Boost & Gyro Aim)

You are porting the stick/camera response system, Look Acceleration (turn boost), and GyroWiki-style gyro aim into **[GAME]** (**[ENGINE / MOD SETUP]** — e.g. C/C++ decomp, source port, Rust recomp, Unity/BepInEx, or Unreal/UE4SS).

Reference implementation (public, MIT):
- `https://github.com/Kabosune/perfect_dark_weighty_aim` (branch `weighty-aim`) — see `WEIGHTY_AIM_DESIGN.md`, `port/src/weightyaim.c`, and `port/src/weightyaimmenu.c`.
- Portable pure-C11 core in this repository: [`core/weighty_aim.h`](core/weighty_aim.h) and [`core/weighty_aim.c`](core/weighty_aim.c) (port the behavior, not N64-specific plumbing).

---

## 1. Discovery First (Report Before Writing Code)

Before writing any code, inspect **[GAME]** / **[ENGINE]** and report back:
1. **Look-stick hook site:** Where **[GAME]** reads the look stick and turns it into camera rotation (yaw/pitch), plus its native response curve, built-in deadzone, and max turn speed.
2. **Hidden engine/input deadzones:** Check whether the engine or input backend (SDL, XInput, Steam Input, Unity InputSystem, etc.) applies its own deadzone before the game sees the stick, and report it.
3. **Motion sensor access:** How (or whether) **[ENGINE]** exposes controller gyro + accelerometer data (e.g. SDL gamepad sensors, Steam Input, DualSense/Switch HID, or engine input API). If there is no native sensor access, report the available options instead of guessing.

---

## 2. Core Features to Build (Per-Player If Split-Screen Exists)

If **[ENGINE]** supports C/C++ or C FFI, compile `core/weighty_aim.c` directly; otherwise mirror its logic 1:1 and match [`core/test_weighty_aim.c`](core/test_weighty_aim.c).

### 2.1 Stick Curves (Applied After Deadzones)
- **Classic** (`WA_CURVE_CLASSIC`, default unless specified otherwise): **[GAME]**'s own native response untouched (sensitivity and turn boost still apply).
- **Linear** (`WA_CURVE_LINEAR`): `out = n`
- **Balanced** (`WA_CURVE_BALANCED`): `out = n^1.5` (halfway between linear and squared)
- **Custom 1–3** (`WA_CURVE_CUSTOM1..3`): Cubic Bezier from `(0, 0)` to `(1, 1)` with control points `(x1, y1), (x2, y2)` in `[0, 1]` (DS4Windows style). Solve `x(t) = n` via 24-step bisection and return `y(t)`. Default points: `(0.4, 0.0, 0.75, 1.0)`. Each custom slot remembers its own points.

### 2.2 Radial Deadzones (Non-Classic Curves)
- Use **radial** deadzones (never per-axis) so diagonals track straight lines:
  - `mag = length(stick)`
  - `n = clamp((mag - inner) / (outer - inner), 0, 1)`
  - `out_dir = (stick / mag) * curve(n)`
- **Inner deadzone:** `0%–40%` (default `1%`).
- **Outer deadzone:** `10%–100%` (default `95%`), always clamped so `outer >= inner + 5%`.
- **Game's Built-In Deadzone** toggle (`game_builtin_deadzone`): when **[GAME]** applies its own deadzone, expose a toggle so players can bypass it and let only this mod's deadzones apply.

### 2.3 Sensitivity
- **Sensitivity:** `0.25x–2.5x` (default `1.0x`).
- **Vertical Sensitivity:** `25%–200%` of horizontal (default `80%`).

### 2.4 Look Acceleration ("Turn Boost")
- **Modes:** `Off` / `Instant` / `Ramped` (default `Ramped`).
- Once deflection `>= threshold` (`0.3–1.0`, default `0.9`):
  - **Instant:** full boost (`level = 1.0`) immediately.
  - **Ramped:** wait `delay` (default `0.1s`), then ramp `0 -> 1` over `boost_time` (default `0.25s`) with smoothstep `t^2 * (3 - 2t)`.
- Boost level only rises while held past the threshold; on release it falls smoothly over `~0.1s` (never in a single frame).
- **Multiplier:** `1 + (amount - 1) * level`, `amount` in `1x–4x` (default `2.0x`).
- **Vertical Acceleration (`boost_vertical`):** vertical axis receives only `boost_vertical` of the boost (default `35%`).

### 2.5 Live Curve Graph in the Settings Menu
Use `wa_build_curve_graph()` to drive the menu widget:
- Axes: `x = stick deflection (0..1)`, `y = turn speed` scaled to `max(sensitivity, 1.0)`.
- Draw:
  1. Background box and border.
  2. Tinted band for the inner deadzone and a distinct tinted band past the outer deadzone (non-Classic).
  3. Subtle `25% / 50% / 75%` grid lines.
  4. Dotted linear reference line.
  5. Live curve (updating immediately as sliders move).
  6. For Custom 1–3, both Bezier control points and their handle lines to `(inner, 0)` and `(outer, sens)`.
  7. Optional live stick deflection marker.

### 2.6 Separate Reset Actions
- **Reset Stick Response** (`wa_reset_stick_response`): resets curve, inner/outer deadzones, sensitivity, vertical sensitivity, and built-in deadzone toggle. Leaves Turn Boost and saved Custom 1–3 Bezier points intact.
- **Reset Look Acceleration** (`wa_reset_boost`): resets Turn Boost settings only.
- **Reset Gyro Settings** (`wa_reset_gyro`): restores gyro tuning defaults while **keeping the current gyro mode** (resetting tuning must not switch gyro off).

### 2.7 Gyro Aim (JibbSmart's GyroWiki Pipeline)
- **Not a stick:** Gyro never passes through stick curves, stick deadzones, or turn boost. Treat it like mouse look — real degrees in, in-game degrees out — added to the mouse look path so ADS sensitivity or free-aim applies automatically.
- **Mode:** `Off` / `Always On` / `While Aiming` (default `Off`).
- **Sensitivity:** `0.1–20.0` in-game degrees per real degree (`1.0 = 1:1`, default `2.5`).
- **Vertical Sensitivity:** `0%–200%` of horizontal (default `90%`).
- **Invert Vertical:** toggle (default off).
- **Gyro Space (Advanced):**
  - **Player (default):** yaw from both turning and leaning the controller, regardless of hold angle. Maintain a smoothed unit gravity vector from the accelerometer (exponential smoothing, `tau = 0.1s`):
    - `worldyaw = dot((yaw, roll), (grav_y, grav_z))`
    - `yaw = sign(worldyaw) * min(|worldyaw| * 1.41, length(yaw, roll))` (`1.41` gives ~45° of freedom)
    - `pitch` is always the controller's local pitch.
  - **Local:** `yaw` = controller's local yaw axis only.
- **Calibration:**
  - **Calibrate** button: averages raw gyro samples over `1.5s` while held still and stores the bias.
  - **Auto-Calibrate When Still** (advanced, default ON): when all 3 axes stay within `< 2 deg/s` of the bias for `> 1.0s`, exponentially settle the bias toward the reading (`tau = 1.0s`).
  - Subtract bias from every sample; output `0` while calibrating.
- **Menu Tick While Paused:** Keep sampling and calibrating while the Gyro Aim / Gyro Advanced menu pages are open even though gameplay is paused (`wa_gyro_menu_tick` uses real wall-clock `dt`, falling back to `1/60s` when `dt <= 0` or `dt > 0.25s`).
- **Exact Status Line Strings (`wa_gyro_status_text`):**
  - `"No gyro found on this controller"`
  - `"Calibrating... keep the controller still"`
  - `"Gyro ready (calibrated)"`
  - `"Gyro found - calibrate for best results"`
- **Per-Frame Processing Order** (deg/s after bias subtraction; `speed = length(yaw, pitch)`):
  1. **Tightening** (`0–30 deg/s`, default `0` / Off): below threshold, scale `(yaw, pitch)` by `speed / threshold` (hides hand tremor with no hard cutoff).
  2. **Soft Tiered Smoothing** (`0–30 deg/s`, default `4 deg/s`): exponential smoothing (`tau = 0.1s`) blended only on small moves — fully smoothed `< threshold/2`, fully direct `>= threshold`, linear blend between. Large flicks are never delayed.
  3. **Acceleration** (`1–4x`, default `1` / Off; threshold `5–500 deg/s`, default `75 deg/s`): `sens *= 1 + (accel - 1) * clamp(speed / threshold, 0, 1)`.
  4. **Output Degrees:** `out_yaw = -yaw * sens * dt`, `out_pitch = pitch * sens * vertical * dt * (invert ? -1 : 1)`. Verify sign conventions against **[GAME]**'s camera (SDL convention: `x = pitch`, `y = yaw` where `+` turns left, `z = roll`).
- **Pause While Using Look Stick** (advanced, default off): ignore gyro while the look stick is moving.
- **Smoothing State Reset:** Reset the smoothing filter state whenever gyro is off, forced original, paused by stick, calibrating, or set to `While Aiming` while not aiming.

### 2.8 Global "Force Original Aim" Switch
- Provide a global **Force Original Aim** toggle (for fair / tournament play) that makes look input 1:1 with the unmodded game — native curve, native deadzone, no turn boost, no gyro — **without overwriting any saved settings**, so disabling it restores every player's tuning immediately.

---

## 3. Recommended Menu Layout

- **Stick Response**
  - Look Curve (`Classic`, `Linear`, `Balanced`, `Custom 1–3`)
  - Live Curve Graph
  - Sensitivity (`0.25x–2.5x`)
  - Vertical Sensitivity (`25%–200%`)
  - Inner Deadzone (`0%–40%`)
  - Outer Deadzone (`10%–100%`)
  - Custom Curve `X1`, `Y1`, `X2`, `Y2` (`0.0–1.0`)
  - Game's Built-In Deadzone (checkbox)
  - Reset Stick Response
- **Look Acceleration**
  - Mode (`Off`, `Instant`, `Ramped`)
  - Acceleration Speed (`1x–4x`)
  - Stick Threshold (`30%–100%`)
  - Acceleration Delay (`0–0.5s`)
  - Ramp-Up Time (`0–1.0s`)
  - Vertical Acceleration (`0%–100%`)
  - Reset Look Acceleration
- **Gyro Aim**
  - Status line (`wa_gyro_status_text`)
  - Gyro Aim (`Off`, `Always On`, `While Aiming`)
  - Sensitivity (`0.1–20.0`)
  - Vertical Sensitivity (`0%–200%`)
  - Calibrate (hold still)
  - Advanced... -> **Gyro Advanced**
  - Reset Gyro Settings
- **Gyro Advanced**
  - Gyro Space (`Player`, `Local`)
  - Invert Vertical
  - Acceleration (`1x–4x`)
  - Acceleration Threshold (`5–500 deg/s`)
  - Tightening (`0–30 deg/s`)
  - Smoothing (`0–30 deg/s`)
  - Auto-Calibrate When Still
  - Pause While Using Look Stick

---

## 4. Porting Rules

- **Frame-rate independent:** Always pass real `dt_sec`. When paused (`dt_sec <= 0`), return zero output and preserve runtime state.
- **Minimal upstream footprint:** Keep edits in **[GAME]**'s own files to tiny hook calls; put all mod logic in its own source file(s).
- **Persist every setting:** Save all stick, boost, custom Bezier profile, gyro, and global Force Original settings in **[GAME]**'s / the mod's config file.
- **Non-destructive additions:** When adding a new option later, keep existing defaults and behavior unchanged unless explicitly requested.
- **Confirm before expensive loops:** Before running token-expensive in-game screenshot loops or long build/debug cycles, ask and wait for confirmation.
- **Credits & commit hygiene:** Follow [`docs/PORTING_CONVENTIONS.md`](../../docs/PORTING_CONVENTIONS.md) — no AI mentions in source code comments/headers, credits only in docs/README, `Co-Authored-By:` trailer in AI-assisted commits, and never include session/conversation links.
