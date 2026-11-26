# Porting Guide: FPV Drone (`libfpv`)

You are integrating a drop-in Betaflight-flown FPV quadcopter powered by **`libfpv`** (`vendor/libfpv`, upstream `https://github.com/hammerill/libfpv`) into **[GAME]** (**[ENGINE]** — e.g. C/C++ decomp, Rust recomp, Unity/BepInEx, Unreal/UE4SS, or native DLL mod loader).

---

## 1. What `libfpv` Provides

`libfpv` runs **real Betaflight firmware** (via SimITL) on its own dedicated **1 kHz simulation thread** (or explicit `fpv_step()` in `FPV_TIMING_MANUAL` / WebAssembly builds) and exposes a **plain C ABI** (`vendor/libfpv/include/libfpv.h`):

- **Real Betaflight flight controller:** Rates, PIDs, filters, arming checks, GPS/barometer, turtle mode, Betaflight CLI (`fpv_cli_exec`), `diff all` import (`fpv_import_betaflight_diff`), and live Betaflight Configurator WebSocket (`ws://localhost:5761`).
- **Hidden SDL3 RC input & setup wizard:** Reads EdgeTX/OpenTX/ELRS/Crossfire USB radios and gamepads in the background without stealing window focus, with a built-in radio setup wizard and keyboard/gamepad fallbacks. Hosts can also inject channels directly (`fpv_input_set_channels`) or register game actions on radio switches (`fpv_controls_set`).
- **Cameras & video feed:** Computes FPV (`frame.camera`), chase (`frame.chase_camera`), and line-of-sight (`frame.los_camera`) camera poses, Brown-Conrady fisheye lens distortion parameters, and optional analog/digital goggle video link noise (`frame.video_feed`).
- **Backend-agnostic 2D overlays:** Returns textured triangle draw lists (`fpv_draw_data`) for both the **Betaflight MAX7456 OSD** (`fpv_osd_draw_data` or CPU RGBA rasterizer `fpv_osd_render_rgba`) and the **Dear ImGui settings overlay** (`fpv_overlay_render`, toggled with `F8` by default).
- **Procedural motor & beeper audio:** Pull-based interleaved float PCM (`fpv_audio_configure` / `fpv_audio_read`) with onboard or pilot-position acoustics (distance attenuation, occlusion, Doppler, ground reflections).
- **Environment & gameplay hooks:** Static & dynamic triangle collision meshes, per-frame raycast callbacks (`fpv_world_set_raycast_callback` / `fpv_world_set_raycast_callback_ex`), weather wind, rotor/jet wind sources, explosion blasts (`fpv_wind_set`, `fpv_wind_set_sources`, `fpv_wind_add_blast`), portals (`fpv_portals_set`), cargo (`fpv_drone_set_cargo`), and GPS home (`fpv_gps_set_home`).

> **Licensing note:** Default single-library builds of `libfpv` embed Betaflight (GPL-3.0-or-later). Split builds (`-DLIBFPV_SPLIT_PROCESS=ON`) run Betaflight out-of-process in `libfpv-server`, allowing the client library (`include/libfpv.h`, `src/client`, `src/shared`) to be used under `GPL-3.0-or-later OR MIT`.

---

## 2. Key Public C API (`vendor/libfpv/include/libfpv.h`)

All structs passed in or out carry `uint32_t struct_size` (populated by `*_defaults()` helpers) for forward ABI compatibility (`LIBFPV_ABI_VERSION == 1`).

### Core Types
- `fpv_coord_system` / `fpv_coords_preset(preset, units_per_meter)` — Describes the host world's right/up/forward axes and scale (`FPV_COORDS_Y_UP_RIGHT_HANDED`, `FPV_COORDS_Y_UP_LEFT_HANDED`, `FPV_COORDS_Z_UP_LEFT_HANDED`, `FPV_COORDS_Z_UP_RIGHT_HANDED`). Every position, velocity, and `{x, y, z, w}` quaternion crossing the API is automatically converted to/from host conventions.
- `fpv_global_config` — `data_dir` (use `fpv_shared_data_dir()` so radio mappings & Betaflight EEPROM carry across games), `coords`, `timing` (`FPV_TIMING_THREAD` or `FPV_TIMING_MANUAL`), `flags` (`FPV_INIT_NO_DEVICE_INPUT`, `FPV_INIT_NO_CONFIGURATOR`, `FPV_INIT_NO_AUTO_PAUSE`, `FPV_INIT_NO_LENS_DISTORTION`, `FPV_INIT_VIDEO_FEED`), `log`, `host_name`/`host_build`/`host_url`.
- `fpv_drone` / `fpv_drone_desc` — Opaque drone handle (singleton per process because Betaflight is a singleton).
- `fpv_frame` — Per-frame snapshot filled by `fpv_drone_tick()`:
  - `position[3]`, `rotation[4]`, `velocity[3]`, `angular_velocity[3]`, `speed_kmh`, `altitude`
  - `camera`, `chase_camera`, `los_camera` (each `fpv_camera` has `position`, `rotation`, `forward`, `up`, `right`, `fov_y_degrees`, `near_plane`, `distortion_k1/k2/zoom`)
  - `motors[4]` (`rpm`, `thrust_newton`, `prop_damage`, `flags`), `battery`, `video`, `video_feed`
  - `flags` (`FPV_DRONE_ARMED`, `FPV_DRONE_CRASHED`, `FPV_DRONE_PAUSED`, `FPV_DRONE_ON_GROUND`, `FPV_DRONE_OSD_DISABLED`, `FPV_DRONE_RADIO_ACTIVE`, `FPV_DRONE_HEARD_FROM_PILOT`)
  - `impacts[FPV_MAX_IMPACTS]` + `impact_count`, `osd[FPV_OSD_ROWS * FPV_OSD_COLS]`, `wind[3]`
- `fpv_draw_data` / `fpv_texture` — Backend-agnostic textured triangle lists (`FPV_TEXTURE_OVERLAY_FONT`, `FPV_TEXTURE_OSD_FONT`) fetched with `fpv_texture_get()`.

### Frame Lifecycle
```c
// 1. One-time startup
fpv_global_config cfg;
fpv_global_config_defaults(&cfg);
char dir[1024];
fpv_shared_data_dir(dir, sizeof(dir));
cfg.data_dir = dir;
cfg.coords   = fpv_coords_preset(FPV_COORDS_Y_UP_RIGHT_HANDED, 1.0f);
fpv_global_init(&cfg);
fpv_audio_configure(48000, 2);

// 2. Register world collision (static meshes, dynamic objects, or raycast callback)
fpv_static_surfaces_load(&level_mesh, 1);
// or: fpv_world_set_raycast_callback_ex(host_raycast_ex, ctx);

// 3. Spawn drone when player activates FPV mode
fpv_drone_desc desc;
fpv_drone_desc_defaults(&desc);
// set desc.position, desc.rotation, desc.pilot_position
fpv_drone *drone = NULL;
fpv_drone_create(&desc, &drone);

// 4. Every rendered frame
fpv_frame frame;
frame.struct_size = sizeof(frame);
fpv_drone_tick(drone, frame_dt, &frame);
// - Update game camera from frame.camera / frame.chase_camera / frame.los_camera
// - Draw drone mesh/props at frame.position / frame.rotation using frame.motors[i].rpm
// - Render fpv_osd_draw_data(&frame, w, h, style) unless (frame.flags & FPV_DRONE_OSD_DISABLED)
// - Render fpv_overlay_render(frame_dt) and suppress game input if fpv_overlay_wants_input()

// 5. Audio callback thread (optional)
fpv_audio_read(interleaved_float_buf, num_frames);

// 6. Shutdown / unload
fpv_drone_delete(drone);
fpv_global_terminate();
```

---

## 3. Discovery-First Host Game Hooks

Before writing integration code in **[GAME]**, discover and document these 6 host hooks:

1. **Coordinate System & Scale:** Confirm **[ENGINE]**'s right/up/forward axes and `units_per_meter` for `fpv_coord_system`.
2. **World Collision Hook:**
   - *Small/static levels (decomps, arenas):* Export level geometry into `fpv_mesh` triangles and call `fpv_static_surfaces_load()`, plus `fpv_surface_object_create/move/delete` for doors/elevators.
   - *Large/open worlds (GTA, Unreal, Unity streaming):* Register `fpv_world_set_raycast_callback` or `fpv_world_set_raycast_callback_ex` wrapping **[ENGINE]**'s physics raycast. Callbacks always execute on the host thread inside `fpv_drone_tick()`.
3. **Camera Override:** Locate where **[GAME]** computes its active view matrix / FOV so the mod can switch between `FPV_VIEW_FPV` (`frame.camera`), `FPV_VIEW_CHASE` (`frame.chase_camera`), `FPV_VIEW_LOS` (`frame.los_camera`), and `FPV_VIEW_PILOT` (normal game camera) and call `fpv_set_view()`.
4. **2D Overlay Rendering (OSD + Settings):** Identify the post-scene 2D render pass (OpenGL, D3D11/12, Vulkan, or engine UI canvas) to draw `fpv_osd_draw_data()` and `fpv_overlay_render()` textured triangle lists. Forward keyboard scancodes (`fpv_input_key`), mouse (`fpv_overlay_mouse_*`), and text (`fpv_overlay_text`) when the overlay is open, and block game input while `fpv_overlay_wants_input()` is non-zero. See `vendor/libfpv/docs/renderers/` for ready-made OpenGL shaders.
5. **Procedural Audio Hook:** Hook **[ENGINE]**'s audio mixer or open a lightweight audio stream (SDL/WASAPI/miniaudio) that pulls samples via `fpv_audio_read()`. Update `fpv_audio_set_listener()` when in `FPV_VIEW_PILOT`.
6. **Toggle Hotkey & Drone Visuals:** Choose a non-conflicting hotkey to spawn/despawn or toggle FPV flight, plus a simple 3D quadcopter visual (or debug mesh) placed at `frame.position` / `frame.rotation`.

---

## 4. Safety Guards

- **Player Avatar While Flying:** Lock or freeze the player character's movement inputs while viewing through the drone (`FPV_VIEW_FPV` / `CHASE` / `LOS`), keep `fpv_drone_set_pilot_position()` updated to the avatar's feet, and optionally use `fpv_pilot_set_support()` (`FPV_PILOT_STAND_IN`, `FPV_PILOT_POSE`) for games that stream chunks around the player.
- **Pause, Cutscenes & Loading Screens:** Call `fpv_set_paused(1)` during pause menus, cutscenes, or dialogue; `libfpv` also auto-pauses after `auto_pause_seconds` (default `0.5s`) without a tick unless `FPV_INIT_NO_AUTO_PAUSE` is set.
- **Level Unload / Respawn / Death:** Call `fpv_static_surfaces_clear()` and `fpv_drone_delete()` cleanly on level transitions, player death, or mod unload before calling `fpv_global_terminate()`.
- **OSD Disable Switch:** Respect `frame.flags & FPV_DRONE_OSD_DISABLED` by hiding any mod HUD elements over the FPV view as well.

---

## 5. Build & Integration Notes

### Submodule Initialization
```sh
git clone --recurse-submodules <repo-url>
# or in an existing checkout:
git submodule update --init --recursive
```
Note: `vendor/libfpv` itself contains submodules (`third_party/betaflight`, `third_party/SimITL`, `third_party/SDL`, etc.) that `libfpv`'s CMake configure step patches automatically.

### Building `libfpv`
- **Linux / macOS:**
  ```sh
  cmake -S mods/fpv-drone/vendor/libfpv -B build/libfpv
  cmake --build build/libfpv -j
  ```
- **Windows (MinGW-w64 cross-compile or MSYS2 MinGW64):** Produces standalone `libfpv.dll` + `libfpv.def` with statically linked runtimes. MSVC hosts can generate an import lib (`lib /def:libfpv.def /machine:x64`) or load dynamically with `LoadLibrary` / `GetProcAddress`.
- **Rust / C# / Unity / Unreal FFI:** Because `include/libfpv.h` is a flat C ABI with explicit `struct_size` fields, generate bindings directly with `bindgen` (Rust) or P/Invoke (`[DllImport("libfpv")]` in C#), or link `libfpv.dll` / `libfpv.so` from a native C++ plugin.
- **Avoiding duplicate SDL3:** If **[ENGINE]** already links SDL3 and conflicts occur (e.g. macOS Obj-C class warnings), build with `-DLIBFPV_WITH_SDL=OFF` and feed channels via `fpv_input_set_channels()`.
- See `vendor/libfpv/docs/INTEGRATION.md` and `vendor/libfpv/examples/standalone/main.cpp` for complete reference integrations.
