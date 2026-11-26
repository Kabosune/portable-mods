/*
 * weighty_aim.c - Pure-C11 stick response, turn boost, curve graph data, and GyroWiki gyro aim.
 */

#include "weighty_aim.h"

#include <math.h>
#include <string.h>

#define WA_GYRO_CALIB_SECONDS 1.5f
#define WA_GYRO_STILL_DEG     2.0f
#define WA_GYRO_STILL_DELAY   1.0f
#define WA_GYRO_YAW_RELAX     1.41f
#define WA_BOOST_RELEASE_SEC  0.1f

static float wa_clampf(float v, float lo, float hi) {
    if (!isfinite(v)) {
        return lo;
    }
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float wa_finite_or(float v, float fallback) {
    return isfinite(v) ? v : fallback;
}

static void wa_effective_deadzones(const wa_stick_cfg_t* cfg, float* out_inner, float* out_outer) {
    float inner = wa_clampf(cfg->inner_deadzone, 0.0f, 0.40f);
    float outer = wa_clampf(cfg->outer_deadzone, 0.10f, 1.00f);
    if (outer < inner + 0.05f) {
        outer = inner + 0.05f;
        if (outer > 1.0f) {
            outer = 1.0f;
        }
    }
    *out_inner = inner;
    *out_outer = outer;
}

const char* wa_curve_name(wa_curve_t curve) {
    switch (curve) {
    case WA_CURVE_CLASSIC:  return "Classic";
    case WA_CURVE_LINEAR:   return "Linear";
    case WA_CURVE_BALANCED: return "Balanced";
    case WA_CURVE_CUSTOM1:  return "Custom 1";
    case WA_CURVE_CUSTOM2:  return "Custom 2";
    case WA_CURVE_CUSTOM3:  return "Custom 3";
    default:                return "Classic";
    }
}

const char* wa_boost_mode_name(wa_boost_mode_t mode) {
    switch (mode) {
    case WA_BOOST_OFF:     return "Off";
    case WA_BOOST_INSTANT: return "Instant";
    case WA_BOOST_RAMPED:  return "Ramped";
    default:               return "Off";
    }
}

const char* wa_gyro_mode_name(wa_gyro_mode_t mode) {
    switch (mode) {
    case WA_GYRO_OFF:    return "Off";
    case WA_GYRO_ALWAYS: return "Always On";
    case WA_GYRO_AIMING: return "While Aiming";
    default:             return "Off";
    }
}

const char* wa_gyro_space_name(wa_gyro_space_t space) {
    switch (space) {
    case WA_GYRO_SPACE_PLAYER: return "Player";
    case WA_GYRO_SPACE_LOCAL:  return "Local";
    default:                   return "Player";
    }
}

int wa_is_custom_curve(wa_curve_t curve) {
    return curve >= WA_CURVE_CUSTOM1 && curve <= WA_CURVE_CUSTOM3;
}

void wa_stick_cfg_defaults(wa_stick_cfg_t* cfg) {
    static const float default_bezier[4] = { 0.4f, 0.0f, 0.75f, 1.0f };
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->curve = WA_CURVE_CLASSIC;
    cfg->inner_deadzone = 0.01f;
    cfg->outer_deadzone = 0.95f;
    memcpy(cfg->bezier, default_bezier, sizeof(default_bezier));
    for (int i = 0; i < WA_NUM_CUSTOM_CURVES; ++i) {
        memcpy(cfg->bezier_profiles[i], default_bezier, sizeof(default_bezier));
    }
    cfg->last_custom_curve = 0;
    cfg->game_builtin_deadzone = 1;
    cfg->sensitivity = 1.0f;
    cfg->vertical_sens = 0.80f;

    cfg->boost_mode = WA_BOOST_RAMPED;
    cfg->boost_amount = 2.0f;
    cfg->boost_threshold = 0.9f;
    cfg->boost_delay = 0.1f;
    cfg->boost_time = 0.25f;
    cfg->boost_vertical = 0.35f;
}

void wa_gyro_cfg_defaults(wa_gyro_cfg_t* cfg) {
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = WA_GYRO_OFF;
    cfg->sensitivity = 2.5f;
    cfg->vertical = 0.90f;
    cfg->space = WA_GYRO_SPACE_PLAYER;
    cfg->invert_y = 0;
    cfg->acceleration = 1.0f;
    cfg->accel_threshold = 75.0f;
    cfg->tightening = 0.0f;
    cfg->smoothing = 4.0f;
    cfg->auto_calibrate = 1;
    cfg->pause_with_stick = 0;
}

void wa_stick_state_init(wa_stick_state_t* st) {
    if (!st) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

void wa_gyro_state_init(wa_gyro_state_t* st) {
    if (!st) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

void wa_player_init(wa_player_t* player) {
    if (!player) {
        return;
    }
    wa_stick_cfg_defaults(&player->stick_cfg);
    wa_stick_state_init(&player->stick_state);
    wa_gyro_cfg_defaults(&player->gyro_cfg);
    wa_gyro_state_init(&player->gyro_state);
}

void wa_reset_stick_response(wa_stick_cfg_t* cfg) {
    if (!cfg) {
        return;
    }
    wa_stick_cfg_t defaults;
    wa_stick_cfg_defaults(&defaults);

    /* Reset curve, deadzones, and sensitivity; preserve boost and custom profiles */
    cfg->curve = defaults.curve;
    cfg->inner_deadzone = defaults.inner_deadzone;
    cfg->outer_deadzone = defaults.outer_deadzone;
    cfg->sensitivity = defaults.sensitivity;
    cfg->vertical_sens = defaults.vertical_sens;
    cfg->game_builtin_deadzone = defaults.game_builtin_deadzone;
}

void wa_reset_boost(wa_stick_cfg_t* cfg) {
    if (!cfg) {
        return;
    }
    wa_stick_cfg_t defaults;
    wa_stick_cfg_defaults(&defaults);

    cfg->boost_mode = defaults.boost_mode;
    cfg->boost_amount = defaults.boost_amount;
    cfg->boost_threshold = defaults.boost_threshold;
    cfg->boost_delay = defaults.boost_delay;
    cfg->boost_time = defaults.boost_time;
    cfg->boost_vertical = defaults.boost_vertical;
}

void wa_reset_gyro(wa_gyro_cfg_t* cfg) {
    if (!cfg) {
        return;
    }
    /* Keep current mode so resetting tuning does not switch gyro off */
    wa_gyro_mode_t mode = cfg->mode;
    wa_gyro_cfg_defaults(cfg);
    cfg->mode = mode;
}

void wa_select_curve(wa_stick_cfg_t* cfg, wa_curve_t curve) {
    if (!cfg) {
        return;
    }
    cfg->curve = curve;
    if (wa_is_custom_curve(curve)) {
        int slot = (int)(curve - WA_CURVE_CUSTOM1);
        memcpy(cfg->bezier, cfg->bezier_profiles[slot], sizeof(cfg->bezier));
        cfg->last_custom_curve = slot;
    }
}

void wa_curve_points_changed(wa_stick_cfg_t* cfg) {
    if (!cfg) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        cfg->bezier[i] = wa_clampf(cfg->bezier[i], 0.0f, 1.0f);
    }
    if (!wa_is_custom_curve(cfg->curve)) {
        int slot = cfg->last_custom_curve;
        if (slot < 0 || slot >= WA_NUM_CUSTOM_CURVES) {
            slot = 0;
        }
        cfg->curve = (wa_curve_t)(WA_CURVE_CUSTOM1 + slot);
    }
    int slot = (int)(cfg->curve - WA_CURVE_CUSTOM1);
    memcpy(cfg->bezier_profiles[slot], cfg->bezier, sizeof(cfg->bezier));
    cfg->last_custom_curve = slot;
}

float wa_bezier_eval(const float pts[4], float x) {
    if (!pts) {
        return wa_clampf(x, 0.0f, 1.0f);
    }
    float target = wa_clampf(x, 0.0f, 1.0f);
    float x1 = wa_clampf(pts[0], 0.0f, 1.0f);
    float y1 = wa_clampf(pts[1], 0.0f, 1.0f);
    float x2 = wa_clampf(pts[2], 0.0f, 1.0f);
    float y2 = wa_clampf(pts[3], 0.0f, 1.0f);

    float lo = 0.0f;
    float hi = 1.0f;
    float t = target;

    for (int i = 0; i < 24; ++i) {
        float u = 1.0f - t;
        float xt = 3.0f * u * u * t * x1 + 3.0f * u * t * t * x2 + t * t * t;
        if (xt < target) {
            lo = t;
        } else {
            hi = t;
        }
        t = 0.5f * (lo + hi);
    }

    float u = 1.0f - t;
    return 3.0f * u * u * t * y1 + 3.0f * u * t * t * y2 + t * t * t;
}

static float wa_eval_curve_unit(const wa_stick_cfg_t* cfg, float n) {
    switch (cfg->curve) {
    case WA_CURVE_LINEAR:
        return n;
    case WA_CURVE_CUSTOM1:
    case WA_CURVE_CUSTOM2:
    case WA_CURVE_CUSTOM3:
        return wa_bezier_eval(cfg->bezier, n);
    case WA_CURVE_BALANCED:
    default:
        return n * sqrtf(n);
    }
}

float wa_curve_output(const wa_stick_cfg_t* cfg, float deflection) {
    if (!cfg) {
        return 0.0f;
    }
    float d = wa_clampf(deflection, 0.0f, 1.0f);
    float sens = wa_clampf(cfg->sensitivity, 0.25f, 2.5f);

    if (cfg->curve == WA_CURVE_CLASSIC) {
        return d * sens;
    }

    float lo, hi;
    wa_effective_deadzones(cfg, &lo, &hi);
    float n = wa_clampf((d - lo) / (hi - lo), 0.0f, 1.0f);
    return wa_eval_curve_unit(cfg, n) * sens;
}

int wa_game_builtin_deadzone_wanted(const wa_stick_cfg_t* cfg, int force_original_aim) {
    if (force_original_aim) {
        return 1;
    }
    if (!cfg) {
        return 1;
    }
    return cfg->game_builtin_deadzone != 0 ? 1 : 0;
}

static void wa_apply_boost(
    const wa_stick_cfg_t* cfg,
    wa_stick_state_t*     st,
    float                 deflection,
    float                 dt_sec,
    float                 out[2])
{
    if (cfg->boost_mode == WA_BOOST_OFF) {
        st->boost_held = 0.0f;
        st->boost_level = 0.0f;
        return;
    }

    float thresh = wa_clampf(cfg->boost_threshold, 0.3f, 1.0f);
    if (deflection >= thresh - 0.0001f) {
        float goal = 1.0f;
        st->boost_held += dt_sec;

        if (cfg->boost_mode == WA_BOOST_RAMPED) {
            float delay = wa_clampf(cfg->boost_delay, 0.0f, 5.0f);
            float ramp_time = wa_clampf(cfg->boost_time, 0.01f, 5.0f);
            goal = wa_clampf((st->boost_held - delay) / ramp_time, 0.0f, 1.0f);
            goal = goal * goal * (3.0f - 2.0f * goal); /* smoothstep */
        }

        if (goal > st->boost_level) {
            st->boost_level = goal;
        }
    } else {
        st->boost_held = 0.0f;
        st->boost_level = wa_clampf(st->boost_level - dt_sec / WA_BOOST_RELEASE_SEC, 0.0f, 1.0f);
    }

    float amount = wa_clampf(cfg->boost_amount, 1.0f, 4.0f);
    float extra = (amount - 1.0f) * st->boost_level;
    float vert_share = wa_clampf(cfg->boost_vertical, 0.0f, 1.0f);

    out[0] *= 1.0f + extra;
    out[1] *= 1.0f + extra * vert_share;
}

void wa_process_stick(
    const wa_stick_cfg_t* cfg,
    wa_stick_state_t*     state,
    float                 stick_x,
    float                 stick_y,
    float                 dt_sec,
    int                   force_original_aim,
    float                 out_rate[2])
{
    if (!out_rate) {
        return;
    }
    out_rate[0] = 0.0f;
    out_rate[1] = 0.0f;

    if (!cfg || !state) {
        return;
    }
    if (!isfinite(dt_sec) || dt_sec <= 0.0f) {
        /* Paused: return zero output and preserve state */
        return;
    }

    float rx = wa_finite_or(stick_x, 0.0f);
    float ry = wa_finite_or(stick_y, 0.0f);

    if (force_original_aim) {
        /* 1:1 unmodded pass-through without mutating saved settings */
        state->boost_held = 0.0f;
        state->boost_level = 0.0f;
        out_rate[0] = rx;
        out_rate[1] = ry;
        return;
    }

    float sens = wa_clampf(cfg->sensitivity, 0.25f, 2.5f);
    float vert = wa_clampf(cfg->vertical_sens, 0.25f, 2.0f);
    float mag = hypotf(rx, ry);

    if (cfg->curve == WA_CURVE_CLASSIC) {
        out_rate[0] = rx * sens;
        out_rate[1] = ry * sens * vert;
        wa_apply_boost(cfg, state, wa_clampf(mag, 0.0f, 1.0f), dt_sec, out_rate);
        return;
    }

    if (mag < 1e-4f) {
        out_rate[0] = 0.0f;
        out_rate[1] = 0.0f;
        wa_apply_boost(cfg, state, 0.0f, dt_sec, out_rate);
        return;
    }

    float lo, hi;
    wa_effective_deadzones(cfg, &lo, &hi);
    float n = wa_clampf((mag - lo) / (hi - lo), 0.0f, 1.0f);
    float o = wa_eval_curve_unit(cfg, n) * sens;

    out_rate[0] = (rx / mag) * o;
    out_rate[1] = (ry / mag) * o * vert;
    wa_apply_boost(cfg, state, n, dt_sec, out_rate);
}

void wa_build_curve_graph(
    const wa_stick_cfg_t* cfg,
    float                 current_stick_mag,
    int                   sample_count,
    wa_curve_graph_t*     out_graph)
{
    if (!cfg || !out_graph) {
        return;
    }
    memset(out_graph, 0, sizeof(*out_graph));

    if (sample_count < 2) {
        sample_count = 2;
    } else if (sample_count > WA_GRAPH_MAX_SAMPLES) {
        sample_count = WA_GRAPH_MAX_SAMPLES;
    }

    float sens = wa_clampf(cfg->sensitivity, 0.25f, 2.5f);
    float y_max = (sens > 1.0f) ? sens : 1.0f;
    int classic = (cfg->curve == WA_CURVE_CLASSIC);

    float lo = 0.0f;
    float hi = 1.0f;
    if (!classic) {
        wa_effective_deadzones(cfg, &lo, &hi);
    }

    out_graph->y_max = y_max;
    out_graph->inner_deadzone_x = lo;
    out_graph->outer_deadzone_x = hi;
    out_graph->grid_ticks[0] = 0.25f;
    out_graph->grid_ticks[1] = 0.50f;
    out_graph->grid_ticks[2] = 0.75f;
    out_graph->sample_count = sample_count;

    for (int i = 0; i < sample_count; ++i) {
        float x = (float)i / (float)(sample_count - 1);
        float raw = wa_curve_output(cfg, x);
        out_graph->curve_raw[i] = raw;
        out_graph->curve_norm[i] = wa_clampf(raw / y_max, 0.0f, 1.0f);
        out_graph->linear_norm[i] = wa_clampf(x / y_max, 0.0f, 1.0f);
    }

    if (wa_is_custom_curve(cfg->curve)) {
        out_graph->has_custom_handles = 1;
        out_graph->anchor0[0] = lo;
        out_graph->anchor0[1] = 0.0f;
        out_graph->control1[0] = lo + wa_clampf(cfg->bezier[0], 0.0f, 1.0f) * (hi - lo);
        out_graph->control1[1] = wa_clampf(wa_clampf(cfg->bezier[1], 0.0f, 1.0f) * sens / y_max, 0.0f, 1.0f);
        out_graph->control2[0] = lo + wa_clampf(cfg->bezier[2], 0.0f, 1.0f) * (hi - lo);
        out_graph->control2[1] = wa_clampf(wa_clampf(cfg->bezier[3], 0.0f, 1.0f) * sens / y_max, 0.0f, 1.0f);
        out_graph->anchor1[0] = hi;
        out_graph->anchor1[1] = wa_clampf(sens / y_max, 0.0f, 1.0f);
    }

    float live = wa_clampf(current_stick_mag, 0.0f, 1.0f);
    if (live > 0.02f) {
        out_graph->has_live_marker = 1;
        out_graph->live_x = live;
        out_graph->live_y = wa_clampf(wa_curve_output(cfg, live) / y_max, 0.0f, 1.0f);
    }
}

void wa_gyro_start_calibration(wa_gyro_state_t* state) {
    if (!state) {
        return;
    }
    state->calib_time = WA_GYRO_CALIB_SECONDS;
    state->calib_sum[0] = 0.0f;
    state->calib_sum[1] = 0.0f;
    state->calib_sum[2] = 0.0f;
    state->calib_count = 0;
}

wa_gyro_status_t wa_gyro_status(const wa_gyro_state_t* state) {
    if (!state || !state->available) {
        return WA_GYRO_STATUS_NOT_FOUND;
    }
    if (state->calib_time > 0.0f) {
        return WA_GYRO_STATUS_CALIBRATING;
    }
    return state->calibrated ? WA_GYRO_STATUS_READY : WA_GYRO_STATUS_UNCALIBRATED;
}

const char* wa_gyro_status_text(const wa_gyro_state_t* state) {
    switch (wa_gyro_status(state)) {
    case WA_GYRO_STATUS_NOT_FOUND:
        return "No gyro found on this controller";
    case WA_GYRO_STATUS_CALIBRATING:
        return "Calibrating... keep the controller still";
    case WA_GYRO_STATUS_READY:
        return "Gyro ready (calibrated)";
    case WA_GYRO_STATUS_UNCALIBRATED:
    default:
        return "Gyro found - calibrate for best results";
    }
}

static int wa_gyro_sample(
    const wa_gyro_cfg_t*    cfg,
    wa_gyro_state_t*        st,
    const wa_gyro_sensor_t* sensor,
    float                   dt_sec,
    float                   out_rate[3])
{
    if (!sensor || !sensor->available) {
        st->available = 0;
        return 0;
    }

    st->available = 1;

    float raw[3] = {
        wa_finite_or(sensor->gyro_deg_s[0], 0.0f),
        wa_finite_or(sensor->gyro_deg_s[1], 0.0f),
        wa_finite_or(sensor->gyro_deg_s[2], 0.0f)
    };
    float accel[3] = {
        wa_finite_or(sensor->accel[0], 0.0f),
        wa_finite_or(sensor->accel[1], 0.0f),
        wa_finite_or(sensor->accel[2], 0.0f)
    };

    /* Smoothed gravity vector (tau = 0.1 s) for Player space */
    float alen = sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
    if (alen > 0.1f) {
        float k = 1.0f - expf(-dt_sec / 0.1f);
        for (int i = 0; i < 3; ++i) {
            st->grav[i] += (accel[i] / alen - st->grav[i]) * k;
        }
    }

    /* Manual calibration: average raw gyro over 1.5 s */
    if (st->calib_time > 0.0f) {
        for (int i = 0; i < 3; ++i) {
            st->calib_sum[i] += raw[i];
        }
        st->calib_count++;
        st->calib_time -= dt_sec;

        if (st->calib_time <= 0.0f) {
            st->calib_time = 0.0f;
            if (st->calib_count > 0) {
                for (int i = 0; i < 3; ++i) {
                    st->bias[i] = st->calib_sum[i] / (float)st->calib_count;
                }
                st->calibrated = 1;
            }
        }
    } else if (cfg->auto_calibrate) {
        /* Auto-Calibrate When Still: all axes < 2 deg/s from bias for > 1 s -> tau = 1 s */
        float dx = fabsf(raw[0] - st->bias[0]);
        float dy = fabsf(raw[1] - st->bias[1]);
        float dz = fabsf(raw[2] - st->bias[2]);

        if (dx < WA_GYRO_STILL_DEG && dy < WA_GYRO_STILL_DEG && dz < WA_GYRO_STILL_DEG) {
            st->still_time += dt_sec;
        } else {
            st->still_time = 0.0f;
        }

        if (st->still_time > WA_GYRO_STILL_DELAY) {
            float k = 1.0f - expf(-dt_sec / 1.0f);
            for (int i = 0; i < 3; ++i) {
                st->bias[i] += (raw[i] - st->bias[i]) * k;
            }
            st->calibrated = 1;
        }
    }

    for (int i = 0; i < 3; ++i) {
        out_rate[i] = raw[i] - st->bias[i];
    }
    return 1;
}

void wa_gyro_menu_tick(
    const wa_gyro_cfg_t*    cfg,
    wa_gyro_state_t*        state,
    const wa_gyro_sensor_t* sensor,
    float                   real_dt_sec)
{
    if (!cfg || !state) {
        return;
    }
    float dt = real_dt_sec;
    if (!isfinite(dt) || dt <= 0.0f || dt > 0.25f) {
        dt = 1.0f / 60.0f;
    }
    float rate[3];
    (void)wa_gyro_sample(cfg, state, sensor, dt, rate);
}

void wa_process_gyro(
    const wa_gyro_cfg_t*    cfg,
    wa_gyro_state_t*        state,
    const wa_gyro_sensor_t* sensor,
    int                     aiming,
    int                     stick_moving,
    float                   dt_sec,
    int                     force_original_aim,
    float                   out_deg[2])
{
    if (!out_deg) {
        return;
    }
    out_deg[0] = 0.0f;
    out_deg[1] = 0.0f;

    if (!cfg || !state) {
        return;
    }
    if (!isfinite(dt_sec) || dt_sec <= 0.0f) {
        /* Paused: return zero output and preserve state */
        return;
    }

    if (force_original_aim || cfg->mode == WA_GYRO_OFF) {
        state->smooth[0] = 0.0f;
        state->smooth[1] = 0.0f;
        return;
    }

    float rate[3];
    if (!wa_gyro_sample(cfg, state, sensor, dt_sec, rate)) {
        state->smooth[0] = 0.0f;
        state->smooth[1] = 0.0f;
        return;
    }

    if (state->calib_time > 0.0f ||
        (cfg->mode == WA_GYRO_AIMING && !aiming) ||
        (cfg->pause_with_stick && stick_moving)) {
        state->smooth[0] = 0.0f;
        state->smooth[1] = 0.0f;
        return;
    }

    /* Pitch is always the controller's local pitch */
    float pitch = rate[0];
    float yaw = rate[1];

    if (cfg->space == WA_GYRO_SPACE_PLAYER &&
        (state->grav[0] != 0.0f || state->grav[1] != 0.0f || state->grav[2] != 0.0f)) {
        float world_yaw = rate[1] * state->grav[1] + rate[2] * state->grav[2];
        float len = hypotf(rate[1], rate[2]);
        float mag = fabsf(world_yaw) * WA_GYRO_YAW_RELAX;
        float clamped = (mag < len) ? mag : len;
        if (world_yaw > 0.0f) {
            yaw = clamped;
        } else if (world_yaw < 0.0f) {
            yaw = -clamped;
        } else {
            yaw = 0.0f;
        }
    }

    float speed = hypotf(yaw, pitch);

    /* (a) Tightening: linear scale down below threshold (no hard cutoff) */
    float tightening = wa_clampf(cfg->tightening, 0.0f, 30.0f);
    if (tightening > 0.0f && speed < tightening) {
        float k = speed / tightening;
        yaw *= k;
        pitch *= k;
    }

    /* (b) Soft tiered smoothing: full < thresh/2, direct >= thresh, linear blend between */
    float smoothing = wa_clampf(cfg->smoothing, 0.0f, 30.0f);
    if (smoothing > 0.0f) {
        float half = smoothing * 0.5f;
        float direct = wa_clampf((speed - half) / half, 0.0f, 1.0f);
        float k = 1.0f - expf(-dt_sec / 0.1f);

        state->smooth[0] += (yaw - state->smooth[0]) * k;
        state->smooth[1] += (pitch - state->smooth[1]) * k;

        yaw = yaw * direct + state->smooth[0] * (1.0f - direct);
        pitch = pitch * direct + state->smooth[1] * (1.0f - direct);
    }

    /* (c) Acceleration: scale sensitivity linearly up to accel_threshold */
    float sens = wa_clampf(cfg->sensitivity, 0.1f, 20.0f);
    float accel = wa_clampf(cfg->acceleration, 1.0f, 4.0f);
    if (accel > 1.0f) {
        float accel_thresh = wa_clampf(cfg->accel_threshold, 5.0f, 500.0f);
        sens *= 1.0f + (accel - 1.0f) * wa_clampf(speed / accel_thresh, 0.0f, 1.0f);
    }

    /* (d) Output degrees: +yaw = right (SDL +y is left), +pitch = up */
    float vert = wa_clampf(cfg->vertical, 0.0f, 2.0f);
    float invert = cfg->invert_y ? -1.0f : 1.0f;

    out_deg[0] = -yaw * sens * dt_sec;
    out_deg[1] = pitch * sens * vert * dt_sec * invert;
}
