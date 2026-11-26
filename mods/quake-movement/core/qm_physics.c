/*
 * qm_physics.c - Pure-C11 Quake (VQ3 & CPMA), Half-Life, Apex Legends, Deadlock & Custom
 *                on-foot movement physics solver in right-handed Z-up meters.
 */

#include "qm_physics.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#define QM_MAX_DT_SEC       0.1f
#define QM_MIN_EPS          1e-6f
#define QM_MAX_FALL_SPEED   60.0f
#define QM_MAX_HORIZ_SPEED  60.0f

static int qm_strieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return (*a == '\0' && *b == '\0');
}

static float qm_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int qm_isfinite3(const float v[3]) {
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

const char* qm_preset_name(qm_preset_id_t preset) {
    switch (preset) {
    case QM_PRESET_QUAKE_VQ3:    return "Quake VQ3";
    case QM_PRESET_QUAKE_CPMA:   return "Quake CPMA";
    case QM_PRESET_HALF_LIFE:    return "Half-Life";
    case QM_PRESET_APEX_LEGENDS: return "Apex Legends";
    case QM_PRESET_DEADLOCK:     return "Deadlock";
    case QM_PRESET_CUSTOM:       return "Custom";
    default:                     return "Unknown";
    }
}

const char* qm_preset_key(qm_preset_id_t preset) {
    switch (preset) {
    case QM_PRESET_QUAKE_VQ3:    return "quake_vq3";
    case QM_PRESET_QUAKE_CPMA:   return "quake_cpma";
    case QM_PRESET_HALF_LIFE:    return "half_life";
    case QM_PRESET_APEX_LEGENDS: return "apex_legends";
    case QM_PRESET_DEADLOCK:     return "deadlock";
    case QM_PRESET_CUSTOM:       return "custom";
    default:                     return "quake_vq3";
    }
}

const char* qm_mode_name(qm_move_mode_t mode) {
    switch (mode) {
    case QM_MODE_GROUND: return "GROUND";
    case QM_MODE_AIR:    return "AIR";
    case QM_MODE_SLIDE:  return "SLIDE";
    case QM_MODE_DASH:   return "DASH";
    default:             return "GROUND";
    }
}

int qm_preset_from_name(const char* name, qm_preset_id_t* out_preset) {
    if (!name || !out_preset) {
        return 0;
    }
    if (qm_strieq(name, "vq3") || qm_strieq(name, "quake_vq3") || qm_strieq(name, "quake") || qm_strieq(name, "q3")) {
        *out_preset = QM_PRESET_QUAKE_VQ3;
        return 1;
    }
    if (qm_strieq(name, "cpma") || qm_strieq(name, "quake_cpma") || qm_strieq(name, "cpm")) {
        *out_preset = QM_PRESET_QUAKE_CPMA;
        return 1;
    }
    if (qm_strieq(name, "halflife") || qm_strieq(name, "half_life") || qm_strieq(name, "hl") || qm_strieq(name, "source")) {
        *out_preset = QM_PRESET_HALF_LIFE;
        return 1;
    }
    if (qm_strieq(name, "apex") || qm_strieq(name, "apex_legends")) {
        *out_preset = QM_PRESET_APEX_LEGENDS;
        return 1;
    }
    if (qm_strieq(name, "deadlock") || qm_strieq(name, "dl")) {
        *out_preset = QM_PRESET_DEADLOCK;
        return 1;
    }
    if (qm_strieq(name, "custom")) {
        *out_preset = QM_PRESET_CUSTOM;
        return 1;
    }
    return 0;
}

void qm_preset_defaults(qm_preset_id_t preset, qm_params_t* p) {
    if (!p) {
        return;
    }
    memset(p, 0, sizeof(*p));

    /* Safe baseline shared defaults */
    p->max_speed = 9.5f;
    p->ground_accel = 10.0f;
    p->air_accel = 1.0f;
    p->air_wish_cap = 9.5f;
    p->air_control = 0.0f;
    p->air_stop_accel = 0.0f;
    p->cpm_strafe_accel = 0.0f;
    p->cpm_strafe_wish_cap = 0.0f;
    p->friction = 6.0f;
    p->stop_speed = 2.5f;
    p->gravity = 20.0f;
    p->jump_speed = 7.0f;
    p->crouch_jump_boost = 0.0f;
    p->coyote_time_sec = 0.08f;
    p->jump_buffer_sec = 0.10f;
    p->auto_bhop = 1;

    p->slide_enabled = 0;
    p->slide_min_speed = 4.5f;
    p->slide_exit_speed = 1.8f;
    p->slide_boost = 3.5f;
    p->slide_friction = 0.85f;
    p->slide_steer_accel = 2.5f;
    p->slide_slope_accel = 16.0f;
    p->slide_max_speed = 17.0f;
    p->slide_cooldown_sec = 1.2f;
    p->slide_jump_boost = 1.8f;

    p->tap_strafe_enabled = 0;
    p->tap_strafe_window_sec = 0.45f;
    p->tap_strafe_strength = 0.50f;

    p->dash_enabled = 0;
    p->dash_max_stamina = 3;
    p->dash_stamina_cost = 1.0f;
    p->dash_recharge_sec = 5.0f;
    p->dash_impulse = 13.5f;
    p->dash_duration_sec = 0.28f;
    p->dash_jump_window_start_sec = 0.06f;
    p->dash_jump_window_end_sec = 0.36f;
    p->dash_jump_boost = 4.2f;
    p->dash_jump_up_bonus = 1.0f;

    p->wall_bounce_enabled = 0;
    p->wall_bounce_probe_dist = 0.90f;
    p->wall_bounce_push = 6.5f;
    p->wall_bounce_up = 6.2f;
    p->wall_bounce_cooldown = 0.35f;

    switch (preset) {
    case QM_PRESET_QUAKE_VQ3:
        /* Classic Q3A strafe-jumping: crisp ground accel/friction, uncapped air wishspeed */
        p->max_speed = 9.5f;
        p->ground_accel = 10.0f;
        p->air_accel = 1.0f;
        p->air_wish_cap = 9.5f;
        p->friction = 6.0f;
        p->stop_speed = 2.5f;
        p->gravity = 20.0f;
        p->jump_speed = 7.0f;
        p->auto_bhop = 1;
        break;

    case QM_PRESET_QUAKE_CPMA:
        /* VQ3 strafe-jumping + CPMA snappy ground + tight A/D & W air-control turning */
        p->max_speed = 10.0f;
        p->ground_accel = 15.0f;
        p->air_accel = 1.0f;
        p->air_wish_cap = 10.0f;
        p->air_control = 6.0f;
        p->air_stop_accel = 2.5f;
        p->cpm_strafe_accel = 70.0f;
        p->cpm_strafe_wish_cap = 1.05f;
        p->friction = 8.0f;
        p->stop_speed = 2.5f;
        p->gravity = 20.0f;
        p->jump_speed = 7.2f;
        p->coyote_time_sec = 0.08f;
        p->jump_buffer_sec = 0.12f;
        p->auto_bhop = 1;
        break;

    case QM_PRESET_HALF_LIFE:
        /* GoldSrc / Source low air-wishspeed cap, high air-accel bunnyhop & crouch-jump */
        p->max_speed = 8.5f;
        p->ground_accel = 10.0f;
        p->air_accel = 120.0f;
        p->air_wish_cap = 0.95f;
        p->friction = 4.0f;
        p->stop_speed = 2.5f;
        p->gravity = 18.5f;
        p->jump_speed = 6.6f;
        p->crouch_jump_boost = 0.55f;
        p->coyote_time_sec = 0.06f;
        p->jump_buffer_sec = 0.10f;
        p->auto_bhop = 1;
        break;

    case QM_PRESET_APEX_LEGENDS:
        /* Sprint-to-crouch slide boost, downhill slope surfing, tap-strafe air-lurch, wall-bounce */
        p->max_speed = 7.5f;
        p->ground_accel = 11.0f;
        p->air_accel = 45.0f;
        p->air_wish_cap = 1.10f;
        p->friction = 5.5f;
        p->stop_speed = 2.2f;
        p->gravity = 19.0f;
        p->jump_speed = 6.8f;
        p->coyote_time_sec = 0.10f;
        p->jump_buffer_sec = 0.12f;
        p->auto_bhop = 0;

        p->slide_enabled = 1;
        p->slide_min_speed = 4.5f;
        p->slide_exit_speed = 1.8f;
        p->slide_boost = 3.8f;
        p->slide_friction = 0.45f;
        p->slide_steer_accel = 2.5f;
        p->slide_slope_accel = 22.0f;
        p->slide_max_speed = 17.0f;
        p->slide_cooldown_sec = 1.2f;
        p->slide_jump_boost = 1.8f;

        p->tap_strafe_enabled = 1;
        p->tap_strafe_window_sec = 0.45f;
        p->tap_strafe_strength = 0.55f;

        p->wall_bounce_enabled = 1;
        p->wall_bounce_probe_dist = 0.90f;
        p->wall_bounce_push = 6.5f;
        p->wall_bounce_up = 6.2f;
        p->wall_bounce_cooldown = 0.35f;
        break;

    case QM_PRESET_DEADLOCK:
        /* Directional ground/air dash, 3 stamina pips, timed dash-jump burst, downhill crouch-slide */
        p->max_speed = 7.2f;
        p->ground_accel = 12.0f;
        p->air_accel = 35.0f;
        p->air_wish_cap = 1.10f;
        p->friction = 6.5f;
        p->stop_speed = 2.2f;
        p->gravity = 19.6f;
        p->jump_speed = 6.5f;
        p->coyote_time_sec = 0.10f;
        p->jump_buffer_sec = 0.12f;
        p->auto_bhop = 0;

        p->slide_enabled = 1;
        p->slide_min_speed = 4.0f;
        p->slide_exit_speed = 1.6f;
        p->slide_boost = 1.5f;
        p->slide_friction = 0.60f;
        p->slide_steer_accel = 2.8f;
        p->slide_slope_accel = 20.0f;
        p->slide_max_speed = 16.0f;
        p->slide_cooldown_sec = 0.9f;
        p->slide_jump_boost = 1.2f;

        p->dash_enabled = 1;
        p->dash_max_stamina = 3;
        p->dash_stamina_cost = 1.0f;
        p->dash_recharge_sec = 5.0f;
        p->dash_impulse = 13.5f;
        p->dash_duration_sec = 0.28f;
        p->dash_jump_window_start_sec = 0.06f;
        p->dash_jump_window_end_sec = 0.36f;
        p->dash_jump_boost = 4.2f;
        p->dash_jump_up_bonus = 1.0f;

        p->wall_bounce_enabled = 1;
        p->wall_bounce_probe_dist = 0.90f;
        p->wall_bounce_push = 5.5f;
        p->wall_bounce_up = 5.8f;
        p->wall_bounce_cooldown = 0.35f;
        break;

    case QM_PRESET_CUSTOM:
    default:
        /* Hybrid sandbox preset exposing all mechanics for live tuning */
        p->max_speed = 9.0f;
        p->ground_accel = 12.0f;
        p->air_accel = 20.0f;
        p->air_wish_cap = 2.0f;
        p->air_control = 2.5f;
        p->friction = 6.0f;
        p->stop_speed = 2.5f;
        p->gravity = 19.6f;
        p->jump_speed = 7.0f;
        p->auto_bhop = 1;

        p->slide_enabled = 1;
        p->slide_min_speed = 4.2f;
        p->slide_exit_speed = 1.6f;
        p->slide_boost = 3.0f;
        p->slide_friction = 0.55f;
        p->slide_steer_accel = 2.5f;
        p->slide_slope_accel = 20.0f;
        p->slide_max_speed = 18.0f;
        p->slide_cooldown_sec = 1.0f;
        p->slide_jump_boost = 1.5f;

        p->tap_strafe_enabled = 1;
        p->tap_strafe_window_sec = 0.40f;
        p->tap_strafe_strength = 0.45f;

        p->dash_enabled = 1;
        p->dash_max_stamina = 3;
        p->dash_stamina_cost = 1.0f;
        p->dash_recharge_sec = 4.5f;
        p->dash_impulse = 13.0f;
        p->dash_duration_sec = 0.28f;
        p->dash_jump_window_start_sec = 0.06f;
        p->dash_jump_window_end_sec = 0.36f;
        p->dash_jump_boost = 3.5f;
        p->dash_jump_up_bonus = 0.8f;

        p->wall_bounce_enabled = 1;
        break;
    }
}

void qm_state_init(qm_state_t* state, const qm_params_t* params) {
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    if (params && params->dash_max_stamina > 0) {
        state->stamina = (float)params->dash_max_stamina;
    }
    state->mode = QM_MODE_GROUND;
}

float qm_horiz_speed(const float vel[3]) {
    if (!vel) {
        return 0.0f;
    }
    return hypotf(vel[0], vel[1]);
}

void qm_yaw_basis(float cam_yaw_rad, float out_forward[2], float out_right[2]) {
    float s = sinf(cam_yaw_rad);
    float c = cosf(cam_yaw_rad);
    if (out_forward) {
        out_forward[0] = -s;
        out_forward[1] = c;
    }
    if (out_right) {
        out_right[0] = c;
        out_right[1] = s;
    }
}

/* Classic Quake PM_Friction on horizontal XY velocity */
static void qm_apply_friction(float vel[3], float friction, float stop_speed, float dt) {
    float speed = hypotf(vel[0], vel[1]);
    if (speed < 1e-4f) {
        vel[0] = 0.0f;
        vel[1] = 0.0f;
        return;
    }
    float control = (speed < stop_speed) ? stop_speed : speed;
    float drop = control * friction * dt;
    float new_speed = speed - drop;
    if (new_speed < 0.0f) {
        new_speed = 0.0f;
    }
    float scale = new_speed / speed;
    vel[0] *= scale;
    vel[1] *= scale;
}

/* Classic Quake/Source PM_Accelerate / PM_AirAccelerate */
static void qm_accelerate(
    float       vel[3],
    const float wishdir[2],
    float       wishspeed_cap,
    float       wishspeed_accel,
    float       accel,
    float       dt)
{
    if (wishspeed_cap <= 0.0f || accel <= 0.0f) {
        return;
    }
    float current_speed = vel[0] * wishdir[0] + vel[1] * wishdir[1];
    float add_speed = wishspeed_cap - current_speed;
    if (add_speed <= 0.0f) {
        return;
    }
    float accel_speed = accel * dt * wishspeed_accel;
    if (accel_speed > add_speed) {
        accel_speed = add_speed;
    }
    vel[0] += accel_speed * wishdir[0];
    vel[1] += accel_speed * wishdir[1];
}

/* CPMA CPM_AirControl: rotates horizontal velocity toward wishdir while preserving speed */
static void qm_cpm_air_control(
    float       vel[3],
    const float wishdir[2],
    float       air_control,
    float       dt)
{
    if (air_control <= 0.0f) {
        return;
    }
    float speed = hypotf(vel[0], vel[1]);
    if (speed < 0.25f) {
        return;
    }
    float dir_x = vel[0] / speed;
    float dir_y = vel[1] / speed;
    float dot = dir_x * wishdir[0] + dir_y * wishdir[1];
    if (dot <= 0.0f) {
        return;
    }
    float k = air_control * dot * dot * dt;
    float nx = dir_x * speed + wishdir[0] * (k * speed);
    float ny = dir_y * speed + wishdir[1] * (k * speed);
    float norm = hypotf(nx, ny);
    if (norm > QM_MIN_EPS) {
        vel[0] = (nx / norm) * speed;
        vel[1] = (ny / norm) * speed;
    }
}

/* Try wall-bounce via optional surface normal probe callback */
static int qm_try_wall_bounce(
    const qm_params_t* params,
    const qm_input_t*  input,
    const float        wishdir[2],
    int                has_wish,
    qm_state_t*        st)
{
    if (!params->wall_bounce_enabled || !input->wall_probe || st->wall_bounce_timer > 0.0f) {
        return 0;
    }

    float probe_dirs[4][3];
    int dir_count = 0;

    float hspd = hypotf(st->vel[0], st->vel[1]);
    if (hspd > 0.5f) {
        probe_dirs[dir_count][0] = st->vel[0] / hspd;
        probe_dirs[dir_count][1] = st->vel[1] / hspd;
        probe_dirs[dir_count][2] = 0.0f;
        ++dir_count;
    }
    if (has_wish) {
        probe_dirs[dir_count][0] = wishdir[0];
        probe_dirs[dir_count][1] = wishdir[1];
        probe_dirs[dir_count][2] = 0.0f;
        ++dir_count;
    }
    float fwd[2], right[2];
    qm_yaw_basis(input->cam_yaw_rad, fwd, right);
    probe_dirs[dir_count][0] = right[0];
    probe_dirs[dir_count][1] = right[1];
    probe_dirs[dir_count][2] = 0.0f;
    ++dir_count;
    probe_dirs[dir_count][0] = -right[0];
    probe_dirs[dir_count][1] = -right[1];
    probe_dirs[dir_count][2] = 0.0f;
    ++dir_count;

    float max_dist = (params->wall_bounce_probe_dist > 0.1f) ? params->wall_bounce_probe_dist : 0.9f;
    for (int i = 0; i < dir_count; ++i) {
        qm_wall_probe_result_t hit;
        memset(&hit, 0, sizeof(hit));
        if (!input->wall_probe(st->pos, probe_dirs[i], max_dist, &hit, input->wall_probe_ctx)) {
            continue;
        }
        if (!hit.hit || !qm_isfinite3(hit.normal)) {
            continue;
        }
        float nlen = sqrtf(hit.normal[0] * hit.normal[0] + hit.normal[1] * hit.normal[1] + hit.normal[2] * hit.normal[2]);
        if (nlen < QM_MIN_EPS) {
            continue;
        }
        float nx = hit.normal[0] / nlen;
        float ny = hit.normal[1] / nlen;
        float nz = hit.normal[2] / nlen;
        /* Only bounce off near-vertical walls (not floors or ceilings) */
        if (fabsf(nz) > 0.35f) {
            continue;
        }
        float nhlen = hypotf(nx, ny);
        if (nhlen < QM_MIN_EPS) {
            continue;
        }
        nx /= nhlen;
        ny /= nhlen;

        /* Remove inward component into the wall and apply outward push + vertical launch */
        float vn = st->vel[0] * nx + st->vel[1] * ny;
        if (vn < 0.0f) {
            st->vel[0] -= vn * nx;
            st->vel[1] -= vn * ny;
        }
        float push = fmaxf(params->wall_bounce_push, -vn);
        st->vel[0] += nx * push;
        st->vel[1] += ny * push;
        st->vel[2] = fmaxf(st->vel[2], params->wall_bounce_up);
        st->wall_bounce_timer = params->wall_bounce_cooldown;
        st->wall_bounced = 1;
        if (params->tap_strafe_enabled) {
            st->tap_strafe_timer = params->tap_strafe_window_sec;
        }
        return 1;
    }
    return 0;
}

void qm_step(
    const qm_params_t* params,
    const qm_input_t*  input,
    const qm_state_t*  in_state,
    qm_state_t*        out_state)
{
    if (!params || !input || !in_state || !out_state) {
        return;
    }

    qm_state_t st = *in_state;
    /* Clear per-step event flags */
    st.jumped = 0;
    st.slide_started = 0;
    st.slide_jumped = 0;
    st.tap_strafed = 0;
    st.dashed = 0;
    st.dash_jumped = 0;
    st.wall_bounced = 0;

    if (!qm_isfinite3(st.pos)) {
        st.pos[0] = st.pos[1] = st.pos[2] = 0.0f;
    }
    if (!qm_isfinite3(st.vel)) {
        st.vel[0] = st.vel[1] = st.vel[2] = 0.0f;
    }

    float dt = input->dt_sec;
    if (!isfinite(dt) || dt <= 0.0f) {
        st.horiz_speed = qm_horiz_speed(st.vel);
        *out_state = st;
        return;
    }
    if (dt > QM_MAX_DT_SEC) {
        dt = QM_MAX_DT_SEC;
    }

    /* Normalize surface slope normal (default flat ground +Z) */
    float slope_n[3] = { 0.0f, 0.0f, 1.0f };
    if (qm_isfinite3(input->slope_normal)) {
        float slen = sqrtf(
            input->slope_normal[0] * input->slope_normal[0] +
            input->slope_normal[1] * input->slope_normal[1] +
            input->slope_normal[2] * input->slope_normal[2]);
        if (slen > QM_MIN_EPS) {
            slope_n[0] = input->slope_normal[0] / slen;
            slope_n[1] = input->slope_normal[1] / slen;
            slope_n[2] = input->slope_normal[2] / slen;
        }
    }

    /* Camera horizontal basis & wish direction */
    float fwd[2], right[2];
    qm_yaw_basis(isfinite(input->cam_yaw_rad) ? input->cam_yaw_rad : 0.0f, fwd, right);

    float wf = qm_clampf(isfinite(input->wish_forward) ? input->wish_forward : 0.0f, -1.0f, 1.0f);
    float wr = qm_clampf(isfinite(input->wish_right) ? input->wish_right : 0.0f, -1.0f, 1.0f);
    float wx = fwd[0] * wf + right[0] * wr;
    float wy = fwd[1] * wf + right[1] * wr;
    float wlen = hypotf(wx, wy);
    float wishdir[2] = { 0.0f, 0.0f };
    int has_wish = 0;
    float wish_scale = 0.0f;
    if (wlen > 1e-4f) {
        wishdir[0] = wx / wlen;
        wishdir[1] = wy / wlen;
        wish_scale = (wlen > 1.0f) ? 1.0f : wlen;
        has_wish = 1;
    }
    float wishspeed = params->max_speed * wish_scale;

    /* Tick persistent cooldowns & stamina recharge */
    if (st.slide_cooldown > 0.0f) {
        st.slide_cooldown = fmaxf(0.0f, st.slide_cooldown - dt);
    }
    if (st.wall_bounce_timer > 0.0f) {
        st.wall_bounce_timer = fmaxf(0.0f, st.wall_bounce_timer - dt);
    }
    if (st.tap_strafe_timer > 0.0f) {
        st.tap_strafe_timer = fmaxf(0.0f, st.tap_strafe_timer - dt);
    }
    if (params->dash_enabled && params->dash_max_stamina > 0) {
        float max_stam = (float)params->dash_max_stamina;
        if (st.stamina < max_stam && params->dash_recharge_sec > QM_MIN_EPS) {
            st.stamina = fminf(max_stam, st.stamina + (dt / params->dash_recharge_sec));
        } else if (st.stamina > max_stam) {
            st.stamina = max_stam;
        }
    }

    /* Advance dash timers */
    if (st.dash_timer > 0.0f) {
        st.dash_timer = fmaxf(0.0f, st.dash_timer - dt);
    }
    if (st.dash_active_window) {
        st.dash_elapsed += dt;
        if (st.dash_elapsed > params->dash_jump_window_end_sec + 0.05f && st.dash_timer <= 0.0f) {
            st.dash_active_window = 0;
        }
    }

    /* Coyote time & jump input buffer */
    int on_ground = input->on_ground ? 1 : 0;
    if (on_ground) {
        st.coyote_timer = params->coyote_time_sec;
    } else if (st.coyote_timer > 0.0f) {
        st.coyote_timer = fmaxf(0.0f, st.coyote_timer - dt);
    }

    if (input->jump_pressed) {
        st.jump_buffer_timer = fmaxf(params->jump_buffer_sec, dt);
    } else if (st.jump_buffer_timer > 0.0f) {
        st.jump_buffer_timer = fmaxf(0.0f, st.jump_buffer_timer - dt);
    }

    /* Directional ground/air dash (Deadlock / Custom) */
    if (params->dash_enabled && input->dash_pressed && st.dash_timer <= 0.0f &&
        st.stamina + 1e-4f >= params->dash_stamina_cost) {
        st.stamina = fmaxf(0.0f, st.stamina - params->dash_stamina_cost);
        float ddir_x = has_wish ? wishdir[0] : fwd[0];
        float ddir_y = has_wish ? wishdir[1] : fwd[1];
        float hspd = hypotf(st.vel[0], st.vel[1]);
        float target_spd = fmaxf(hspd, params->dash_impulse);
        st.vel[0] = ddir_x * target_spd;
        st.vel[1] = ddir_y * target_spd;
        if (!on_ground && st.vel[2] < 0.0f) {
            st.vel[2] = 0.0f;
        }
        st.dash_dir[0] = ddir_x;
        st.dash_dir[1] = ddir_y;
        st.dash_timer = params->dash_duration_sec;
        st.dash_elapsed = 0.0f;
        st.dash_active_window = 1;
        st.dash_jumped_this_dash = 0;
        st.sliding = 0;
        st.dashed = 1;
    }

    /* Jump check: grounded/coyote jump with buffer or auto-bhop */
    int can_ground_jump = on_ground || (st.coyote_timer > 0.0f);
    int want_jump = (st.jump_buffer_timer > 0.0f) || (params->auto_bhop && input->jump_held && on_ground);
    int did_jump = 0;

    if (can_ground_jump && want_jump) {
        did_jump = 1;
        st.jumped = 1;
        st.coyote_timer = 0.0f;
        st.jump_buffer_timer = 0.0f;
        on_ground = 0; /* Leaves ground immediately -> skips ground friction for clean bhop */

        float launch_vz = params->jump_speed;
        if (input->crouch_held && params->crouch_jump_boost > 0.0f) {
            launch_vz += params->crouch_jump_boost;
        }

        /* Slide-jump momentum burst (Apex / Deadlock) */
        if (st.sliding) {
            float hspd = hypotf(st.vel[0], st.vel[1]);
            if (hspd > 0.25f && params->slide_jump_boost > 0.0f) {
                float boost_cap = (params->slide_max_speed > 0.0f) ? (params->slide_max_speed + params->slide_jump_boost) : QM_MAX_HORIZ_SPEED;
                float next_spd = fminf(hspd + params->slide_jump_boost, boost_cap);
                st.vel[0] = (st.vel[0] / hspd) * next_spd;
                st.vel[1] = (st.vel[1] / hspd) * next_spd;
            }
            st.sliding = 0;
            st.slide_jumped = 1;
        }

        /* Timed dash-jump momentum burst (Deadlock) */
        if (params->dash_enabled && st.dash_active_window && !st.dash_jumped_this_dash &&
            st.dash_elapsed >= params->dash_jump_window_start_sec &&
            st.dash_elapsed <= params->dash_jump_window_end_sec) {
            float dir_x = st.dash_dir[0];
            float dir_y = st.dash_dir[1];
            float hspd = hypotf(st.vel[0], st.vel[1]);
            if (hspd > 0.25f) {
                dir_x = st.vel[0] / hspd;
                dir_y = st.vel[1] / hspd;
            }
            st.vel[0] += dir_x * params->dash_jump_boost;
            st.vel[1] += dir_y * params->dash_jump_boost;
            launch_vz += params->dash_jump_up_bonus;
            st.dash_timer = 0.0f;
            st.dash_active_window = 0;
            st.dash_jumped_this_dash = 1;
            st.dash_jumped = 1;
        }

        st.vel[2] = launch_vz;
        if (params->tap_strafe_enabled) {
            st.tap_strafe_timer = params->tap_strafe_window_sec;
        }
    } else if (!on_ground && input->jump_pressed) {
        /* Airborne jump press: optional wall-bounce hook */
        if (qm_try_wall_bounce(params, input, wishdir, has_wish, &st)) {
            st.jump_buffer_timer = 0.0f;
        }
    }

    /* Ground vs. Air locomotion */
    if (on_ground) {
        if (st.vel[2] < 0.0f) {
            st.vel[2] = 0.0f;
        }

        float hspd = hypotf(st.vel[0], st.vel[1]);

        /* Enter / maintain crouch-slide (Apex / Deadlock / Custom) */
        if (params->slide_enabled && input->crouch_held && st.dash_timer <= 0.0f) {
            if (!st.sliding && hspd >= params->slide_min_speed) {
                st.sliding = 1;
                st.slide_started = 1;
                if (st.slide_cooldown <= 0.0f && params->slide_boost > 0.0f && hspd > QM_MIN_EPS) {
                    float dir_x = st.vel[0] / hspd;
                    float dir_y = st.vel[1] / hspd;
                    float boosted = fminf(hspd + params->slide_boost, params->slide_max_speed);
                    st.vel[0] = dir_x * boosted;
                    st.vel[1] = dir_y * boosted;
                    st.slide_cooldown = params->slide_cooldown_sec;
                }
            }
        } else {
            st.sliding = 0;
        }

        if (st.sliding) {
            /* Low friction + mild steering + downhill slope acceleration */
            qm_apply_friction(st.vel, params->slide_friction, params->stop_speed * 0.5f, dt);

            if (has_wish && params->slide_steer_accel > 0.0f) {
                qm_accelerate(st.vel, wishdir, params->max_speed * 0.5f, params->max_speed, params->slide_steer_accel, dt);
            }

            /* Downhill slope tangent acceleration from surface normal */
            float sin_slope = hypotf(slope_n[0], slope_n[1]);
            if (slope_n[2] > 0.25f && slope_n[2] < 0.995f && sin_slope > 0.02f && params->slide_slope_accel > 0.0f) {
                float down_x = slope_n[0] / sin_slope;
                float down_y = slope_n[1] / sin_slope;
                float slope_dv = params->slide_slope_accel * sin_slope * dt;
                st.vel[0] += down_x * slope_dv;
                st.vel[1] += down_y * slope_dv;
            }

            float cur_spd = hypotf(st.vel[0], st.vel[1]);
            if (params->slide_max_speed > 0.0f && cur_spd > params->slide_max_speed) {
                float s = params->slide_max_speed / cur_spd;
                st.vel[0] *= s;
                st.vel[1] *= s;
                cur_spd = params->slide_max_speed;
            }
            if (cur_spd < params->slide_exit_speed) {
                st.sliding = 0;
            }
        } else {
            float eff_friction = (st.dash_timer > 0.0f) ? (params->friction * 0.2f) : params->friction;
            qm_apply_friction(st.vel, eff_friction, params->stop_speed, dt);
            if (has_wish) {
                float ground_cap = wishspeed;
                if (input->crouch_held && !params->slide_enabled) {
                    ground_cap *= 0.6f;
                } else if (input->crouch_held && params->slide_enabled) {
                    ground_cap *= 0.55f;
                }
                qm_accelerate(st.vel, wishdir, ground_cap, ground_cap, params->ground_accel, dt);
            }
        }
    } else {
        /* Airborne physics */
        st.sliding = 0;

        if (has_wish) {
            /* Apex tap-strafe / air-lurch window right after jump */
            if (params->tap_strafe_enabled && st.tap_strafe_timer > 0.0f && !did_jump) {
                float hspd = hypotf(st.vel[0], st.vel[1]);
                if (hspd > 0.5f) {
                    float vx_n = st.vel[0] / hspd;
                    float vy_n = st.vel[1] / hspd;
                    float align = vx_n * wishdir[0] + vy_n * wishdir[1];
                    if (align < 0.999f) {
                        float blend = qm_clampf(params->tap_strafe_strength * dt * 16.0f, 0.0f, 0.85f);
                        float nx = vx_n * (1.0f - blend) + wishdir[0] * blend;
                        float ny = vy_n * (1.0f - blend) + wishdir[1] * blend;
                        float nlen = hypotf(nx, ny);
                        if (nlen > QM_MIN_EPS) {
                            /* Preserve momentum across sharp lurches (tiny 0.8% decay on extreme >120 deg turns) */
                            float preserve = (align < -0.5f) ? 0.992f : 1.0f;
                            st.vel[0] = (nx / nlen) * (hspd * preserve);
                            st.vel[1] = (ny / nlen) * (hspd * preserve);
                            st.tap_strafed = 1;
                        }
                    }
                }
            }

            /* CPMA pure A/D strafe accel vs standard VQ3/Source PM_AirAccelerate */
            int pure_strafe = (fabsf(wf) < 1e-3f && fabsf(wr) > 1e-3f);
            int pure_fwd = (fabsf(wf) > 1e-3f && fabsf(wr) < 1e-3f);
            float eff_accel = params->air_accel;
            float eff_cap = (params->air_wish_cap > 0.0f && params->air_wish_cap < wishspeed)
                ? params->air_wish_cap
                : wishspeed;

            if (pure_strafe && params->cpm_strafe_accel > 0.0f && params->cpm_strafe_wish_cap > 0.0f) {
                eff_accel = params->cpm_strafe_accel;
                eff_cap = fminf(wishspeed, params->cpm_strafe_wish_cap);
            } else if (params->air_stop_accel > 0.0f) {
                float dot = st.vel[0] * wishdir[0] + st.vel[1] * wishdir[1];
                if (dot < 0.0f) {
                    eff_accel = params->air_stop_accel;
                }
            }

            qm_accelerate(st.vel, wishdir, eff_cap, wishspeed, eff_accel, dt);

            /* CPMA air-control turning (pure W/S or pure A/D turning without losing momentum) */
            if (params->air_control > 0.0f && (pure_fwd || pure_strafe)) {
                qm_cpm_air_control(st.vel, wishdir, params->air_control, dt);
            }
        }

        /* Gravity (reduced during active air-dash for crisp horizontal dash arc) */
        float eff_gravity = (st.dash_timer > 0.0f) ? (params->gravity * 0.25f) : params->gravity;
        st.vel[2] -= eff_gravity * dt;
        if (st.vel[2] < -QM_MAX_FALL_SPEED) {
            st.vel[2] = -QM_MAX_FALL_SPEED;
        }
    }

    /* Safety velocity clamp */
    float final_hspd = hypotf(st.vel[0], st.vel[1]);
    if (final_hspd > QM_MAX_HORIZ_SPEED) {
        float s = QM_MAX_HORIZ_SPEED / final_hspd;
        st.vel[0] *= s;
        st.vel[1] *= s;
        final_hspd = QM_MAX_HORIZ_SPEED;
    }

    /* Integrate position for standalone/predictive use */
    st.pos[0] += st.vel[0] * dt;
    st.pos[1] += st.vel[1] * dt;
    st.pos[2] += st.vel[2] * dt;

    /* Track last wish direction */
    if (has_wish) {
        st.last_wish_dir[0] = wishdir[0];
        st.last_wish_dir[1] = wishdir[1];
        st.has_last_wish = 1;
    } else {
        st.has_last_wish = 0;
    }

    /* Update mode & telemetry */
    st.horiz_speed = final_hspd;
    if (final_hspd > st.peak_speed) {
        st.peak_speed = final_hspd;
    }
    if (st.dash_timer > 0.0f) {
        st.mode = QM_MODE_DASH;
    } else if (st.sliding) {
        st.mode = QM_MODE_SLIDE;
    } else if (on_ground) {
        st.mode = QM_MODE_GROUND;
    } else {
        st.mode = QM_MODE_AIR;
    }

    *out_state = st;
}
