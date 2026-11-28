#pragma once

#include "control_helpers.h"
#include "fpv_bridge.h"
#include "free_aim.h"
#include "gym_map.h"
#include "input_manager.h"
#include "math_types.h"
#include "physics_world.h"
#include "qm_physics.h"
#include "renderer.h"
#include "sandbox_settings.h"
#include "weapon_audio.h"
#include "weighty_aim.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <string>
#include <vector>

namespace sb {

struct SandboxApp {
    // World & engine subsystems
    GymMap           gym;
    PhysicsWorld     physics;
    InputManager     input;
    Renderer         renderer;
    FpvBridge        fpv;
    WeaponAudioSynth weapon_audio;
    SDL_AudioStream* weapon_audio_stream = nullptr;

    // Mode & UI state
    bool       fpv_mode                 = false;
    FpvCamView fpv_cam_view             = FpvCamView::Fpv;
    bool       ui_open                  = false;
    bool       show_hud                 = true;
    bool       fullscreen               = false;
    bool       controller_bypasses_menu = true;
    bool       ads_toggle_mode          = false;
    bool       ads_toggled_on           = false;
    TeleportId last_teleport            = TeleportId::HubSpawn;
    int        requested_ui_tab         = -1; // -1 = none, 0=Sandbox, 1=Quake Move, 2=Weighty Aim, 3=Free-Aim, 4=FPV

    // Local settings INI persistence
    std::string settings_path = "sandbox_settings.ini";
    std::string settings_status;

    // Camera & FOV settings (world FOV is kept strictly separate from viewmodel FOV)
    float base_world_fovy_deg = 75.0f; // Vertical FOV (~107 deg horizontal at 16:9)
    float current_world_fovy  = 75.0f;
    float viewmodel_fovy_deg  = 54.0f;
    float cam_yaw_rad         = 0.0f;  // qm_physics convention: 0 = +Y, +CCW
    float cam_pitch_rad       = 0.0f;  // +up, clamped to [-89, +89] deg

    // Player physical state (Z-up meters)
    CharacterMoveResult  player_char{};
    float                smoothed_eye_h = PhysicsWorld::kStandEyeHeight;
    WeaponFireController weapon_fire{};
    float                recoil_kick        = 0.0f;
    float                recoil_vel         = 0.0f;
    float                recoil_roll_deg    = 0.0f;
    float                muzzle_flash_timer = 0.0f;
    float                muzzle_flash_angle = 0.0f;
    uint32_t             shot_visual_seq    = 0u;
    float                hit_marker_timer   = 0.0f;
    bool                 hit_marker_was_target = false;
    std::string          last_hit_text;
    AimStats             aim_stats{};

    // Fixed-timestep physics clock, menu router & edge-input latch
    int             physics_tick_hz  = 120;
    float           sim_accumulator  = 0.0f;
    double          sim_time_sec     = 0.0;
    float           fps_smoothed     = 60.0f;
    InputEdgeLatch  edge_latch{};
    MenuInputRouter menu_router{};

    // 1. quake-movement state
    qm_preset_id_t qm_preset      = QM_PRESET_QUAKE_VQ3;
    qm_params_t    qm_params{};
    qm_state_t     qm_state{};
    float          qm_speed_scale = 1.0f;

    // 2. weighty-aim state
    wa_player_t wa_player{};
    bool        wa_force_original_aim  = false;
    float       full_stick_deg_per_sec = 210.0f;
    float       mouse_sens_deg_per_px  = 0.11f;
    float       last_look_stick_mag    = 0.0f;
    float       last_raw_stick[2]      = {0.0f, 0.0f};
    float       last_wa_stick_out[2]   = {0.0f, 0.0f};
    float       last_gyro_out_deg[2]   = {0.0f, 0.0f};
    bool        demo_stick_active      = false;
    float       demo_stick[2]          = {0.0f, 0.0f};

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
    void reset_aim_stats();
    void set_fpv_launch_pad(int pad_index);
    void set_drone_spawn_at_player();
    void set_qm_preset(qm_preset_id_t preset);
    void toggle_fpv_mode();
    void toggle_csv_logging(bool enable);

    // Local INI settings persistence
    SandboxSettings capture_settings() const;
    void            apply_settings(const SandboxSettings& s, bool apply_fullscreen = true);
    bool            save_settings();
    bool            load_settings(bool apply_fullscreen = true);
    void            reset_all_defaults();

    void step_frame(float real_dt_sec, bool window_focused);
    void render(int fb_w, int fb_h, int win_w, int win_h, float ui_scale, float frame_dt);

private:
    static void SDLCALL weapon_audio_callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void step_fps_aim(const InputFrame& in, float dt_sec, bool active_gameplay);
    void step_fps_physics(const InputFrame& in, float dt_sec);
    void fire_hitscan();
};

} // namespace sb
