#include "fpv_bridge.h"

#include "gl_loader.h"

#define FPV_RENDER_GL3_IMPLEMENTATION
#include "fpv_render_gl3.h"
#define FPV_VIDEO_GL3_IMPLEMENTATION
#include "fpv_video_gl3.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sb {

struct FpvBridge::GlState {
    fpv_gl3       ui{};
    fpv_video_gl3 video{};
};

static void fpv_log_cb(void*, fpv_log_level level, const char* msg) {
    if (level >= FPV_LOG_WARN) {
        SDL_Log("[libfpv] %s", msg);
    }
}

FpvBridge::~FpvBridge() {
    shutdown();
}

void SDLCALL FpvBridge::sdl_audio_callback(void*, SDL_AudioStream* stream, int additional, int) {
    static thread_local std::vector<float> pcm;
    const int frames = additional / static_cast<int>(2 * sizeof(float));
    if (frames <= 0) return;
    pcm.resize(static_cast<size_t>(frames) * 2);
    fpv_audio_read(pcm.data(), static_cast<uint32_t>(frames));
    SDL_PutAudioStreamData(stream, pcm.data(), frames * 2 * static_cast<int>(sizeof(float)));
}

int FpvBridge::sdl_scancode_to_fpv_key(SDL_Scancode k) {
    if (k >= SDL_SCANCODE_A && k <= SDL_SCANCODE_Z) return 'A' + static_cast<int>(k - SDL_SCANCODE_A);
    if (k >= SDL_SCANCODE_1 && k <= SDL_SCANCODE_9) return '1' + static_cast<int>(k - SDL_SCANCODE_1);
    if (k >= SDL_SCANCODE_F1 && k <= SDL_SCANCODE_F12) return FPV_KEY_F1 + static_cast<int>(k - SDL_SCANCODE_F1);
    switch (k) {
    case SDL_SCANCODE_0:         return '0';
    case SDL_SCANCODE_TAB:       return FPV_KEY_TAB;
    case SDL_SCANCODE_RETURN:    return FPV_KEY_ENTER;
    case SDL_SCANCODE_ESCAPE:    return FPV_KEY_ESCAPE;
    case SDL_SCANCODE_SPACE:     return FPV_KEY_SPACE;
    case SDL_SCANCODE_LEFT:      return FPV_KEY_LEFT;
    case SDL_SCANCODE_RIGHT:     return FPV_KEY_RIGHT;
    case SDL_SCANCODE_UP:        return FPV_KEY_UP;
    case SDL_SCANCODE_DOWN:      return FPV_KEY_DOWN;
    case SDL_SCANCODE_PAGEUP:    return FPV_KEY_PAGE_UP;
    case SDL_SCANCODE_PAGEDOWN:  return FPV_KEY_PAGE_DOWN;
    case SDL_SCANCODE_HOME:      return FPV_KEY_HOME;
    case SDL_SCANCODE_END:       return FPV_KEY_END;
    case SDL_SCANCODE_INSERT:    return FPV_KEY_INSERT;
    case SDL_SCANCODE_DELETE:    return FPV_KEY_DELETE;
    case SDL_SCANCODE_BACKSPACE: return FPV_KEY_BACKSPACE;
    case SDL_SCANCODE_LSHIFT:    return FPV_KEY_LSHIFT;
    case SDL_SCANCODE_RSHIFT:    return FPV_KEY_RSHIFT;
    case SDL_SCANCODE_LCTRL:     return FPV_KEY_LCTRL;
    case SDL_SCANCODE_RCTRL:     return FPV_KEY_RCTRL;
    case SDL_SCANCODE_LALT:      return FPV_KEY_LALT;
    case SDL_SCANCODE_RALT:      return FPV_KEY_RALT;
    default:                     return FPV_KEY_NONE;
    }
}

bool FpvBridge::init(const MeshData& gym_mesh, Vec3 spawn_pos, const std::string& data_dir) {
    shutdown();
    spawn_pos_ = spawn_pos;

    fpv_global_config cfg{};
    fpv_global_config_defaults(&cfg);

    char shared_dir[1024] = {};
    std::string chosen_dir = data_dir;
    if (chosen_dir.empty()) {
        if (fpv_shared_data_dir(shared_dir, sizeof(shared_dir)) == FPV_OK) {
            chosen_dir = shared_dir;
        } else {
            chosen_dir = "libfpv_data";
        }
    }

    // Match canonical sandbox space: Z-up right-handed meters (+X right, +Z up, +Y forward)
    cfg.data_dir               = chosen_dir.c_str();
    cfg.coords.right           = FPV_AXIS_POS_X;
    cfg.coords.up              = FPV_AXIS_POS_Z;
    cfg.coords.forward         = FPV_AXIS_POS_Y;
    cfg.coords.units_per_meter = 1.0f;
    cfg.timing                 = FPV_TIMING_THREAD;
    cfg.flags                  = FPV_INIT_NO_DEVICE_INPUT | FPV_INIT_VIDEO_FEED;
    cfg.log                    = fpv_log_cb;
    cfg.log_level              = FPV_LOG_WARN;
    cfg.host_name              = "portable-mods Testing Sandbox";
    cfg.host_build             = "1.0.0";
    cfg.host_url               = "https://github.com/Kabosune/portable-mods";

    const fpv_result r = fpv_global_init(&cfg);
    if (r != FPV_OK && r != FPV_ERROR_ALREADY_INITIALIZED) {
        std::fprintf(stderr, "[fpv] fpv_global_init failed: %s\n", fpv_last_error());
        return false;
    }
    initialized_ = true;

    // Feed the exact same Testing Gym triangles into libfpv static collision
    fpv_mesh mesh{};
    mesh.positions      = gym_mesh.vertices.empty() ? nullptr : gym_mesh.vertices[0].pos;
    mesh.vertex_count   = static_cast<uint32_t>(gym_mesh.vertices.size());
    mesh.vertex_stride  = sizeof(GymVertex);
    mesh.indices        = gym_mesh.indices.data();
    mesh.triangle_count = static_cast<uint32_t>(gym_mesh.indices.size() / 3);
    mesh.material       = 0;
    if (fpv_static_surfaces_load(&mesh, 1) != FPV_OK) {
        std::fprintf(stderr, "[fpv] fpv_static_surfaces_load failed: %s\n", fpv_last_error());
    }

    // Enumerate shipped drone presets
    preset_names_.clear();
    const uint32_t pcount = fpv_preset_count();
    for (uint32_t i = 0; i < pcount; ++i) {
        const char* name = fpv_preset_name(i);
        if (name) preset_names_.emplace_back(name);
    }

    fpv_drone_desc desc{};
    fpv_drone_desc_defaults(&desc);
    desc.position[0]       = spawn_pos_.x;
    desc.position[1]       = spawn_pos_.y;
    desc.position[2]       = spawn_pos_.z;
    desc.pilot_position[0] = spawn_pos_.x - 6.0f;
    desc.pilot_position[1] = spawn_pos_.y - 4.0f;
    desc.pilot_position[2] = 0.0f;

    if (fpv_drone_create(&desc, &drone_) != FPV_OK) {
        std::fprintf(stderr, "[fpv] fpv_drone_create failed: %s\n", fpv_last_error());
        return false;
    }

    const float home[3] = {spawn_pos_.x, spawn_pos_.y, spawn_pos_.z};
    fpv_gps_set_home(home);

    // Optional SDL3 audio stream (gracefully continues if no playback device exists)
    fpv_audio_configure(48000, 2);
    fpv_audio_set_volume(audio_muted ? 0.0f : audio_volume);
    fpv_audio_set_beep_volume(audio_muted ? 0.0f : beep_volume);

    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdl_audio_callback, nullptr);
    if (audio_stream_) {
        SDL_ResumeAudioStreamDevice(audio_stream_);
    } else {
        SDL_Log("[fpv] Audio device unavailable (continuing silently): %s", SDL_GetError());
    }

    std::memset(&frame_, 0, sizeof(frame_));
    frame_.struct_size = sizeof(frame_);
    fpv_drone_tick(drone_, 0.0f, &frame_);
    return true;
}

void FpvBridge::init_gl_renderers() {
    if (gl_) return;
    gl_ = new GlState{};
    gl_ready_ = fpv_gl3_init(&gl_->ui) != 0;
    video_ready_ = fpv_video_gl3_init(&gl_->video) != 0;
    if (!gl_ready_) {
        std::fprintf(stderr, "[fpv] fpv_gl3_init failed\n");
    }
    if (!video_ready_) {
        std::fprintf(stderr, "[fpv] fpv_video_gl3_init failed (falling back to plain OSD)\n");
    }
}

void FpvBridge::shutdown() {
    if (audio_stream_) {
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
    }
    if (drone_) {
        fpv_drone_delete(drone_);
        drone_ = nullptr;
    }
    if (initialized_) {
        fpv_static_surfaces_clear();
        fpv_global_terminate();
        initialized_ = false;
    }
    if (gl_) {
        if (video_ready_) fpv_video_gl3_shutdown(&gl_->video);
        if (gl_ready_) fpv_gl3_shutdown(&gl_->ui);
        delete gl_;
        gl_ = nullptr;
        gl_ready_ = false;
        video_ready_ = false;
    }
}

void FpvBridge::reset_drone() {
    if (!drone_) return;
    fpv_drone_repair(drone_);
    const float pos[3] = {spawn_pos_.x, spawn_pos_.y, spawn_pos_.z};
    const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    fpv_drone_reset(drone_, pos, rot);
    ui_armed = false;
}

void FpvBridge::set_pilot_position(Vec3 feet_pos) {
    if (!drone_) return;
    const float p[3] = {feet_pos.x, feet_pos.y, feet_pos.z};
    fpv_drone_set_pilot_position(drone_, p);
}

void FpvBridge::set_active_mode(bool fpv_active, FpvCamView view) {
    if (!initialized_) return;
    if (!fpv_active) {
        fpv_set_view(FPV_VIEW_PILOT);
    } else {
        fpv_set_view(view == FpvCamView::Fpv ? FPV_VIEW_FPV
                   : view == FpvCamView::Chase ? FPV_VIEW_CHASE
                   : FPV_VIEW_LOS);
    }
}

void FpvBridge::handle_sdl_event(const SDL_Event& e, bool fpv_active) {
    if (!initialized_) return;
    const bool ov_open = fpv_overlay_is_open() != 0;
    if (!fpv_active && !ov_open) return;

    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const int k = sdl_scancode_to_fpv_key(e.key.scancode);
        if (k != FPV_KEY_NONE && !e.key.repeat) {
            fpv_input_key(k, e.type == SDL_EVENT_KEY_DOWN ? 1 : 0);
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (ov_open) fpv_overlay_text(e.text.text);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (ov_open) fpv_overlay_mouse_move(e.motion.x, e.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ov_open) {
            const int b = (e.button.button == SDL_BUTTON_LEFT) ? 0
                        : (e.button.button == SDL_BUTTON_RIGHT) ? 1 : 2;
            fpv_overlay_mouse_button(b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 0);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (ov_open) fpv_overlay_mouse_wheel(e.wheel.x, e.wheel.y);
        break;
    default:
        break;
    }
}

void FpvBridge::apply_host_controls(const InputFrame& in, bool fpv_active) {
    if (!initialized_ || !fpv_active) {
        return;
    }

    // Inject host channels when user armed via UI or when flying with a gamepad.
    // Otherwise let libfpv's built-in keyboard fallback (fed by fpv_input_key) drive flight.
    const bool gamepad_active = use_gamepad_rc && (
        std::fabs(in.left_stick_x) > 0.08f ||
        std::fabs(in.left_stick_y) > 0.08f ||
        std::fabs(in.look_stick_x) > 0.08f ||
        std::fabs(in.look_stick_y) > 0.08f ||
        in.gp_south || in.gp_east || in.gp_north);

    if (ui_armed || gamepad_active) {
        float ch[FPV_CHANNEL_COUNT] = {};
        // Mode 2 mapping: Aileron (roll) = Right X, Elevator (pitch) = Right Y,
        // Throttle = Left Y, Rudder (yaw) = Left X
        const float* keys = nullptr;
        (void)keys;
        const bool* kb = SDL_GetKeyboardState(nullptr);
        float roll  = std::clamp(in.look_stick_x, -1.0f, 1.0f);
        float pitch = std::clamp(in.look_stick_y, -1.0f, 1.0f);
        float thr   = std::clamp(in.left_stick_y, -1.0f, 1.0f);
        float yaw   = std::clamp(in.left_stick_x, -1.0f, 1.0f);

        if (!gamepad_active && kb) {
            // Simple keyboard flight assist while UI-armed: W/S throttle, A/D yaw, Arrow keys pitch/roll
            thr   = kb[SDL_SCANCODE_W] ? 0.25f : (kb[SDL_SCANCODE_S] ? -0.95f : -0.30f);
            yaw   = (kb[SDL_SCANCODE_D] ? 0.6f : 0.0f) - (kb[SDL_SCANCODE_A] ? 0.6f : 0.0f);
            pitch = (kb[SDL_SCANCODE_UP] ? 0.5f : 0.0f) - (kb[SDL_SCANCODE_DOWN] ? 0.5f : 0.0f);
            roll  = (kb[SDL_SCANCODE_RIGHT] ? 0.5f : 0.0f) - (kb[SDL_SCANCODE_LEFT] ? 0.5f : 0.0f);
        }

        ch[0] = roll;
        ch[1] = pitch;
        ch[2] = thr;
        ch[3] = yaw;
        ch[4] = (ui_armed || in.gp_south) ? 1.0f : -1.0f; // AUX1: ARM
        ch[5] = ui_angle_mode ? 1.0f : -1.0f;             // AUX2: ANGLE mode
        ch[6] = in.gp_east ? 1.0f : -1.0f;                // AUX3: BEEPER
        ch[7] = in.gp_north ? 1.0f : -1.0f;               // AUX4: TURTLE
        ch[8] = 1.0f;                                     // AUX5: AIR MODE
        ch[9] = show_osd ? -1.0f : 1.0f;                  // AUX6: OSD DISABLE
        fpv_input_set_channels(ch, FPV_CHANNEL_COUNT);
    } else {
        fpv_input_clear_channels();
    }
}

void FpvBridge::tick(float dt_sec) {
    if (!drone_) return;
    frame_.struct_size = sizeof(frame_);
    fpv_drone_tick(drone_, dt_sec, &frame_);
    for (int i = 0; i < 4; ++i) {
        prop_angles_[i] += (frame_.motors[i].rpm / 60.0) * (2.0 * kPi) * dt_sec * frame_.motors[i].spin_direction;
    }
}

void FpvBridge::render_fpv_video_and_osd(
    GLuint scene_tex, int scene_w, int scene_h, int fb_w, int fb_h, int win_w, int win_h,
    bool fpv_active, FpvCamView view) {
    if (!gl_ || !fpv_active) return;

    const bool osd_allowed = show_osd && !(frame_.flags & FPV_DRONE_OSD_DISABLED);
    if (view == FpvCamView::Fpv && video_ready_) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        fpv_video_gl3_draw(&gl_->video, &gl_->ui, &frame_, scene_tex, scene_w, scene_h, fb_w, fb_h, osd_allowed ? 1 : 0);
    } else if (view == FpvCamView::Fpv && gl_ready_ && osd_allowed) {
        fpv_gl3_render(&gl_->ui,
                       fpv_osd_draw_data(&frame_, static_cast<float>(win_w), static_cast<float>(win_h),
                                         static_cast<fpv_osd_style>(osd_style)),
                       fb_w, fb_h);
    }
}

void FpvBridge::render_libfpv_overlay(float dt_sec, int fb_w, int fb_h, int win_w, int win_h, float ui_scale) {
    if (!gl_ || !gl_ready_ || !initialized_) return;
    fpv_overlay_set_display(static_cast<float>(win_w), static_cast<float>(win_h), ui_scale > 0.0f ? ui_scale : 1.0f);
    fpv_gl3_render(&gl_->ui, fpv_overlay_render(dt_sec), fb_w, fb_h);
}

bool FpvBridge::overlay_is_open() const {
    return initialized_ && fpv_overlay_is_open() != 0;
}

bool FpvBridge::overlay_wants_input() const {
    return initialized_ && fpv_overlay_wants_input() != 0;
}

void FpvBridge::set_overlay_open(bool open) {
    if (initialized_) fpv_overlay_set_open(open ? 1 : 0);
}

void FpvBridge::set_audio_volume(float vol) {
    audio_volume = std::clamp(vol, 0.0f, 2.0f);
    if (initialized_) fpv_audio_set_volume(audio_muted ? 0.0f : audio_volume);
}

void FpvBridge::set_beep_volume(float vol) {
    beep_volume = std::clamp(vol, 0.0f, 2.0f);
    if (initialized_) fpv_audio_set_beep_volume(audio_muted ? 0.0f : beep_volume);
}

void FpvBridge::set_muted(bool muted) {
    audio_muted = muted;
    if (initialized_) {
        fpv_audio_set_volume(audio_muted ? 0.0f : audio_volume);
        fpv_audio_set_beep_volume(audio_muted ? 0.0f : beep_volume);
    }
}

void FpvBridge::apply_rates_scale(float scale) {
    rates_scale = std::clamp(scale, 0.25f, 2.0f);
    if (!initialized_) return;
    const int rate_val = std::clamp(static_cast<int>(std::lround(70.0f * rates_scale)), 20, 140);
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "set roll_rc_rate = %d\nset pitch_rc_rate = %d\nset yaw_rc_rate = %d\nsave\n",
                  rate_val, rate_val, rate_val);
    char* out = nullptr;
    const fpv_result r = fpv_cli_exec(cmd, &out);
    if (r == FPV_OK) {
        last_cli_status = "Rates applied via Betaflight CLI";
    } else {
        last_cli_status = std::string("CLI: ") + fpv_result_string(r);
    }
    if (out) fpv_free(out);
}

void FpvBridge::select_preset(int index) {
    if (!drone_ || index < 0 || index >= static_cast<int>(preset_names_.size())) return;
    if (fpv_drone_set_preset(drone_, preset_names_[index].c_str()) == FPV_OK) {
        current_preset_idx_ = index;
    }
}

DroneRenderParams FpvBridge::drone_render_params(bool visible) const {
    DroneRenderParams p{};
    p.visible = visible && (drone_ != nullptr);
    p.position = {frame_.position[0], frame_.position[1], frame_.position[2]};
    for (int i = 0; i < 4; ++i) {
        p.rotation_quat[i]  = frame_.rotation[i];
        p.motor_pos[i]      = {frame_.motors[i].position[0], frame_.motors[i].position[1], frame_.motors[i].position[2]};
        p.prop_angle_rad[i] = static_cast<float>(prop_angles_[i]);
        p.prop_broken[i]    = (frame_.motors[i].flags & FPV_MOTOR_PROP_BROKEN) != 0;
    }
    return p;
}

Mat4 FpvBridge::active_view_matrix(FpvCamView view, Vec3& out_eye, float& out_fovy, float& out_near) const {
    const fpv_camera& c = (view == FpvCamView::Fpv)   ? frame_.camera
                        : (view == FpvCamView::Chase) ? frame_.chase_camera
                                                      : frame_.los_camera;
    out_eye  = {c.position[0], c.position[1], c.position[2]};
    out_fovy = c.fov_y_degrees > 10.0f ? c.fov_y_degrees : 95.0f;
    out_near = std::max(0.02f, c.near_plane);

    Vec3 r{c.right[0],   c.right[1],   c.right[2]};
    Vec3 u{c.up[0],      c.up[1],      c.up[2]};
    Vec3 f{c.forward[0], c.forward[1], c.forward[2]};

    // Apply optional extra sandbox camera tilt in FPV view for quick testing
    if (view == FpvCamView::Fpv && std::fabs(camera_tilt_deg) > 0.01f) {
        const float tilt_rad = (camera_tilt_deg - 15.0f) * kDegToRad;
        const float ct = std::cos(tilt_rad);
        const float st = std::sin(tilt_rad);
        const Vec3 nf = normalize(f * ct + u * st);
        const Vec3 nu = normalize(cross(r, nf));
        f = nf;
        u = nu;
    }

    return mat4_view_from_basis(out_eye, r, u, f);
}

} // namespace sb
