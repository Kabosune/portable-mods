#include "sandbox_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sb {

bool SandboxApp::init(const std::string& fpv_data_dir) {
    gym = GymMap::build();
    input.init();

    // Initialize quake-movement
    set_qm_preset(QM_PRESET_QUAKE_VQ3);

    // Initialize weighty-aim
    wa_player_init(&wa_player);
    wa_select_curve(&wa_player.stick_cfg, WA_CURVE_BALANCED);

    // Initialize free-aim (start on Weighty preset so dynamic reticle & weapon spring are live immediately)
    fa_profile_bank_init(&fa_bank);
    fa_profile_apply_preset(&fa_bank, FA_PRESET_WEIGHTY);
    fa_reset_state(&fa_state);
    fa_out = {};
    fa_out.show_reticle = fa_bank.cfg.reticle.show_hip;

    // Initialize Jolt Physics world from unified gym mesh
    const TeleportPoint spawn = GymMap::get_teleport(TeleportId::HubSpawn);
    if (!physics.init(gym.static_mesh, spawn.position)) {
        return false;
    }
    teleport(TeleportId::HubSpawn);

    // Initialize libfpv with exact same gym mesh
    if (!fpv.init(gym.static_mesh, gym.drone_spawn, fpv_data_dir)) {
        SDL_Log("[sandbox] Warning: libfpv failed to initialize; FPV mode will be unavailable");
    }
    fpv.set_pilot_position(player_char.feet_pos);
    fpv.set_active_mode(false, fpv_cam_view);
    return true;
}

void SandboxApp::init_gl() {
    renderer.init(gym.static_mesh);
    fpv.init_gl_renderers();
}

void SandboxApp::shutdown() {
    toggle_csv_logging(false);
    fpv.shutdown();
    renderer.shutdown();
    physics.shutdown();
    input.shutdown();
}

void SandboxApp::set_qm_preset(qm_preset_id_t preset) {
    qm_preset = preset;
    qm_preset_defaults(preset, &qm_params);
    const float prev_pos[3] = {qm_state.pos[0], qm_state.pos[1], qm_state.pos[2]};
    qm_state_init(&qm_state, &qm_params);
    qm_state.pos[0] = prev_pos[0];
    qm_state.pos[1] = prev_pos[1];
    qm_state.pos[2] = prev_pos[2];
}

void SandboxApp::teleport(TeleportId id) {
    last_teleport = id;
    const TeleportPoint tp = GymMap::get_teleport(id);
    physics.teleport_player(tp.position);
    player_char = physics.query_character_state();

    qm_state_init(&qm_state, &qm_params);
    qm_state.pos[0] = player_char.feet_pos.x;
    qm_state.pos[1] = player_char.feet_pos.y;
    qm_state.pos[2] = player_char.feet_pos.z;

    cam_yaw_rad    = tp.yaw_rad;
    cam_pitch_rad  = tp.pitch_rad;
    smoothed_eye_h = player_char.eye_height;
    fa_reset_state(&fa_state);
    fpv.set_pilot_position(player_char.feet_pos);
}

void SandboxApp::reset_player() {
    teleport(last_teleport);
}

void SandboxApp::toggle_fpv_mode() {
    if (!fpv.is_ready()) return;
    fpv_mode = !fpv_mode;
    edge_latch.clear();
    if (!fpv_mode) {
        fpv.release_all_keys();
    }
    fpv.set_pilot_position(player_char.feet_pos);
    fpv.set_active_mode(fpv_mode, fpv_cam_view);
    if (fpv_mode) {
        // Freeze FPS locomotion timers cleanly on transition
        qm_state.vel[0] = qm_state.vel[1] = qm_state.vel[2] = 0.0f;
        qm_state.sliding = 0;
        qm_state.dash_timer = 0.0f;
    }
}

void SandboxApp::toggle_csv_logging(bool enable) {
    if (enable && !fa_csv_file) {
        fa_csv_path = "telemetry_free_aim.csv";
        fa_csv_file = std::fopen(fa_csv_path.c_str(), "w");
        if (fa_csv_file) {
            char hdr[512];
            if (fa_format_telemetry_csv_header(hdr, sizeof(hdr)) > 0) {
                std::fprintf(fa_csv_file, "%s\n", hdr);
                std::fflush(fa_csv_file);
            }
            fa_csv_logging = true;
        }
    } else if (!enable && fa_csv_file) {
        std::fclose(fa_csv_file);
        fa_csv_file = nullptr;
        fa_csv_logging = false;
    }
}

void SandboxApp::step_fps_aim(const InputFrame& in, float dt_sec, bool active_gameplay) {
    const float raw_stick_mag = std::hypot(in.look_stick_x, in.look_stick_y);
    last_look_stick_mag = raw_stick_mag;

    if (!active_gameplay) {
        // Keep gyro calibration ticking while Tuning Panel is open (menu tick)
        wa_gyro_menu_tick(&wa_player.gyro_cfg, &wa_player.gyro_state, &in.sensor, dt_sec);
        return;
    }

    const bool aiming_now = ads_toggle_mode ? ads_toggled_on : in.ads_held;

    // 1. weighty-aim stick response & turn boost
    float stick_rate[2] = {0.0f, 0.0f};
    wa_process_stick(
        &wa_player.stick_cfg,
        &wa_player.stick_state,
        in.look_stick_x,
        in.look_stick_y,
        dt_sec,
        wa_force_original_aim ? 1 : 0,
        stick_rate);

    // Optional repeatable scripted look pattern for A/B free-aim tuning
    float stick_deg[2] = {
        stick_rate[0] * full_stick_deg_per_sec * dt_sec,
        stick_rate[1] * full_stick_deg_per_sec * dt_sec,
    };
    float stick_defl = raw_stick_mag;
    if (fa_scripted_look) {
        const float s = fa_scripted_input_sample(static_cast<float>(sim_time_sec));
        stick_deg[0] = s * full_stick_deg_per_sec * dt_sec;
        stick_deg[1] = 0.0f;
        stick_defl   = std::fabs(s);
    }

    // 2. weighty-aim GyroWiki gyro pipeline (degrees/frame directly; never passes through stick curve)
    const int stick_moving = (raw_stick_mag > 0.15f) ? 1 : 0;
    wa_process_gyro(
        &wa_player.gyro_cfg,
        &wa_player.gyro_state,
        &in.sensor,
        aiming_now ? 1 : 0,
        stick_moving,
        dt_sec,
        wa_force_original_aim ? 1 : 0,
        last_gyro_out_deg);

    // 3. Raw mouse delta -> degrees (+yaw right, +pitch up; SDL mouse_dy is +down so negate)
    const float mouse_deg[2] = {
        in.mouse_dx * mouse_sens_deg_per_px,
       -in.mouse_dy * mouse_sens_deg_per_px,
    };

    // 4. Current world FOV (zoomed smoothly during ADS)
    const float ads_zoom = std::clamp(fa_bank.cfg.ads_zoom, 1.0f, 3.0f);
    const float ads_prev = fa_out.ads_amount;
    current_world_fovy = base_world_fovy_deg / (1.0f + (ads_zoom - 1.0f) * ads_prev);

    fa_input_t fin{};
    fin.stick_deg[0]           = stick_deg[0];
    fin.stick_deg[1]           = stick_deg[1];
    fin.mouse_deg[0]           = mouse_deg[0];
    fin.mouse_deg[1]           = mouse_deg[1];
    fin.gyro_deg[0]            = last_gyro_out_deg[0];
    fin.gyro_deg[1]            = last_gyro_out_deg[1];
    fin.stick_deflection       = stick_defl;
    fin.full_stick_deg_per_sec = full_stick_deg_per_sec;
    fin.dt_sec                 = dt_sec;
    fin.aiming                 = aiming_now ? 1 : 0;
    fin.gameplay_active        = 1;
    fin.move_fraction          = std::clamp(qm_state.horiz_speed / std::max(1.0f, qm_params.max_speed), 0.0f, 1.0f);
    fin.fov_scale              = fa_fov_scale(current_world_fovy, base_world_fovy_deg);

    fa_step(&fa_bank.cfg, &fa_state, &fin, fa_force_original_aim ? 1 : 0, &fa_out);

    // Update ADS-zoomed world FOV with freshly computed ads_amount
    current_world_fovy = base_world_fovy_deg / (1.0f + (ads_zoom - 1.0f) * fa_out.ads_amount);

    // Bridge angle signs: free_aim +yaw = right, qm_physics +cam_yaw_rad = left (CCW around +Z)
    cam_yaw_rad   -= fa_out.cam_delta_deg[0] * kDegToRad;
    cam_pitch_rad += fa_out.cam_delta_deg[1] * kDegToRad;
    cam_pitch_rad  = std::clamp(cam_pitch_rad, -89.0f * kDegToRad, 89.0f * kDegToRad);

    // Optional CSV telemetry logging
    if (fa_csv_logging && fa_csv_file) {
        const float in_deg[2] = {
            stick_deg[0] + mouse_deg[0] + last_gyro_out_deg[0],
            stick_deg[1] + mouse_deg[1] + last_gyro_out_deg[1],
        };
        const float cam_deg[2] = {-cam_yaw_rad * kRadToDeg, cam_pitch_rad * kRadToDeg};
        char row[512];
        if (fa_format_telemetry_csv_row(
                row, sizeof(row), static_cast<float>(sim_time_sec), dt_sec,
                &fa_bank.cfg, &fa_state, in_deg, &fa_out, cam_deg, current_world_fovy) > 0) {
            std::fprintf(fa_csv_file, "%s\n", row);
        }
    }
}

void SandboxApp::step_fps_physics(const InputFrame& in, float dt_sec) {
    // Sync solver position with Jolt CharacterVirtual
    qm_state.pos[0] = player_char.feet_pos.x;
    qm_state.pos[1] = player_char.feet_pos.y;
    qm_state.pos[2] = player_char.feet_pos.z;

    qm_input_t qin{};
    qin.dt_sec          = dt_sec;
    qin.wish_forward    = in.move_forward;
    qin.wish_right      = in.move_right;
    qin.cam_yaw_rad     = cam_yaw_rad;
    qin.jump_held       = in.jump_held ? 1 : 0;
    qin.jump_pressed    = in.jump_pressed ? 1 : 0;
    // Keep crouch locomotion consistent while physically trapped under a low tunnel ceiling
    qin.crouch_held     = (in.crouch_held || player_char.crouching) ? 1 : 0;
    qin.crouch_pressed  = in.crouch_pressed ? 1 : 0;
    qin.dash_pressed    = in.dash_pressed ? 1 : 0;
    qin.on_ground       = player_char.on_ground ? 1 : 0;
    qin.slope_normal[0] = player_char.slope_normal.x;
    qin.slope_normal[1] = player_char.slope_normal.y;
    qin.slope_normal[2] = player_char.slope_normal.z;
    qin.wall_probe      = &PhysicsWorld::qm_wall_probe_callback;
    qin.wall_probe_ctx  = &physics;

    qm_step(&qm_params, &qin, &qm_state, &qm_state);

    // Apply optional adapter-level speed_scale without mutating qm_params presets
    const float sc = std::clamp(qm_speed_scale, 0.25f, 3.0f);
    const Vec3 desired_vel = {
        qm_state.vel[0] * sc,
        qm_state.vel[1] * sc,
        qm_state.vel[2],
    };

    const bool want_crouch = in.crouch_held || (qm_state.sliding != 0);
    const bool jumping_up  = (qm_state.jumped != 0) || (qm_state.wall_bounced != 0);
    player_char = physics.step_character(desired_vel, dt_sec, want_crouch, jumping_up);

    // Sync clipped velocity back from Jolt (unscaled horizontally) so hitting walls kills into-wall velocity
    qm_state.pos[0] = player_char.feet_pos.x;
    qm_state.pos[1] = player_char.feet_pos.y;
    qm_state.pos[2] = player_char.feet_pos.z;
    qm_state.vel[0] = player_char.velocity.x / sc;
    qm_state.vel[1] = player_char.velocity.y / sc;
    qm_state.vel[2] = player_char.velocity.z;
    qm_state.horiz_speed = qm_horiz_speed(qm_state.vel);
    if (qm_state.horiz_speed > qm_state.peak_speed) {
        qm_state.peak_speed = qm_state.horiz_speed;
    }

    // Smooth camera eye height transition during crouch/slide
    const float eye_alpha = 1.0f - std::exp(-dt_sec * 16.0f);
    smoothed_eye_h += (player_char.eye_height - smoothed_eye_h) * eye_alpha;

    fpv.set_pilot_position(player_char.feet_pos);
}

void SandboxApp::fire_hitscan() {
    const Vec3 eye = {
        player_char.feet_pos.x,
        player_char.feet_pos.y,
        player_char.feet_pos.z + smoothed_eye_h,
    };
    const CameraBasis basis = camera_basis_from_yaw_pitch(cam_yaw_rad, cam_pitch_rad);
    const float cf[3] = {basis.forward.x, basis.forward.y, basis.forward.z};
    const float cr[3] = {basis.right.x,   basis.right.y,   basis.right.z};
    const float cu[3] = {basis.up.x,      basis.up.y,      basis.up.z};

    float dir_arr[3] = {};
    fa_compute_shot_dir(cf, cr, cu, fa_out.reticle_deg, dir_arr);
    const Vec3 shot_dir{dir_arr[0], dir_arr[1], dir_arr[2]};

    const float max_dist = 180.0f;
    const RaycastHit hit = physics.raycast_hitscan(eye, shot_dir, max_dist, gym.targets);
    const Vec3 end_pt = hit.hit ? hit.point : (eye + shot_dir * max_dist);

    // Start tracer slightly below/right of eye so the beam visibly tracks toward the reticle impact point
    const Vec3 muzzle_world = eye + basis.forward * 0.45f + basis.right * (0.14f * (1.0f - fa_out.ads_amount)) - basis.up * 0.10f;
    tracers.push_back({muzzle_world, end_pt, hit.target_id >= 0, 0.14f, 0.14f});
    recoil_kick = 1.0f;
    fire_cooldown = 0.12f;

    if (hit.target_id >= 0) {
        hit_marker_timer = 0.22f;
        hit_marker_was_target = true;
        for (auto& t : gym.targets) {
            if (t.id == hit.target_id) {
                t.hit_flash_timer = 0.22f;
                t.hit_count++;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "HIT: %s (%.1fm, hits=%d)", t.label.c_str(), hit.distance, t.hit_count);
                last_hit_text = buf;
                break;
            }
        }
    } else if (hit.hit) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "World hit (%.1fm)", hit.distance);
        last_hit_text = buf;
    }
}

void SandboxApp::step_frame(float real_dt_sec, bool window_focused) {
    const float dt = std::clamp(real_dt_sec, 0.0001f, 0.1f);
    sim_time_sec += dt;
    if (real_dt_sec > 0.0001f) {
        fps_smoothed = fps_smoothed * 0.92f + (1.0f / real_dt_sec) * 0.08f;
    }

    const bool mouse_captured = window_focused && !ui_open && !fpv.overlay_wants_input();
    InputFrame in = input.sample(mouse_captured);
    input.consume_mouse_delta();

    // Decay visual timers
    recoil_kick      = std::max(0.0f, recoil_kick - dt * 7.5f);
    fire_cooldown    = std::max(0.0f, fire_cooldown - dt);
    hit_marker_timer = std::max(0.0f, hit_marker_timer - dt);
    for (auto& tr : tracers) tr.time_left -= dt;
    tracers.erase(std::remove_if(tracers.begin(), tracers.end(),
                                 [](const TracerBeam& b) { return b.time_left <= 0.0f; }),
                  tracers.end());

    // Update moving Aiming Range targets continuously
    gym.update_targets(static_cast<float>(sim_time_sec), dt);

    if (!fpv_mode) {
        const bool active_fps = mouse_captured;

        // Aim runs once per rendered frame with exact dt for smooth 1:1 mouse/gyro/spring response
        step_fps_aim(in, dt, active_fps);

        // Fire hitscan shot
        if (active_fps && in.fire_pressed && fire_cooldown <= 0.0f) {
            fire_hitscan();
        }

        // Latch rising-edge presses so 0-substep frames (when render FPS > physics tick Hz) never drop inputs
        if (active_fps) {
            edge_latch.accumulate(in.jump_pressed, in.crouch_pressed, in.dash_pressed);
        } else {
            edge_latch.clear();
        }

        // Fixed-timestep movement physics (60 / 120 / 240 Hz)
        const float fixed_dt = 1.0f / static_cast<float>(std::clamp(physics_tick_hz, 60, 240));
        sim_accumulator = std::min(sim_accumulator + dt, fixed_dt * 8.0f);
        while (sim_accumulator >= fixed_dt) {
            InputFrame sub_in = in;
            if (active_fps) {
                sub_in.jump_pressed   = edge_latch.jump_pressed;
                sub_in.crouch_pressed = edge_latch.crouch_pressed;
                sub_in.dash_pressed   = edge_latch.dash_pressed;
                edge_latch.clear();
            } else {
                sub_in.move_forward = sub_in.move_right = 0.0f;
                sub_in.jump_held = sub_in.jump_pressed = false;
                sub_in.crouch_held = sub_in.crouch_pressed = false;
                sub_in.dash_pressed = false;
            }
            step_fps_physics(sub_in, fixed_dt);
            sim_accumulator -= fixed_dt;
        }
    } else {
        edge_latch.clear();
    }

    // Update FPV host channel / keyboard arm pulse every frame
    fpv.apply_host_controls(in, fpv_mode && !ui_open && !fpv.overlay_wants_input(), input.has_gamepad());

    // Tick libfpv every frame so drone state/audio/preview stay live even when standing beside it in FPS mode
    fpv.set_active_mode(fpv_mode, fpv_cam_view);
    fpv.tick(dt);
}

void SandboxApp::render(int fb_w, int fb_h, int win_w, int win_h, float ui_scale, float frame_dt) {
    if (fb_w <= 0 || fb_h <= 0) return;

    renderer.ensure_fbo(fb_w, fb_h);
    const float aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);

    Mat4 view{};
    Mat4 proj{};
    Vec3 eye{};

    if (!fpv_mode) {
        eye = {
            player_char.feet_pos.x,
            player_char.feet_pos.y,
            player_char.feet_pos.z + smoothed_eye_h,
        };
        const CameraBasis basis = camera_basis_from_yaw_pitch(cam_yaw_rad, cam_pitch_rad);
        view = mat4_view_from_basis(eye, basis.right, basis.up, basis.forward);
        proj = mat4_perspective(current_world_fovy, aspect, 0.08f, 450.0f);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        renderer.render_world(view, proj, eye, gym.targets, tracers, fpv.drone_render_params(true), fb_w, fb_h);

        // Separate depth-cleared weapon viewmodel pass using independent viewmodel FOV
        const float barrel_offset[3] = {0.19f, -0.135f, 0.0f};
        float pos_off[3] = {0, 0, 0};
        float rot_deg[2] = {0, 0};
        fa_compute_viewmodel_offset(
            fa_out.reticle_deg,
            fa_out.ads_amount,
            fa_bank.cfg.ads_height * 0.01f,
            barrel_offset,
            0.55f,
            pos_off,
            rot_deg);

        ViewmodelRenderParams vm{};
        vm.pos_offset_cam     = {pos_off[0], pos_off[1], pos_off[2]};
        vm.rot_yaw_deg        = rot_deg[0];
        vm.rot_pitch_deg      = rot_deg[1];
        vm.ads_amount         = fa_out.ads_amount;
        vm.recoil_kick        = recoil_kick;
        vm.viewmodel_fovy_deg = viewmodel_fovy_deg;
        renderer.render_viewmodel(vm, fb_w, fb_h);
    } else {
        float fpv_fovy = 95.0f;
        float fpv_near = 0.03f;
        view = fpv.active_view_matrix(fpv_cam_view, eye, fpv_fovy, fpv_near);
        proj = mat4_perspective(fpv_fovy, aspect, fpv_near, 450.0f);

        const bool draw_drone = (fpv_cam_view != FpvCamView::Fpv);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer.fbo_handle());
        renderer.render_world(view, proj, eye, gym.targets, tracers, fpv.drone_render_params(draw_drone), fb_w, fb_h);

        if (fpv_cam_view == FpvCamView::Fpv) {
            fpv.render_fpv_video_and_osd(renderer.fbo_color_tex(), fb_w, fb_h, fb_w, fb_h, win_w, win_h, true, fpv_cam_view);
        } else {
            renderer.blit_fbo_to_screen(fb_w, fb_h);
        }
    }

    // Draw libfpv's own F8 overlay (no-op when closed)
    fpv.render_libfpv_overlay(frame_dt, fb_w, fb_h, win_w, win_h, ui_scale);
}

} // namespace sb
