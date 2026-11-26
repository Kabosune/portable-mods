/*
 * test_free_aim.c - Standalone pure-C11 unit tests for free_aim.
 */

#include "free_aim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, (msg)); \
            g_failures++;                                                     \
        }                                                                     \
    } while (0)

static int approx(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static fa_input_t make_base_input(float dt_sec) {
    fa_input_t in;
    memset(&in, 0, sizeof(in));
    in.dt_sec = dt_sec;
    in.gameplay_active = 1;
    in.fov_scale = 1.0f;
    in.full_stick_deg_per_sec = 210.0f;
    return in;
}

static void test_presets_and_force_original(void) {
    fa_profile_bank_t bank;
    fa_profile_bank_init(&bank);

    /* Default preset is Classic (mod off) */
    CHECK(bank.cfg.preset == FA_PRESET_CLASSIC, "Default preset is Classic");
    CHECK(!fa_preset_is_custom(FA_PRESET_CLASSIC), "Classic is not custom");
    CHECK(!fa_preset_persists_edits(FA_PRESET_WEIGHTY), "Built-in presets do not persist edits");
    CHECK(fa_preset_persists_edits(FA_PRESET_CUSTOM1), "Custom 1 persists edits");
    CHECK(fa_preset_persists_edits(FA_PRESET_CUSTOM2), "Custom 2 persists edits");
    CHECK(fa_preset_persists_edits(FA_PRESET_CUSTOM3), "Custom 3 persists edits");

    /* Verify built-in preset values */
    fa_cfg_t cfg;
    fa_preset_defaults(FA_PRESET_WEIGHTY, &cfg);
    CHECK(approx(cfg.zone_x, 7.5f, 1e-4f) && approx(cfg.zone_y, 4.5f, 1e-4f), "Weighty zone 7.5/4.5");
    CHECK(approx(cfg.camera_share, 0.5f, 1e-4f) && approx(cfg.camera_lead, 0.8f, 1e-4f), "Weighty share/lead");
    CHECK(approx(cfg.recenter_speed, 1.0f, 1e-4f) && approx(cfg.recenter_delay, 0.25f, 1e-4f) && approx(cfg.recenter_smooth, 0.4f, 1e-4f), "Weighty catch-up");
    CHECK(approx(cfg.gun_response, 8.5f, 1e-4f) && approx(cfg.gun_damping, 0.7f, 1e-4f) && approx(cfg.turn_drag, 0.35f, 1e-4f), "Weighty spring/drag");
    CHECK(approx(cfg.edge_smoothing, 0.1f, 1e-4f) && approx(cfg.camera_sway, 0.08f, 1e-4f) && approx(cfg.walk_sway, 0.25f, 1e-4f), "Weighty edge/sway");
    CHECK(approx(cfg.ads_zoom, 1.3f, 1e-4f) && approx(cfg.ads_time, 0.18f, 1e-4f) && approx(cfg.ads_sway, 0.35f, 1e-4f) && approx(cfg.ads_sens, 0.65f, 1e-4f), "Weighty ADS");

    fa_preset_defaults(FA_PRESET_IMMERSIVE, &cfg);
    CHECK(approx(cfg.zone_x, 11.0f, 1e-4f) && approx(cfg.zone_y, 6.5f, 1e-4f), "Immersive zone 11/6.5");
    CHECK(approx(cfg.gun_response, 4.6f, 1e-4f) && approx(cfg.gun_damping, 0.52f, 1e-4f) && approx(cfg.turn_drag, 0.6f, 1e-4f), "Immersive spring/drag");
    CHECK(cfg.reticle.show_ads == 0 && approx(cfg.ads_time, 0.26f, 1e-4f), "Immersive no reticle while aiming & ads_time 0.26");

    fa_preset_defaults(FA_PRESET_ARCADE, &cfg);
    CHECK(approx(cfg.zone_x, 28.0f, 1e-4f) && approx(cfg.zone_y, 18.0f, 1e-4f), "Arcade zone 28/18");
    CHECK(approx(cfg.camera_share, 0.0f, 1e-4f) && approx(cfg.camera_lead, 0.0f, 1e-4f) && approx(cfg.recenter_speed, 0.0f, 1e-4f), "Arcade zero share/lead/recenter");
    CHECK(approx(cfg.edge_turn_speed, 120.0f, 1e-4f) && approx(cfg.edge_band, 0.3f, 1e-4f) && approx(cfg.edge_influence, 0.8f, 1e-4f), "Arcade Edge Auto-Turn");
    CHECK(cfg.reticle_profile == 1 && cfg.reticle.smooth_reticle == 1 && cfg.reticle.size > 2.5f, "Arcade own reticle profile");

    fa_preset_defaults(FA_PRESET_BORING, &cfg);
    CHECK(approx(cfg.zone_x, 0.0f, 1e-4f) && approx(cfg.zone_y, 0.0f, 1e-4f) && approx(cfg.camera_share, 1.0f, 1e-4f), "Boring centered FPS");
    CHECK(approx(cfg.gun_response, 20.0f, 1e-4f) && approx(cfg.gun_damping, 1.0f, 1e-4f) && approx(cfg.turn_drag, 0.0f, 1e-4f) && approx(cfg.camera_sway, 0.0f, 1e-4f), "Boring spring/no sway");

    /* Custom 1-3 copy from starting preset & isolate edits; built-in presets refresh on load */
    fa_profile_apply_preset(&bank, FA_PRESET_ARCADE);
    CHECK(bank.reticle_live_profile == 1 && bank.cfg.reticle.smooth_reticle == 1, "Arcade switches to reticle profile 1");
    bank.cfg.zone_x = 22.0f;
    fa_profile_settings_changed(&bank);
    CHECK(bank.cfg.preset == FA_PRESET_CUSTOM1, "Editing built-in preset switches to Custom 1");
    CHECK(approx(bank.custom[0].zone_x, 22.0f, 1e-4f) && bank.custom[0].reticle_profile == 1, "Custom 1 keeps copied Arcade values and edit");

    fa_profile_apply_preset(&bank, FA_PRESET_WEIGHTY);
    CHECK(bank.reticle_live_profile == 0 && bank.cfg.reticle.smooth_reticle == 0, "Switching to Weighty restores normal reticle profile");
    bank.cfg.zone_x = 99.0f; /* simulate stale ini value on built-in preset */
    fa_profile_on_load_refresh_builtin(&bank);
    CHECK(approx(bank.cfg.zone_x, 7.5f, 1e-4f), "Built-in preset refreshes on load");

    fa_profile_apply_preset(&bank, FA_PRESET_CUSTOM1);
    fa_profile_on_load_refresh_builtin(&bank);
    CHECK(approx(bank.cfg.zone_x, 22.0f, 1e-4f), "Custom 1 preserves edits across load refresh");

    /* Force Original Aim bypasses everything without mutating cfg */
    fa_cfg_t saved_cfg;
    fa_preset_defaults(FA_PRESET_IMMERSIVE, &saved_cfg);
    fa_cfg_t snapshot = saved_cfg;
    fa_state_t st;
    fa_reset_state(&st);

    fa_input_t in = make_base_input(1.0f / 60.0f);
    in.mouse_deg[0] = 3.5f;
    in.mouse_deg[1] = -1.25f;
    in.aiming = 1;
    fa_output_t out;

    fa_step(&saved_cfg, &st, &in, 1, &out);
    CHECK(out.active == 0, "Force Original Aim marks active = 0");
    CHECK(approx(out.cam_delta_deg[0], 3.5f, 1e-5f) && approx(out.cam_delta_deg[1], -1.25f, 1e-5f), "Force Original Aim passes 1:1 unmodded camera delta");
    CHECK(approx(out.reticle_deg[0], 0.0f, 1e-5f) && approx(out.reticle_deg[1], 0.0f, 1e-5f), "Force Original Aim keeps reticle at zero");
    CHECK(memcmp(&saved_cfg, &snapshot, sizeof(saved_cfg)) == 0, "Force Original Aim does not mutate saved settings");
}

static void test_ellipse_conservation_drag_and_edge_overflow(void) {
    fa_cfg_t cfg;
    fa_preset_defaults(FA_PRESET_WEIGHTY, &cfg);
    cfg.zone_x = 10.0f;
    cfg.zone_y = 5.0f;
    cfg.camera_share = 0.4f;
    cfg.camera_lead = 0.0f;
    cfg.recenter_speed = 0.0f;
    cfg.camera_sway = 0.0f;
    cfg.walk_sway = 0.0f;
    cfg.turn_drag = 0.5f;
    cfg.edge_smoothing = 0.0f; /* rigid first for exact 1:1 check */
    cfg.reticle_speed = 1.0f;

    fa_state_t st;
    fa_reset_state(&st);
    fa_input_t in = make_base_input(1.0f / 60.0f);
    fa_output_t out;

    /* Inside ellipse: camera_delta + target == input */
    in.mouse_deg[0] = 6.0f;
    in.mouse_deg[1] = 2.0f;
    fa_step(&cfg, &st, &in, 0, &out);
    CHECK(approx(out.cam_delta_deg[0], 2.4f, 1e-4f), "Camera share 0.4 takes 40% of yaw");
    CHECK(approx(st.target[0], 3.6f, 1e-4f), "Remaining 60% moves target yaw");
    CHECK(approx(out.cam_delta_deg[0] + st.target[0], 6.0f, 1e-4f), "1:1 yaw conservation inside zone");
    CHECK(approx(out.cam_delta_deg[1] + st.target[1], 2.0f, 1e-4f), "1:1 pitch conservation inside zone");
    /* Turn drag pulls display behind camera turn */
    CHECK(st.display[0] < st.target[0], "Turn drag makes weapon display lag behind target");

    /* Push past ellipse edge with rigid smoothing: total camera + target == cumulative input */
    float total_in_x = 6.0f, total_in_y = 2.0f;
    float total_cam_x = out.cam_delta_deg[0], total_cam_y = out.cam_delta_deg[1];
    in.mouse_deg[0] = 20.0f;
    in.mouse_deg[1] = 10.0f;
    fa_step(&cfg, &st, &in, 0, &out);
    total_in_x += 20.0f;
    total_in_y += 10.0f;
    total_cam_x += out.cam_delta_deg[0];
    total_cam_y += out.cam_delta_deg[1];

    float ell = (st.target[0] / 10.0f) * (st.target[0] / 10.0f)
              + (st.target[1] / 5.0f) * (st.target[1] / 5.0f);
    CHECK(ell <= 1.001f, "Target stays clamped on ellipse boundary");
    CHECK(approx(total_cam_x + st.target[0], total_in_x, 1e-3f), "1:1 yaw conservation across edge overflow");
    CHECK(approx(total_cam_y + st.target[1], total_in_y, 1e-3f), "1:1 pitch conservation across edge overflow");

    /* Edge smoothing + 3x soft_cap hard cap flush */
    fa_reset_state(&st);
    cfg.edge_smoothing = 0.5f;
    cfg.camera_share = 0.0f;
    in.dt_sec = 1.0f / 240.0f;
    in.mouse_deg[0] = 25.0f; /* 15 deg overflow past zx=10, exceeds 3*soft_cap=11.25 */
    in.mouse_deg[1] = 0.0f;
    fa_step(&cfg, &st, &in, 0, &out);

    float soft_cap = 0.25f * (10.0f + 5.0f); /* 3.75 deg */
    float hard_cap = 3.0f * soft_cap;        /* 11.25 deg */
    float over_len = hypotf(st.over[0], st.over[1]);
    CHECK(approx(over_len, hard_cap, 1e-3f), "Overflow hard-capped at 3 * soft_cap");
    CHECK(over_len > 0.0f, "Edge smoothing retains bounded overflow for subsequent frames");
    CHECK(approx(out.cam_delta_deg[0] + st.target[0] + st.over[0], 25.0f, 1e-3f),
          "1:1 conservation holds with smoothed overflow in flight");

    /* Let overflow drain with zero input and verify leftover flush */
    in.dt_sec = 1.0f / 60.0f;
    in.mouse_deg[0] = 0.0f;
    for (int f = 0; f < 240; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
    }
    CHECK(approx(st.over[0], 0.0f, 1e-5f) && approx(st.over[1], 0.0f, 1e-5f),
          "Edge smoothing drains and flushes tiny leftovers to zero");
}

static void test_edge_auto_turn_and_input_gating(void) {
    fa_cfg_t cfg;
    fa_preset_defaults(FA_PRESET_ARCADE, &cfg);
    cfg.camera_sway = 0.0f;
    cfg.walk_sway = 0.0f;
    cfg.edge_smoothing = 0.0f;

    fa_state_t st;
    fa_reset_state(&st);
    fa_output_t out;
    float dt = 1.0f / 60.0f;

    /* Move reticle to outer edge with mouse -> last_input becomes FA_INPUT_MOUSE */
    fa_input_t in = make_base_input(dt);
    in.mouse_deg[0] = 28.0f;
    fa_step(&cfg, &st, &in, 0, &out);
    CHECK(st.last_input == FA_INPUT_MOUSE, "Mouse input claims last_input");

    /* Small stick drift (< 25% deflection) must NOT steal last_input */
    in.mouse_deg[0] = 0.0f;
    in.stick_deg[0] = 0.1f;
    in.stick_deflection = 0.20f;
    fa_step(&cfg, &st, &in, 0, &out);
    CHECK(st.last_input == FA_INPUT_MOUSE, "Stick below 25% deflection does not steal last_input");

    /* During 0.15s dwell, Edge Auto-Turn has not started yet */
    in.stick_deg[0] = 0.0f;
    in.stick_deflection = 0.0f;
    int dwell_frames = 6; /* 0.10s + prior 2 frames = 0.133s < 0.15s */
    for (int f = 0; f < dwell_frames; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
        CHECK(st.edge_turn_active == 0, "Edge Auto-Turn waits for 0.15s dwell");
    }

    /* Advance past dwell + ease-in: camera turns steadily right at ~120 deg/s */
    for (int f = 0; f < 30; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
    }
    CHECK(st.edge_turn_active == 1, "Edge Auto-Turn engages after dwell");
    CHECK(approx(out.cam_delta_deg[0] / dt, 120.0f, 1.0f), "Edge Auto-Turn reaches full 120 deg/s after ease-in");

    /* Pushing past edge while auto-turning keeps only edge_influence (0.8 in Arcade) */
    in.mouse_deg[0] = 10.0f;
    fa_step(&cfg, &st, &in, 0, &out);
    float expected_cam = 10.0f * cfg.edge_influence + 120.0f * dt;
    CHECK(approx(out.cam_delta_deg[0], expected_cam, 0.2f), "Edge Auto-Turn scales manual overflow by edge_influence");

    /* Stick deflection > 25% claims last_input and disables Edge Auto-Turn (edge_stick = 0) */
    in.mouse_deg[0] = 0.0f;
    in.stick_deg[0] = 1.0f;
    in.stick_deflection = 0.50f;
    fa_step(&cfg, &st, &in, 0, &out);
    CHECK(st.last_input == FA_INPUT_STICK, "Stick past 25% deflection claims last_input");
    CHECK(st.edge_turn_active == 0, "Edge Auto-Turn stops when stick is last_input and edge_stick == 0");

    /* Gyro is treated like mouse and re-enables Edge Auto-Turn */
    in.stick_deg[0] = 0.0f;
    in.stick_deflection = 0.0f;
    in.gyro_deg[0] = 1.0f;
    fa_step(&cfg, &st, &in, 0, &out);
    CHECK(st.last_input == FA_INPUT_GYRO, "Gyro claims last_input");
    in.gyro_deg[0] = 0.0f;
    for (int f = 0; f < 25; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
    }
    CHECK(st.edge_turn_active == 1, "Gyro enables Edge Auto-Turn when edge_gyro == 1");
}

static void test_camera_lead_and_jerk_free_catchup(void) {
    fa_cfg_t cfg;
    fa_preset_defaults(FA_PRESET_WEIGHTY, &cfg);
    cfg.camera_sway = 0.0f;
    cfg.walk_sway = 0.0f;
    cfg.turn_drag = 0.0f;
    cfg.edge_smoothing = 0.0f;
    cfg.camera_share = 0.0f;
    cfg.zone_x = 10.0f;
    cfg.zone_y = 6.0f;
    cfg.recenter_delay = 0.25f;
    cfg.recenter_smooth = 0.40f;
    cfg.recenter_speed = 1.5f;
    cfg.camera_lead = 1.0f;

    fa_state_t st;
    fa_reset_state(&st);
    fa_output_t out;
    float dt = 1.0f / 120.0f;
    fa_input_t in = make_base_input(dt);

    /* Move reticle to +8 deg inside zone over several frames (tests Camera Lead too) */
    float cam_angle_x = 0.0f;
    for (int f = 0; f < 8; ++f) {
        in.mouse_deg[0] = 1.0f;
        fa_step(&cfg, &st, &in, 0, &out);
        cam_angle_x += out.cam_delta_deg[0];
    }
    CHECK(st.last_lead_rate > 0.0f, "Camera Lead is active while moving inside zone");
    CHECK(st.target[0] > 4.0f, "Target is offset inside zone before idle");

    /* Stop input and hold idle through recenter_delay (0.25s = 30 frames at 120 Hz) */
    in.mouse_deg[0] = 0.0f;
    float world_aim_start = cam_angle_x + st.target[0];
    float prev_vel = 0.0f;
    float peak_vel = 0.0f;
    int saw_catchup = 0;
    int crossed_zero = 0;
    float prev_target = st.target[0];
    int first_catchup_frame = 1;

    for (int f = 0; f < 600; ++f) { /* 5 seconds total */
        fa_step(&cfg, &st, &in, 0, &out);
        cam_angle_x += out.cam_delta_deg[0];
        float cam_vel = out.cam_delta_deg[0] / dt;

        /* World aim point (cam_angle + target) must remain invariant while idle */
        CHECK(approx(cam_angle_x + st.target[0], world_aim_start, 1e-3f),
              "Catch-Up keeps world aim point invariant");

        if (st.idle_time <= cfg.recenter_delay + 1e-5f) {
            CHECK(approx(st.catchup_vel[0], 0.0f, 1e-5f),
                  "Catch-up velocity is strictly 0 before recenter_delay");
            prev_vel = 0.0f;
        } else {
            saw_catchup = 1;
            if (first_catchup_frame) {
                /* Zero onset step: first frame velocity must start near 0 (< 0.1 deg/s) */
                CHECK(fabsf(cam_vel) < 0.1f, "Catch-Up onset has zero velocity step jump");
                first_catchup_frame = 0;
            }
            /* Frame-to-frame acceleration is bounded (no velocity discontinuity / jerk spike) */
            float accel = fabsf(cam_vel - prev_vel) / dt;
            CHECK(accel < 60.0f, "Catch-Up camera velocity changes smoothly without jerk spike");

            if (cam_vel > peak_vel) {
                peak_vel = cam_vel;
            }
            prev_vel = cam_vel;
        }

        /* Monotonicity & zero overshoot */
        if (st.target[0] < -1e-5f) {
            crossed_zero = 1;
        }
        CHECK(st.target[0] <= prev_target + 1e-5f, "Target decreases monotonically toward zero");
        prev_target = st.target[0];
    }

    CHECK(saw_catchup == 1, "Catch-Up engaged after recenter_delay");
    CHECK(peak_vel > 1.0f, "Catch-Up ramped up to meaningful angular velocity");
    CHECK(crossed_zero == 0, "Catch-Up never overshoots target == 0");
    CHECK(fabsf(st.target[0]) < 0.05f, "Catch-Up converges smoothly to centre");
}

static void test_ads_fov_sway_and_aim_assist(void) {
    /* FOV scale helper */
    CHECK(approx(fa_fov_scale(60.0f, 60.0f), 1.0f, 1e-5f), "FOV scale is 1.0 at reference FOV");
    float zoomed = fa_fov_scale(30.0f, 60.0f);
    CHECK(zoomed > 0.45f && zoomed < 0.50f, "FOV scale halves angle at half FOV (tan ratio)");

    /* ADS pull when ads_zone < 1.0 and exaggeration when ads_zone > 1.0 */
    fa_cfg_t cfg;
    fa_preset_defaults(FA_PRESET_WEIGHTY, &cfg);
    cfg.camera_sway = 0.0f;
    cfg.walk_sway = 0.0f;
    cfg.camera_lead = 0.0f;
    cfg.recenter_speed = 0.0f;
    cfg.camera_share = 0.0f;
    cfg.ads_zone = 0.0f;
    cfg.ads_time = 0.2f;

    fa_state_t st;
    fa_reset_state(&st);
    fa_output_t out;
    fa_input_t in = make_base_input(1.0f / 60.0f);
    in.mouse_deg[0] = 6.0f;
    fa_step(&cfg, &st, &in, 0, &out);
    float world_aim = out.cam_delta_deg[0] + st.target[0];

    in.mouse_deg[0] = 0.0f;
    in.aiming = 1;
    float cam_x = out.cam_delta_deg[0];
    for (int f = 0; f < 30; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
        cam_x += out.cam_delta_deg[0];
    }
    CHECK(out.ads_amount > 0.99f, "ADS blend reaches 1.0");
    CHECK(fabsf(st.target[0]) < 0.2f, "ads_zone == 0 pulls reticle toward centre while aiming");
    CHECK(approx(cam_x + st.target[0], world_aim, 1e-2f), "ADS pull keeps world aim steady");

    /* ads_zone == 1.5 exaggerates zone size while aiming */
    fa_reset_state(&st);
    cfg.ads_zone = 1.5f;
    in.aiming = 1;
    for (int f = 0; f < 20; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
    }
    fa_debug_info_t dbg;
    fa_get_debug_info(&st, &dbg);
    CHECK(approx(dbg.zone_x, cfg.zone_x * 1.5f, 1e-3f), "ads_zone > 1.0 exaggerates zone size");

    /* Camera & walk sway is strictly bounded and never drifts */
    fa_preset_defaults(FA_PRESET_IMMERSIVE, &cfg);
    cfg.camera_lead = 0.0f;
    cfg.recenter_speed = 0.0f;
    fa_reset_state(&st);
    in = make_base_input(1.0f / 60.0f);
    in.move_fraction = 1.0f;
    float net_cam_x = 0.0f, net_cam_y = 0.0f;
    for (int f = 0; f < 600; ++f) { /* 10 seconds */
        fa_step(&cfg, &st, &in, 0, &out);
        net_cam_x += out.cam_delta_deg[0];
        net_cam_y += out.cam_delta_deg[1];
    }
    CHECK(fabsf(net_cam_x) < 1.5f && fabsf(net_cam_y) < 1.5f,
          "Delta-only sway stays bounded and never drifts over 10 seconds");

    /* Aim assist pulls reticle only, never the camera */
    fa_preset_defaults(FA_PRESET_WEIGHTY, &cfg);
    cfg.camera_sway = 0.0f;
    cfg.walk_sway = 0.0f;
    cfg.camera_lead = 0.0f;
    cfg.recenter_speed = 0.0f;
    fa_reset_state(&st);
    in = make_base_input(1.0f / 60.0f);
    in.assist_locked = 1;
    in.assist_target_deg[0] = 4.0f;
    in.assist_target_deg[1] = -2.0f;
    float assist_cam = 0.0f;
    for (int f = 0; f < 120; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
        assist_cam += fabsf(out.cam_delta_deg[0]) + fabsf(out.cam_delta_deg[1]);
    }
    CHECK(approx(assist_cam, 0.0f, 1e-5f), "Aim assist never rotates the camera");
    CHECK(approx(out.reticle_deg[0], 4.0f, 0.15f) && approx(out.reticle_deg[1], -2.0f, 0.15f),
          "Aim assist eases reticle onto target angle");
}

static void test_spring_projection_tools_and_safety(void) {
    /* Spring convergence & lag cap */
    fa_cfg_t cfg;
    fa_preset_defaults(FA_PRESET_IMMERSIVE, &cfg);
    cfg.camera_sway = 0.0f;
    cfg.walk_sway = 0.0f;
    cfg.camera_lead = 0.0f;
    cfg.recenter_speed = 0.0f;
    cfg.camera_share = 0.5f;
    cfg.turn_drag = 1.0f;
    cfg.edge_smoothing = 0.0f;

    fa_state_t st;
    fa_reset_state(&st);
    fa_output_t out;
    fa_input_t in = make_base_input(1.0f / 60.0f);
    in.mouse_deg[0] = 120.0f; /* violent spin */
    fa_step(&cfg, &st, &in, 0, &out);

    float min_z = cfg.zone_x < cfg.zone_y ? cfg.zone_x : cfg.zone_y;
    float max_lag = 0.35f * min_z + 1.5f;
    float lag_len = hypotf(
        st.display[0] - (st.target[0] + st.over[0] + st.assist[0]),
        st.display[1] - (st.target[1] + st.over[1] + st.assist[1]));
    CHECK(lag_len <= max_lag + 1e-4f, "Spring lag is capped on fast spins");

    /* Converges to target */
    in.mouse_deg[0] = 0.0f;
    for (int f = 0; f < 180; ++f) {
        fa_step(&cfg, &st, &in, 0, &out);
    }
    CHECK(approx(st.display[0], st.target[0], 0.05f), "Damped spring converges to target");

    /* Projection & shot-dir consistency */
    float ret[2] = { 10.0f, 5.0f };
    float ndc[2];
    fa_project_reticle_ndc(ret, 60.0f, 16.0f / 9.0f, ndc);
    CHECK(ndc[0] > 0.0f && ndc[0] < 0.97f && ndc[1] > 0.0f && ndc[1] < 0.97f,
          "Reticle projects to valid positive NDC");

    float fwd[3] = { 0.0f, 0.0f, 1.0f };
    float right[3] = { 1.0f, 0.0f, 0.0f };
    float up[3] = { 0.0f, 1.0f, 0.0f };
    float dir[3];
    fa_compute_shot_dir(fwd, right, up, ret, dir);
    float dlen = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    CHECK(approx(dlen, 1.0f, 1e-5f), "Shot direction is unit length");
    CHECK(approx(atan2f(dir[0], dir[2]) * 180.0f / 3.14159265f, 10.0f, 1e-3f),
          "Shot direction yaw matches reticle yaw");
    CHECK(approx(atan2f(dir[1], dir[2]) * 180.0f / 3.14159265f, 5.0f, 1e-3f),
          "Shot direction pitch matches reticle pitch");

    /* Viewmodel offset helper */
    float barrel[3] = { 0.1f, -0.05f, 0.3f };
    float vm_pos[3], vm_rot[2];
    fa_compute_viewmodel_offset(ret, 1.0f, -0.02f, barrel, 0.5f, vm_pos, vm_rot);
    CHECK(approx(vm_rot[0], 10.0f, 1e-4f) && approx(vm_rot[1], 5.0f, 1e-4f),
          "Viewmodel rotation matches reticle offset");
    CHECK(isfinite(vm_pos[0]) && isfinite(vm_pos[1]) && approx(vm_pos[2], 0.0f, 1e-5f),
          "Viewmodel ADS translation is finite");

    /* Scripted 4.8s input cycle */
    CHECK(approx(fa_scripted_input_sample(0.2f), 0.5f, 1e-5f), "Scripted phase 1: fast right");
    CHECK(approx(fa_scripted_input_sample(1.0f), 0.0f, 1e-5f), "Scripted phase 2: stop");
    CHECK(approx(fa_scripted_input_sample(1.7f), -1.0f, 1e-5f), "Scripted phase 3: fast left");
    CHECK(approx(fa_scripted_input_sample(2.4f), 0.0f, 1e-5f), "Scripted phase 4: stop");
    CHECK(approx(fa_scripted_input_sample(3.3f), 0.12f, 1e-5f), "Scripted phase 5: slow sweep");
    CHECK(approx(fa_scripted_input_sample(4.2f), 0.0f, 1e-5f), "Scripted phase 6: stop");
    CHECK(approx(fa_scripted_input_sample(4.8f + 0.2f), 0.5f, 1e-5f), "Scripted cycle wraps at 4.8s");

    /* CSV telemetry formatting */
    char hdr[256], row[256];
    int n_hdr = fa_format_telemetry_csv_header(hdr, sizeof(hdr));
    float in_deg[2] = { 1.25f, -0.5f };
    float cam_ang[2] = { 45.0f, 2.5f };
    int n_row = fa_format_telemetry_csv_row(row, sizeof(row), 1.0f, 1.0f / 60.0f, &cfg, &st, in_deg, &out, cam_ang, 60.0f);
    CHECK(n_hdr > 0 && strstr(hdr, "reticle_yaw") != NULL, "CSV header formats with reticle columns");
    CHECK(n_row > 0 && strstr(row, "45.000") != NULL, "CSV row formats telemetry fields");

    /* Pause (dt <= 0) preserves state and returns zero deltas */
    fa_state_t before = st;
    in.dt_sec = 0.0f;
    in.mouse_deg[0] = 15.0f;
    fa_step(&cfg, &st, &in, 0, &out);
    CHECK(approx(out.cam_delta_deg[0], 0.0f, 1e-6f) && approx(out.reticle_delta_deg[0], 0.0f, 1e-6f),
          "dt <= 0 returns zero camera and reticle deltas");
    CHECK(memcmp(&st, &before, sizeof(st)) == 0, "dt <= 0 preserves state identically");

    /* NaN/Inf safety */
    in.dt_sec = 1.0f / 60.0f;
    in.mouse_deg[0] = NAN;
    in.mouse_deg[1] = INFINITY;
    in.stick_deflection = NAN;
    in.fov_scale = NAN;
    fa_step(&cfg, &st, &in, 0, &out);
    CHECK(isfinite(out.cam_delta_deg[0]) && isfinite(out.cam_delta_deg[1])
          && isfinite(out.reticle_deg[0]) && isfinite(out.reticle_deg[1]),
          "NaN/Inf inputs produce finite outputs");
}

int main(void) {
    test_presets_and_force_original();
    test_ellipse_conservation_drag_and_edge_overflow();
    test_edge_auto_turn_and_input_gating();
    test_camera_lead_and_jerk_free_catchup();
    test_ads_fov_sway_and_aim_assist();
    test_spring_projection_tools_and_safety();

    if (g_failures == 0) {
        printf("test_free_aim: ALL TESTS PASSED\n");
        return 0;
    }
    fprintf(stderr, "test_free_aim: %d FAILURE(S)\n", g_failures);
    return 1;
}
