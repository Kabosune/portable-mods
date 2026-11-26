# Porting Conventions

Shared architectural and workflow rules for porting any module in `portable-mods` into a target game or engine.

---

## 1. Two-Layer Architecture (Core + Adapter)

Every mod in this repository is split into two strict layers:

1. **Core (`mods/<mod>/core/` or `vendor/`):**
   - Pure C11 (or plain C ABI), zero OS or game-engine dependencies.
   - Deterministic, self-contained, and covered by standalone unit tests.
   - Compile the core directly whenever the target engine/mod loader supports C/C++ or C FFI. If the target language cannot link C (e.g. pure managed C# or GDScript), translate the core 1:1 and port its unit tests first.
2. **Engine Adapter (written in the target game repo):**
   - Minimal hook calls inside the game's own files; all adapter logic lives in dedicated mod files so upstream merges stay clean.
   - Owns coordinate/unit conversion, input observation, safety guards, config persistence, and UI/overlay rendering.

---

## 2. Discovery First (Never Guess Hooks)

Before writing any hook or gameplay code in a target game:
1. Inspect the target codebase, SDK, or binary and locate the exact hook sites required by that module's `PORTING.md`.
2. Confirm the engine's coordinate system (up axis, forward axis, handedness) and world unit scale (`units_per_meter`).
3. Confirm whether the engine or input backend applies hidden deadzones, air-drag caps, or post-hook gravity.
4. Report the discovered hooks and any missing capabilities (e.g. no gyro sensor API) before implementing.

---

## 3. Coordinate & Unit Adapters

- Never modify a module's `core/` math to match a single game's axis conventions or unit scale.
- Convert inputs into the core's documented coordinate space at the adapter boundary, run the core step, and convert outputs back into the game's native axes and units.
- Verify axes and units with read-only telemetry/logging before enabling gameplay writes.

---

## 4. Frame-Rate Independence & Pause Safety

- Always drive physics, boost timers, smoothing filters, and cooldowns with real delta time (`dt_sec`).
- When paused or stalled (`dt_sec <= 0` or non-finite), return zero output / no-op and preserve runtime state.
- Guard all floating-point inputs from the host against `NaN` and `Inf`.
- Never overwrite player state during death, ragdolls, cutscenes, pause menus, loading screens, vehicles, swimming, or scripted traversal.

---

## 5. Config Persistence & Non-Destructive Defaults

- Persist every user-facing setting in the game's or mod's config file (per-player when split-screen exists).
- Keep mods non-intrusive by default (e.g. movement overrides disabled at startup behind a toggle; stick curves defaulting to `Classic`; tournament/fair-play `Force Original` toggles that never overwrite saved settings).
- When adding a new option to an existing port, do not change existing defaults or behavior unless explicitly requested.

---

## 6. Credits & Commit Hygiene

- **No AI credits or mentions in source code:** Keep code comments and headers strictly technical.
- **Credits belong in docs only:** Credit `KaboWabo/Kabosune (me :3)` and AI assistance (with the model name) in `README.md` / documentation files only.
- **Commit trailers:** Commits created with AI assistance end with a `Co-Authored-By:` trailer for the model.
- **No session or conversation links:** Never include chat session URLs, conversation IDs, or transcript links in commit messages, code, or documentation.
