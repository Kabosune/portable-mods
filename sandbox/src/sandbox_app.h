#pragma once

#include "fpv_bridge.h"
#include "free_aim.h"
#include "gym_map.h"
#include "input_manager.h"
#include "math_types.h"
#include "physics_world.h"
#include "qm_physics.h"
#include "renderer.h"
#include "weighty_aim.h"

#include <cstdio>
#include <string>
#include <vector>

namespace sb {

struct SandboxApp {
    // World & engine subsystems
    GymMap       gym;
    PhysicsWorld physics;
    InputManager input;
    Renderer     renderer;
    FpvBridge    fpv;

    // Mode & UI state
    bool       fpv_mode         = false;
    FpvCamView fpv_cam_view     = FpvCamView::Fpv;
    bool       ui_open          = false;
    bool       show_hud         = true;
    bool       ads_toggle_mode  = false;
    bool       ads_toggled_on   = false;
    TeleportId last_teleport    = TeleportId::HubSpawn;

    // Camera & FOV settings (world FOV is kept strictly separate from viewmodel FOV)
    float base_world_fovy_deg = 75.0f; // Vertical FOV (~107 deg horizontal at 16:9)
    float current_world_fovy  = 75.0f;
    float viewmodel_fovy_deg  = 54.0f;
    float cam_yaw_rad         = 0.0f;  // qm_physics convention: 0 = +Y, +CCW
    float cam_pitch_rad       = 0.0f;  // +up, clamped to [-89, +89] deg

    // Player physical state (Z-up meters)
    CharacterMoveResult player_char{};
    float               smoothed_eye_h = PhysicsWorld::kStandEyeHeight;
    float               recoil_kick    = 0.0f;
    float               fire_cooldown  = 0.0f;
    float               hit_marker_timer = 0.0f;
    bool                hit_marker_was_target = false;
    std::string         last_hit_text;

    // Fixed-timestep physics clock & edge-input latch (prevents dropped presses when FPS > tick Hz)
    int            physics_tick_hz  = 120;
    float          sim_accumulator  = 0.0f;
    double         sim_time_sec     = 0.0;
    float          fps_smoothed     = 60.0f;
    InputEdgeLatch edge_latch{};

    // 1. quake-movement state
    qm_preset_id_t qm_preset   = QM_PRESET_QUAKE_VQ3;
    qm_params_t    qm_params{};
    qm_state_t     qm_state{};
    float          qm_speed_scale = 1.0f;

    // 2. weighty-aim state
    wa_player_t wa_player{};
    bool        wa_force_original_aim = false;
    float       full_stick_deg_per_sec = 210.0f;
    float       mouse_sens_deg_per_px  = 0.11f;
    float       last_look_stick_mag    = 0.0f;
    float       last_gyro_out_deg[2]   = {0.0f, 0.0f};

    // 3. free-aim state
    fa_profile_bank_t fa_bank{};
    fa_state_t        fa_state{};
    fa_output_t       fa_out{};
    bool              fa_force_original_aim = false;
    bool              fa_debug_ellipse      = false;
    bool              fa_scripted_look      = false;
    bool              fa_csv_logging        = false;
    std::FILE*        fa_csv_file           = nullptr;
    std::string       fa_csv_path;

    // Hitscan tracers
    std::vector<TracerBeam> tracers;

    bool init(const std::string& fpv_data_dir);
    void init_gl();
    void shutdown();

    void teleport(TeleportId id);
    void reset_player();
    void set_qm_preset(qm_preset_id_t preset);
    void toggle_fpv_mode();
    void toggle_csv_logging(bool enable);

    void step_frame(float real_dt_sec, bool window_focused);
    void render(int fb_w, int fb_h, int win_w, int win_h, float ui_scale, float frame_dt);

private:
    void step_fps_aim(const InputFrame& in, float dt_sec, bool active_gameplay);
    void step_fps_physics(const InputFrame& in, float dt_sec);
    void fire_hitscan();
};

} // namespace sb
