#pragma once

#include "control_helpers.h"
#include "gym_map.h"
#include "input_manager.h"
#include "math_types.h"
#include "renderer.h"
#include "libfpv.h"

#include <SDL3/SDL.h>
#include <set>
#include <string>
#include <vector>

namespace sb {

enum class FpvCamView {
    Fpv = 0,
    Chase = 1,
    Los = 2
};

class FpvBridge {
public:
    FpvBridge() = default;
    ~FpvBridge();

    bool init(const MeshData& gym_mesh, Vec3 spawn_pos, const std::string& data_dir);
    void init_gl_renderers();
    void shutdown();

    bool is_ready() const { return initialized_ && drone_ != nullptr; }

    void reset_drone();
    void set_spawn_pos(Vec3 pos);
    Vec3 spawn_pos() const { return spawn_pos_; }
    void set_pilot_position(Vec3 feet_pos);
    void set_active_mode(bool fpv_active, FpvCamView view);

    // Input forwarding into libfpv (always releases held keys on KEY_UP to prevent stuck keys)
    void handle_sdl_event(const SDL_Event& e, bool fpv_active, bool allow_new_presses);
    void release_all_keys();
    void apply_host_controls(const InputFrame& in, bool fpv_active, bool has_gamepad);

    // Toggle arm cleanly in either gamepad RC mode or libfpv keyboard mode (synthetic Enter pulse)
    void trigger_arm_toggle(bool has_gamepad);

    // Per-frame tick & presentation
    void tick(float dt_sec);
    void render_fpv_video_and_osd(GLuint scene_tex, int scene_w, int scene_h, int fb_w, int fb_h, int win_w, int win_h, bool fpv_active, FpvCamView view);
    void render_libfpv_overlay(float dt_sec, int fb_w, int fb_h, int win_w, int win_h, float ui_scale);

    bool overlay_is_open() const;
    bool overlay_wants_input() const;
    void set_overlay_open(bool open);

    // Camera / audio / Betaflight CLI controls exposed to the Tuning Panel
    void set_audio_volume(float vol);
    void set_beep_volume(float vol);
    void set_muted(bool muted);
    void apply_rates_scale(float rate_scale, bool save_to_eeprom = false);
    void select_preset(int index);

    const fpv_frame&          frame() const { return frame_; }
    const std::vector<std::string>& preset_names() const { return preset_names_; }
    int                       current_preset_index() const { return current_preset_idx_; }
    DroneRenderParams         drone_render_params(bool visible) const;
    Mat4                      active_view_matrix(FpvCamView view, Vec3& out_eye, float& out_fovy, float& out_near) const;

    // Public UI-adjustable settings
    FpvGamepadRcMapper gp_rc{};
    bool               ui_angle_mode          = true;
    bool               show_osd               = true;
    float              camera_tilt_offset_deg = 0.0f; // Extra tilt offset (-25..+35 deg) on top of preset uptilt
    float              rates_scale            = 1.0f;
    float              audio_volume           = 0.65f;
    float              beep_volume            = 0.50f;
    bool               audio_muted            = false;
    bool               use_gamepad_rc         = true;
    std::string        last_cli_status;

private:
    static void SDLCALL sdl_audio_callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    static int  sdl_scancode_to_fpv_key(SDL_Scancode sc);

    bool             initialized_             = false;
    bool             gl_ready_                = false;
    bool             video_ready_             = false;
    bool             host_channels_active_     = false;
    bool             pending_kb_enter_release_ = false;
    bool             pending_kb_turtle_release_ = false;
    fpv_drone*       drone_                   = nullptr;
    fpv_frame        frame_{};
    Vec3             spawn_pos_{38.0f, -6.0f, 0.14f};
    double           prop_angles_[4]          = {0, 0, 0, 0};
    SDL_AudioStream* audio_stream_            = nullptr;
    std::set<int>    held_fpv_keys_;

    std::vector<std::string> preset_names_;
    int                      current_preset_idx_ = 0;

    struct GlState;
    GlState* gl_ = nullptr;
};

} // namespace sb
