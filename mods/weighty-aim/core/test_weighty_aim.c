/*
 * test_weighty_aim.c - Standalone pure-C11 unit tests for weighty_aim.
 */

#include "weighty_aim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, (msg));  \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static int approx(float a, float b, float eps) {
    return fabsf(a - b) <= eps;
}

static void test_curves_and_bezier(void) {
    wa_stick_cfg_t cfg;
    wa_stick_cfg_defaults(&cfg);

    /* Classic is default and passes deflection * sensitivity */
    CHECK(cfg.curve == WA_CURVE_CLASSIC, "Default curve is Classic");
    CHECK(approx(wa_curve_output(&cfg, 0.5f), 0.5f, 1e-4f), "Classic at 0.5 with sens 1.0");
    cfg.sensitivity = 1.5f;
    CHECK(approx(wa_curve_output(&cfg, 0.5f), 0.75f, 1e-4f), "Classic still applies sensitivity");

    /* Linear & Balanced */
    cfg.sensitivity = 1.0f;
    cfg.inner_deadzone = 0.0f;
    cfg.outer_deadzone = 1.0f;
    cfg.curve = WA_CURVE_LINEAR;
    CHECK(approx(wa_curve_output(&cfg, 0.25f), 0.25f, 1e-4f), "Linear curve output");

    cfg.curve = WA_CURVE_BALANCED;
    CHECK(approx(wa_curve_output(&cfg, 0.25f), 0.125f, 1e-4f), "Balanced curve is n^1.5 (0.25^1.5 = 0.125)");

    /* Bezier identity (0,0)-(1,1) with linear control points */
    float lin_pts[4] = { 0.25f, 0.25f, 0.75f, 0.75f };
    CHECK(approx(wa_bezier_eval(lin_pts, 0.0f), 0.0f, 1e-5f), "Bezier at 0");
    CHECK(approx(wa_bezier_eval(lin_pts, 0.5f), 0.5f, 1e-4f), "Linear Bezier at 0.5");
    CHECK(approx(wa_bezier_eval(lin_pts, 1.0f), 1.0f, 1e-5f), "Bezier at 1");

    /* Custom slots remember independent points */
    wa_select_curve(&cfg, WA_CURVE_CUSTOM1);
    cfg.bezier[0] = 0.2f;
    cfg.bezier[1] = 0.8f;
    wa_curve_points_changed(&cfg);

    wa_select_curve(&cfg, WA_CURVE_CUSTOM2);
    CHECK(approx(cfg.bezier[0], 0.4f, 1e-5f) && approx(cfg.bezier[1], 0.0f, 1e-5f),
          "Custom 2 retains default points independently of Custom 1");
    cfg.bezier[0] = 0.9f;
    cfg.bezier[1] = 0.1f;
    wa_curve_points_changed(&cfg);

    wa_select_curve(&cfg, WA_CURVE_CUSTOM1);
    CHECK(approx(cfg.bezier[0], 0.2f, 1e-5f) && approx(cfg.bezier[1], 0.8f, 1e-5f),
          "Switching back to Custom 1 restores its saved points");
}

static void test_radial_deadzone_and_diagonals(void) {
    wa_stick_cfg_t cfg;
    wa_stick_state_t st;
    wa_stick_cfg_defaults(&cfg);
    wa_stick_state_init(&st);

    cfg.curve = WA_CURVE_LINEAR;
    cfg.inner_deadzone = 0.20f;
    cfg.outer_deadzone = 0.80f;
    cfg.sensitivity = 1.0f;
    cfg.vertical_sens = 1.0f;
    cfg.boost_mode = WA_BOOST_OFF;

    /* Inside inner deadzone -> zero */
    float out[2];
    wa_process_stick(&cfg, &st, 0.10f, 0.10f, 0.016f, 0, out);
    CHECK(approx(out[0], 0.0f, 1e-5f) && approx(out[1], 0.0f, 1e-5f),
          "Radial deadzone zeroes stick inside inner radius");

    /* Diagonal at 45 deg preserves exact angle (no axis-snap) */
    float d = 0.50f / sqrtf(2.0f); /* mag = 0.50, midpoint of [0.20, 0.80] -> n = 0.50 */
    wa_process_stick(&cfg, &st, d, d, 0.016f, 0, out);
    CHECK(approx(out[0], out[1], 1e-4f), "Diagonal stick output stays symmetric on 45 deg");
    CHECK(approx(hypotf(out[0], out[1]), 0.50f, 1e-3f), "Radial deadzone remaps magnitude linearly");

    /* Outer deadzone clamps to inner + 0.05 minimum */
    cfg.inner_deadzone = 0.40f;
    cfg.outer_deadzone = 0.20f;
    CHECK(approx(wa_curve_output(&cfg, 0.425f), 0.50f, 1e-3f),
          "Outer deadzone is clamped to at least inner + 0.05");
}

static void test_turn_boost_ramp_decay_and_vertical(void) {
    wa_stick_cfg_t cfg;
    wa_stick_state_t st;
    wa_stick_cfg_defaults(&cfg);
    wa_stick_state_init(&st);

    cfg.curve = WA_CURVE_LINEAR;
    cfg.inner_deadzone = 0.0f;
    cfg.outer_deadzone = 1.0f;
    cfg.sensitivity = 1.0f;
    cfg.vertical_sens = 1.0f;
    cfg.boost_mode = WA_BOOST_RAMPED;
    cfg.boost_threshold = 0.9f;
    cfg.boost_delay = 0.10f;
    cfg.boost_time = 0.25f;
    cfg.boost_amount = 2.0f;
    cfg.boost_vertical = 0.35f;

    float out[2];
    /* During delay (< 0.10s), boost stays 0 */
    wa_process_stick(&cfg, &st, 1.0f, 0.0f, 0.05f, 0, out);
    CHECK(approx(st.boost_level, 0.0f, 1e-4f) && approx(out[0], 1.0f, 1e-4f),
          "Ramped boost waits through delay");

    /* Halfway through ramp (0.05 + 0.175 = 0.225s -> t = 0.5 -> smoothstep = 0.5) */
    wa_process_stick(&cfg, &st, 1.0f, 0.0f, 0.175f, 0, out);
    CHECK(approx(st.boost_level, 0.5f, 1e-3f) && approx(out[0], 1.5f, 1e-3f),
          "Ramped boost smoothsteps at midpoint");

    /* Full ramp reached; check vertical share (0.35 of +1.0 = 1.35x) */
    wa_process_stick(&cfg, &st, 0.0f, 1.0f, 0.20f, 0, out);
    CHECK(approx(st.boost_level, 1.0f, 1e-4f), "Ramped boost reaches 1.0");
    CHECK(approx(out[1], 1.35f, 1e-3f), "Vertical axis gets only boost_vertical share");

    /* Release decays over ~0.1s rather than 1 frame */
    wa_process_stick(&cfg, &st, 0.2f, 0.0f, 0.05f, 0, out);
    CHECK(approx(st.boost_level, 0.5f, 1e-3f), "Boost decays smoothly over ~0.1s on release");

    /* Instant mode jumps to full immediately */
    wa_stick_state_init(&st);
    cfg.boost_mode = WA_BOOST_INSTANT;
    wa_process_stick(&cfg, &st, 1.0f, 0.0f, 0.016f, 0, out);
    CHECK(approx(st.boost_level, 1.0f, 1e-4f) && approx(out[0], 2.0f, 1e-3f),
          "Instant boost reaches full amount immediately");
}

static void test_curve_graph_helper(void) {
    wa_stick_cfg_t cfg;
    wa_stick_cfg_defaults(&cfg);
    wa_select_curve(&cfg, WA_CURVE_CUSTOM1);
    cfg.sensitivity = 2.0f;
    cfg.inner_deadzone = 0.10f;
    cfg.outer_deadzone = 0.90f;

    wa_curve_graph_t g;
    wa_build_curve_graph(&cfg, 0.50f, 33, &g);

    CHECK(approx(g.y_max, 2.0f, 1e-5f), "Graph y_max scales to max(sens, 1.0)");
    CHECK(approx(g.inner_deadzone_x, 0.10f, 1e-5f), "Graph inner deadzone band");
    CHECK(approx(g.outer_deadzone_x, 0.90f, 1e-5f), "Graph outer deadzone band");
    CHECK(g.sample_count == 33, "Graph sample count matches request");
    CHECK(approx(g.curve_norm[0], 0.0f, 1e-5f) && approx(g.curve_norm[32], 1.0f, 1e-4f),
          "Graph endpoints span 0..1 normalized");
    CHECK(g.has_custom_handles == 1, "Graph exposes custom Bezier handles");
    CHECK(g.has_live_marker == 1 && approx(g.live_x, 0.50f, 1e-5f), "Graph includes live stick marker");
}

static void test_gyro_pipeline_and_calibration(void) {
    wa_gyro_cfg_t cfg;
    wa_gyro_state_t st;
    wa_gyro_cfg_defaults(&cfg);
    wa_gyro_state_init(&st);

    /* Initial status before any sample */
    CHECK(wa_gyro_status(&st) == WA_GYRO_STATUS_NOT_FOUND, "Status is NOT_FOUND initially");
    CHECK(strcmp(wa_gyro_status_text(&st), "No gyro found on this controller") == 0, "Exact NOT_FOUND text");

    wa_gyro_sensor_t sensor;
    memset(&sensor, 0, sizeof(sensor));
    sensor.available = 1;
    sensor.gyro_deg_s[0] = 0.5f; /* constant bias */
    sensor.gyro_deg_s[1] = -1.0f;
    sensor.gyro_deg_s[2] = 0.25f;
    sensor.accel[1] = 1.0f; /* gravity along +Y (flat controller) */

    /* Menu tick detects gyro */
    wa_gyro_menu_tick(&cfg, &st, &sensor, 0.0f); /* dt=0 falls back to 1/60s */
    CHECK(wa_gyro_status(&st) == WA_GYRO_STATUS_UNCALIBRATED, "Detected gyro is UNCALIBRATED");
    CHECK(strcmp(wa_gyro_status_text(&st), "Gyro found - calibrate for best results") == 0, "Exact UNCALIBRATED text");

    /* Manual calibration over 1.5s */
    wa_gyro_start_calibration(&st);
    CHECK(wa_gyro_status(&st) == WA_GYRO_STATUS_CALIBRATING, "Status is CALIBRATING");
    CHECK(strcmp(wa_gyro_status_text(&st), "Calibrating... keep the controller still") == 0, "Exact CALIBRATING text");

    cfg.mode = WA_GYRO_ALWAYS;
    float out[2];
    for (int i = 0; i < 95; ++i) {
        wa_process_gyro(&cfg, &st, &sensor, 0, 0, 1.0f / 60.0f, 0, out);
        if (st.calib_time > 0.0f) {
            CHECK(out[0] == 0.0f && out[1] == 0.0f, "Gyro outputs 0 while calibrating");
        }
    }
    CHECK(wa_gyro_status(&st) == WA_GYRO_STATUS_READY, "Calibration finishes -> READY");
    CHECK(strcmp(wa_gyro_status_text(&st), "Gyro ready (calibrated)") == 0, "Exact READY text");
    CHECK(approx(st.bias[0], 0.5f, 1e-3f) && approx(st.bias[1], -1.0f, 1e-3f) && approx(st.bias[2], 0.25f, 1e-3f),
          "Manual calibration measures exact bias");

    /* Auto-calibration settles small drift after >1s still */
    wa_gyro_state_init(&st);
    sensor.gyro_deg_s[0] = 0.8f;
    sensor.gyro_deg_s[1] = 0.0f;
    sensor.gyro_deg_s[2] = 0.0f;
    for (int i = 0; i < 180; ++i) { /* 3 seconds still */
        wa_gyro_menu_tick(&cfg, &st, &sensor, 1.0f / 60.0f);
    }
    CHECK(st.calibrated == 1 && st.bias[0] > 0.5f, "Auto-calibrate settles bias when held still");

    /* Local space + exact deg/s -> deg scaling + invert_y */
    wa_gyro_cfg_defaults(&cfg);
    wa_gyro_state_init(&st);
    cfg.mode = WA_GYRO_ALWAYS;
    cfg.space = WA_GYRO_SPACE_LOCAL;
    cfg.sensitivity = 2.0f;
    cfg.vertical = 0.5f;
    cfg.smoothing = 0.0f;
    cfg.tightening = 0.0f;
    cfg.acceleration = 1.0f;
    cfg.auto_calibrate = 0;

    memset(&sensor, 0, sizeof(sensor));
    sensor.available = 1;
    sensor.gyro_deg_s[0] = 20.0f;  /* +pitch */
    sensor.gyro_deg_s[1] = -30.0f; /* turning right in SDL (-y) */
    wa_process_gyro(&cfg, &st, &sensor, 0, 0, 0.1f, 0, out);
    CHECK(approx(out[0], 6.0f, 1e-3f), "Local yaw: -(-30) * 2.0 * 0.1 = +6.0 deg");
    CHECK(approx(out[1], 2.0f, 1e-3f), "Local pitch: 20 * 2.0 * 0.5 * 0.1 = +2.0 deg");

    cfg.invert_y = 1;
    wa_process_gyro(&cfg, &st, &sensor, 0, 0, 0.1f, 0, out);
    CHECK(approx(out[1], -2.0f, 1e-3f), "Invert vertical flips pitch sign");

    /* Player space uses roll when tilted (gravity along +Z) */
    wa_gyro_state_init(&st);
    cfg.space = WA_GYRO_SPACE_PLAYER;
    cfg.invert_y = 0;
    sensor.gyro_deg_s[0] = 0.0f;
    sensor.gyro_deg_s[1] = 0.0f;
    sensor.gyro_deg_s[2] = -20.0f; /* pure roll */
    sensor.accel[0] = 0.0f;
    sensor.accel[1] = 0.0f;
    sensor.accel[2] = 1.0f; /* gravity along +Z */
    for (int i = 0; i < 30; ++i) {
        wa_process_gyro(&cfg, &st, &sensor, 0, 0, 0.02f, 0, out);
    }
    CHECK(out[0] > 0.5f, "Player space maps roll along gravity into yaw");

    /* Tightening + soft tiered smoothing + acceleration ordering */
    wa_gyro_cfg_defaults(&cfg);
    wa_gyro_state_init(&st);
    cfg.mode = WA_GYRO_ALWAYS;
    cfg.space = WA_GYRO_SPACE_LOCAL;
    cfg.sensitivity = 1.0f;
    cfg.tightening = 10.0f;
    cfg.smoothing = 0.0f;
    cfg.auto_calibrate = 0;
    sensor.gyro_deg_s[0] = 0.0f;
    sensor.gyro_deg_s[1] = -5.0f; /* half of tightening -> scaled by 0.5 -> -2.5 deg/s */
    sensor.gyro_deg_s[2] = 0.0f;
    wa_process_gyro(&cfg, &st, &sensor, 0, 0, 0.1f, 0, out);
    CHECK(approx(out[0], 0.25f, 1e-3f), "Tightening scales sub-threshold speed by speed/threshold");

    /* Acceleration at full threshold (2x at 100 deg/s) */
    cfg.tightening = 0.0f;
    cfg.acceleration = 2.0f;
    cfg.accel_threshold = 100.0f;
    sensor.gyro_deg_s[1] = -100.0f;
    wa_process_gyro(&cfg, &st, &sensor, 0, 0, 0.1f, 0, out);
    CHECK(approx(out[0], 20.0f, 1e-2f), "Acceleration doubles sensitivity at accel_threshold");

    /* Smoothing resets when paused by stick or not aiming in While Aiming mode */
    cfg.smoothing = 10.0f;
    cfg.acceleration = 1.0f;
    cfg.pause_with_stick = 1;
    sensor.gyro_deg_s[1] = -3.0f;
    wa_process_gyro(&cfg, &st, &sensor, 1, 0, 0.05f, 0, out);
    CHECK(fabsf(st.smooth[0]) > 0.01f, "Smoothing state accumulates for small motion");
    wa_process_gyro(&cfg, &st, &sensor, 1, 1, 0.05f, 0, out);
    CHECK(out[0] == 0.0f && st.smooth[0] == 0.0f && st.smooth[1] == 0.0f,
          "Pause While Using Look Stick zeroes output and resets smoothing state");

    cfg.mode = WA_GYRO_AIMING;
    cfg.pause_with_stick = 0;
    wa_process_gyro(&cfg, &st, &sensor, 1, 0, 0.05f, 0, out);
    CHECK(fabsf(st.smooth[0]) > 0.01f, "While Aiming works when aiming");
    wa_process_gyro(&cfg, &st, &sensor, 0, 0, 0.05f, 0, out);
    CHECK(out[0] == 0.0f && st.smooth[0] == 0.0f, "While Aiming resets smoothing when not aiming");
}

static void test_resets_force_original_and_safety(void) {
    wa_player_t p;
    wa_player_init(&p);

    /* Modify stick, boost, custom profile, and gyro */
    wa_select_curve(&p.stick_cfg, WA_CURVE_CUSTOM1);
    p.stick_cfg.bezier[0] = 0.15f;
    wa_curve_points_changed(&p.stick_cfg);
    p.stick_cfg.sensitivity = 2.0f;
    p.stick_cfg.boost_amount = 3.5f;
    p.stick_cfg.game_builtin_deadzone = 0;

    p.gyro_cfg.mode = WA_GYRO_ALWAYS;
    p.gyro_cfg.sensitivity = 8.0f;

    /* Separate resets */
    wa_reset_stick_response(&p.stick_cfg);
    CHECK(p.stick_cfg.curve == WA_CURVE_CLASSIC && approx(p.stick_cfg.sensitivity, 1.0f, 1e-5f),
          "wa_reset_stick_response restores stick response defaults");
    CHECK(approx(p.stick_cfg.boost_amount, 3.5f, 1e-5f),
          "wa_reset_stick_response does not reset boost");
    CHECK(approx(p.stick_cfg.bezier_profiles[0][0], 0.15f, 1e-5f),
          "wa_reset_stick_response preserves custom curve profiles");

    wa_reset_boost(&p.stick_cfg);
    CHECK(approx(p.stick_cfg.boost_amount, 2.0f, 1e-5f), "wa_reset_boost restores boost defaults");

    wa_reset_gyro(&p.gyro_cfg);
    CHECK(p.gyro_cfg.mode == WA_GYRO_ALWAYS && approx(p.gyro_cfg.sensitivity, 2.5f, 1e-5f),
          "wa_reset_gyro restores tuning defaults while keeping current mode");

    /* Force Original Aim: 1:1 pass-through, forces builtin deadzone, blocks gyro, preserves settings */
    p.stick_cfg.curve = WA_CURVE_BALANCED;
    p.stick_cfg.sensitivity = 2.0f;
    p.stick_cfg.game_builtin_deadzone = 0;
    float out[2];
    wa_process_stick(&p.stick_cfg, &p.stick_state, 0.4f, -0.3f, 0.016f, 1, out);
    CHECK(approx(out[0], 0.4f, 1e-5f) && approx(out[1], -0.3f, 1e-5f),
          "Force Original Aim yields 1:1 stick pass-through");
    CHECK(wa_game_builtin_deadzone_wanted(&p.stick_cfg, 1) == 1,
          "Force Original Aim forces game's built-in deadzone on");
    CHECK(p.stick_cfg.curve == WA_CURVE_BALANCED && p.stick_cfg.game_builtin_deadzone == 0,
          "Force Original Aim does not mutate saved settings");

    wa_gyro_sensor_t sensor;
    memset(&sensor, 0, sizeof(sensor));
    sensor.available = 1;
    sensor.gyro_deg_s[1] = -50.0f;
    wa_process_gyro(&p.gyro_cfg, &p.gyro_state, &sensor, 1, 0, 0.05f, 1, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f, "Force Original Aim disables gyro output");

    /* dt <= 0 and NaN safety */
    p.stick_state.boost_level = 0.75f;
    wa_process_stick(&p.stick_cfg, &p.stick_state, 1.0f, 0.0f, 0.0f, 0, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f && approx(p.stick_state.boost_level, 0.75f, 1e-5f),
          "Stick dt == 0 returns zero and preserves state");

    wa_process_stick(&p.stick_cfg, &p.stick_state, NAN, NAN, 0.016f, 0, out);
    CHECK(isfinite(out[0]) && isfinite(out[1]), "Stick NaN input yields finite output");

    p.gyro_state.smooth[0] = 1.25f;
    wa_process_gyro(&p.gyro_cfg, &p.gyro_state, &sensor, 1, 0, -0.01f, 0, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f && approx(p.gyro_state.smooth[0], 1.25f, 1e-5f),
          "Gyro dt < 0 returns zero and preserves state");

    sensor.gyro_deg_s[0] = NAN;
    sensor.gyro_deg_s[1] = NAN;
    sensor.accel[0] = NAN;
    wa_process_gyro(&p.gyro_cfg, &p.gyro_state, &sensor, 1, 0, 0.016f, 0, out);
    CHECK(isfinite(out[0]) && isfinite(out[1]), "Gyro NaN sensor yields finite output");
}

int main(void) {
    test_curves_and_bezier();
    test_radial_deadzone_and_diagonals();
    test_turn_boost_ramp_decay_and_vertical();
    test_curve_graph_helper();
    test_gyro_pipeline_and_calibration();
    test_resets_force_original_and_safety();

    if (g_failures > 0) {
        fprintf(stderr, "weighty_aim tests: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("weighty_aim tests: ALL PASSED (6 suites)\n");
    return 0;
}
