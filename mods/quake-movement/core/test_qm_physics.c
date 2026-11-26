/*
 * test_qm_physics.c - Standalone pure-C11 unit tests for qm_physics.
 */

#include "qm_physics.h"

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

static void test_vq3_strafe_jump_speed_gain(void) {
    qm_params_t params;
    qm_state_t st;
    qm_preset_defaults(QM_PRESET_QUAKE_VQ3, &params);
    qm_state_init(&st, &params);

    /* Accelerate on ground to near max_speed */
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 120; ++i) {
        qm_input_t in;
        memset(&in, 0, sizeof(in));
        in.dt_sec = dt;
        in.wish_forward = 1.0f;
        in.on_ground = 1;
        qm_step(&params, &in, &st, &st);
    }
    float ground_spd = qm_horiz_speed(st.vel);
    CHECK(ground_spd > params.max_speed * 0.95f, "VQ3 reaches ground max speed");

    /* Strafe-jump: jump and sweep camera yaw right while holding W+D */
    float yaw = 0.0f;
    for (int i = 0; i < 360; ++i) {
        qm_input_t in;
        memset(&in, 0, sizeof(in));
        in.dt_sec = dt;
        in.wish_forward = 1.0f;
        in.wish_right = 1.0f;
        in.jump_held = 1;
        in.jump_pressed = (i == 0);
        in.on_ground = (st.pos[2] <= 0.0f && st.vel[2] <= 0.0f) ? 1 : 0;
        if (in.on_ground) {
            st.pos[2] = 0.0f;
        }
        /* Smooth right turn (negative yaw in CCW-around-+Z convention) */
        yaw -= 0.45f * dt;
        in.cam_yaw_rad = yaw;
        qm_step(&params, &in, &st, &st);
        if (st.pos[2] < 0.0f) {
            st.pos[2] = 0.0f;
        }
    }

    float final_spd = qm_horiz_speed(st.vel);
    CHECK(final_spd > params.max_speed + 2.0f, "VQ3 strafe-jumping gains speed above max_speed");
    CHECK(st.peak_speed >= final_spd, "VQ3 peak_speed tracks max horizontal speed");
}

static void test_cpma_air_control_turning(void) {
    qm_params_t params;
    qm_state_t st;
    qm_preset_defaults(QM_PRESET_QUAKE_CPMA, &params);
    qm_state_init(&st, &params);

    /* Airborne moving north (+Y) at 12 m/s */
    st.vel[0] = 0.0f;
    st.vel[1] = 12.0f;
    st.vel[2] = 4.0f;
    st.pos[2] = 2.0f;

    const float dt = 1.0f / 120.0f;
    float yaw = 0.0f;
    for (int i = 0; i < 60; ++i) {
        qm_input_t in;
        memset(&in, 0, sizeof(in));
        in.dt_sec = dt;
        in.wish_forward = 1.0f; /* Pure W engages CPM_AirControl */
        in.wish_right = 0.0f;
        in.on_ground = 0;
        yaw -= 0.9f * dt; /* Turn clockwise toward +X */
        in.cam_yaw_rad = yaw;
        qm_step(&params, &in, &st, &st);
    }

    float spd = qm_horiz_speed(st.vel);
    CHECK(st.vel[0] > 3.0f, "CPMA air control curves velocity toward +X");
    CHECK(spd > 11.5f, "CPMA air control preserves horizontal momentum");
}

static void test_half_life_low_cap_bhop(void) {
    qm_params_t params;
    qm_state_t st;
    qm_preset_defaults(QM_PRESET_HALF_LIFE, &params);
    qm_state_init(&st, &params);

    const float dt = 1.0f / 120.0f;
    /* Start with horizontal speed and crouch-jump */
    st.vel[0] = 0.0f;
    st.vel[1] = params.max_speed;

    qm_input_t jump_in;
    memset(&jump_in, 0, sizeof(jump_in));
    jump_in.dt_sec = dt;
    jump_in.wish_right = 1.0f;
    jump_in.jump_held = 1;
    jump_in.crouch_held = 1;
    jump_in.on_ground = 1;
    qm_step(&params, &jump_in, &st, &st);

    CHECK(st.jumped == 1, "Half-Life auto-bhop triggers jump on ground while jump_held");
    CHECK(st.vel[2] > params.jump_speed + 0.1f, "Half-Life crouch-jump adds vertical boost");
    CHECK(qm_horiz_speed(st.vel) >= params.max_speed, "Half-Life skips ground friction on jump takeoff");

    /* Air-strafe with pure D aligned perpendicular to current velocity heading */
    for (int i = 0; i < 240; ++i) {
        qm_input_t in;
        memset(&in, 0, sizeof(in));
        in.dt_sec = dt;
        in.wish_right = 1.0f;
        in.jump_held = 1;
        in.on_ground = (st.pos[2] <= 0.0f && st.vel[2] <= 0.0f) ? 1 : 0;
        if (in.on_ground) {
            st.pos[2] = 0.0f;
        }
        in.cam_yaw_rad = atan2f(-st.vel[0], st.vel[1]);
        qm_step(&params, &in, &st, &st);
        if (st.pos[2] < 0.0f) {
            st.pos[2] = 0.0f;
        }
    }

    CHECK(qm_horiz_speed(st.vel) > params.max_speed + 2.5f, "Half-Life low-cap air-accel gains bhop speed");
}

static void test_apex_slide_slope_and_tap_strafe(void) {
    qm_params_t params;
    qm_state_t st;
    qm_preset_defaults(QM_PRESET_APEX_LEGENDS, &params);
    qm_state_init(&st, &params);

    const float dt = 1.0f / 120.0f;
    /* Start sprinting along +Y */
    st.vel[0] = 0.0f;
    st.vel[1] = params.max_speed;

    /* 1. Flat crouch-slide boost */
    qm_input_t in;
    memset(&in, 0, sizeof(in));
    in.dt_sec = dt;
    in.wish_forward = 1.0f;
    in.crouch_held = 1;
    in.crouch_pressed = 1;
    in.on_ground = 1;
    qm_step(&params, &in, &st, &st);

    float after_boost = qm_horiz_speed(st.vel);
    CHECK(st.slide_started == 1 && st.sliding == 1, "Apex initiates crouch-slide at sprint speed");
    CHECK(after_boost > params.max_speed + 2.5f, "Apex flat slide grants speed boost");

    /* 2. Downhill slope acceleration along +Y (normal tilted toward +Y and +Z) */
    for (int i = 0; i < 60; ++i) {
        memset(&in, 0, sizeof(in));
        in.dt_sec = dt;
        in.crouch_held = 1;
        in.on_ground = 1;
        in.slope_normal[0] = 0.0f;
        in.slope_normal[1] = 0.45f;
        in.slope_normal[2] = 0.893f;
        qm_step(&params, &in, &st, &st);
    }
    float after_slope = qm_horiz_speed(st.vel);
    CHECK(after_slope > after_boost + 1.0f, "Apex downhill slope accelerates slide");

    /* 3. Slide-jump + tap-strafe 90-degree redirect to +X */
    memset(&in, 0, sizeof(in));
    in.dt_sec = dt;
    in.crouch_held = 1;
    in.jump_pressed = 1;
    in.jump_held = 1;
    in.on_ground = 1;
    qm_step(&params, &in, &st, &st);

    CHECK(st.slide_jumped == 1, "Apex slide-jump triggers slide_jumped event");
    CHECK(st.tap_strafe_timer > 0.0f, "Apex jump opens tap-strafe window");
    float jump_spd = qm_horiz_speed(st.vel);

    int saw_tap_strafe = 0;
    for (int i = 0; i < 20; ++i) {
        memset(&in, 0, sizeof(in));
        in.dt_sec = dt;
        in.wish_right = 1.0f; /* Lurch sharply toward +X */
        in.on_ground = 0;
        qm_step(&params, &in, &st, &st);
        if (st.tap_strafed) {
            saw_tap_strafe = 1;
        }
    }
    CHECK(saw_tap_strafe == 1, "Apex tap-strafe redirects velocity within post-jump window");
    CHECK(st.vel[0] > 6.0f, "Apex tap-strafe shifts velocity strongly toward +X");
    CHECK(qm_horiz_speed(st.vel) > jump_spd * 0.90f, "Apex tap-strafe preserves horizontal momentum");
}

static void test_deadlock_dash_and_dash_jump(void) {
    qm_params_t params;
    qm_state_t st;
    qm_preset_defaults(QM_PRESET_DEADLOCK, &params);
    qm_state_init(&st, &params);

    const float dt = 1.0f / 100.0f;
    float initial_stamina = st.stamina;

    /* Trigger forward dash */
    qm_input_t in;
    memset(&in, 0, sizeof(in));
    in.dt_sec = dt;
    in.wish_forward = 1.0f;
    in.dash_pressed = 1;
    in.on_ground = 1;
    qm_step(&params, &in, &st, &st);

    CHECK(st.dashed == 1, "Deadlock dash triggers dashed event");
    CHECK(st.mode == QM_MODE_DASH, "Deadlock dash enters QM_MODE_DASH");
    CHECK(st.stamina < initial_stamina - 0.5f, "Deadlock dash consumes stamina");
    CHECK(qm_horiz_speed(st.vel) >= params.dash_impulse - 0.5f, "Deadlock dash reaches dash_impulse");

    /* Advance 0.12s (inside 0.06..0.36s dash-jump window) */
    for (int i = 0; i < 12; ++i) {
        memset(&in, 0, sizeof(in));
        in.dt_sec = dt;
        in.wish_forward = 1.0f;
        in.on_ground = 1;
        qm_step(&params, &in, &st, &st);
    }
    float pre_dj_spd = qm_horiz_speed(st.vel);

    /* Timed dash-jump */
    memset(&in, 0, sizeof(in));
    in.dt_sec = dt;
    in.wish_forward = 1.0f;
    in.jump_pressed = 1;
    in.jump_held = 1;
    in.on_ground = 1;
    qm_step(&params, &in, &st, &st);

    CHECK(st.dash_jumped == 1, "Deadlock timed jump triggers dash_jumped burst");
    CHECK(qm_horiz_speed(st.vel) > pre_dj_spd + params.dash_jump_boost * 0.8f, "Deadlock dash-jump adds forward burst");
    CHECK(st.vel[2] > params.jump_speed + params.dash_jump_up_bonus * 0.5f, "Deadlock dash-jump adds vertical bonus");
}

static void test_dt_and_nan_safety(void) {
    qm_params_t params;
    qm_state_t st;
    qm_preset_defaults(QM_PRESET_QUAKE_VQ3, &params);
    qm_state_init(&st, &params);

    st.pos[0] = 1.0f;
    st.pos[1] = 2.0f;
    st.pos[2] = 3.0f;
    st.vel[0] = 4.0f;
    st.vel[1] = 5.0f;
    st.vel[2] = 6.0f;

    /* dt <= 0 preserves state and returns cleanly */
    qm_input_t in;
    memset(&in, 0, sizeof(in));
    in.dt_sec = 0.0f;
    in.wish_forward = 1.0f;
    in.on_ground = 1;
    qm_step(&params, &in, &st, &st);
    CHECK(st.vel[0] == 4.0f && st.vel[1] == 5.0f && st.vel[2] == 6.0f, "dt == 0 preserves velocity");

    in.dt_sec = -0.5f;
    qm_step(&params, &in, &st, &st);
    CHECK(st.pos[0] == 1.0f && st.pos[1] == 2.0f && st.pos[2] == 3.0f, "dt < 0 preserves position");

    /* NaN inputs and corrupted state never output NaN */
    float nan_val = NAN;
    st.pos[0] = nan_val;
    st.vel[1] = nan_val;
    memset(&in, 0, sizeof(in));
    in.dt_sec = 0.016f;
    in.wish_forward = nan_val;
    in.wish_right = nan_val;
    in.cam_yaw_rad = nan_val;
    in.slope_normal[0] = nan_val;
    in.on_ground = 1;
    qm_step(&params, &in, &st, &st);

    CHECK(isfinite(st.pos[0]) && isfinite(st.pos[1]) && isfinite(st.pos[2]), "NaN state/inputs yield finite pos");
    CHECK(isfinite(st.vel[0]) && isfinite(st.vel[1]) && isfinite(st.vel[2]), "NaN state/inputs yield finite vel");
    CHECK(isfinite(st.horiz_speed), "NaN state/inputs yield finite horiz_speed");

    /* NaN dt is rejected safely */
    in.dt_sec = nan_val;
    qm_step(&params, &in, &st, &st);
    CHECK(isfinite(st.vel[0]) && isfinite(st.vel[1]) && isfinite(st.vel[2]), "NaN dt yields finite vel");
}

int main(void) {
    test_vq3_strafe_jump_speed_gain();
    test_cpma_air_control_turning();
    test_half_life_low_cap_bhop();
    test_apex_slide_slope_and_tap_strafe();
    test_deadlock_dash_and_dash_jump();
    test_dt_and_nan_safety();

    if (g_failures > 0) {
        fprintf(stderr, "qm_physics tests: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("qm_physics tests: ALL PASSED (6 suites)\n");
    return 0;
}
