/*
 * free_aim.c - Pure-C11 unlocked free-aim / dynamic reticle implementation.
 */

#include "free_aim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FA_PI                     3.14159265358979323846f
#define FA_DEG2RAD(d)             ((d) * (FA_PI / 180.0f))
#define FA_SPRING_SUBSTEP         (1.0f / 240.0f)
#define FA_SPRING_MAX_SUBSTEPS    64
#define FA_DEFAULT_FULL_STICK_DPS 210.0f /* 3.5 deg/tick * 60 ticks/sec */
#define FA_ASSIST_TAU             0.35f

static float fa_clampf(float v, float lo, float hi) {
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

static float fa_finite_or(float v, float fallback) {
    return isfinite(v) ? v : fallback;
}

static float fa_smoothstep(float t) {
    float c = fa_clampf(t, 0.0f, 1.0f);
    return c * c * (3.0f - 2.0f * c);
}

const char* fa_preset_name(fa_preset_t preset) {
    switch (preset) {
    case FA_PRESET_CLASSIC:   return "Classic";
    case FA_PRESET_WEIGHTY:   return "Weighty";
    case FA_PRESET_IMMERSIVE: return "Immersive";
    case FA_PRESET_ARCADE:    return "Arcade";
    case FA_PRESET_BORING:    return "Boring";
    case FA_PRESET_CUSTOM1:   return "Custom 1";
    case FA_PRESET_CUSTOM2:   return "Custom 2";
    case FA_PRESET_CUSTOM3:   return "Custom 3";
    default:                  return "Classic";
    }
}

const char* fa_input_source_name(fa_input_source_t source) {
    switch (source) {
    case FA_INPUT_NONE:  return "None";
    case FA_INPUT_MOUSE: return "Mouse";
    case FA_INPUT_GYRO:  return "Gyro";
    case FA_INPUT_STICK: return "Stick";
    default:             return "None";
    }
}

int fa_preset_is_custom(fa_preset_t preset) {
    return preset >= FA_PRESET_CUSTOM1 && preset <= FA_PRESET_CUSTOM3;
}

int fa_preset_persists_edits(fa_preset_t preset) {
    return fa_preset_is_custom(preset);
}

void fa_reticle_defaults(int arcade_profile, fa_reticle_cfg_t* out_reticle) {
    if (!out_reticle) {
        return;
    }
    out_reticle->show_hip = 1;
    out_reticle->show_ads = 1;
    if (arcade_profile) {
        out_reticle->size = 3.0f;
        out_reticle->opacity = 80.0f / 255.0f; /* 0x50 / 0xff */
        out_reticle->smooth_reticle = 1;
    } else {
        out_reticle->size = 2.0f;
        out_reticle->opacity = 40.0f / 255.0f; /* 0x28 / 0xff */
        out_reticle->smooth_reticle = 0;
    }
}

void fa_preset_defaults(fa_preset_t preset, fa_cfg_t* out_cfg) {
    if (!out_cfg) {
        return;
    }
    memset(out_cfg, 0, sizeof(*out_cfg));

    /* Base Weighty defaults */
    out_cfg->preset          = FA_PRESET_WEIGHTY;
    out_cfg->zone_x          = 7.5f;
    out_cfg->zone_y          = 4.5f;
    out_cfg->camera_share    = 0.5f;
    out_cfg->camera_lead     = 0.8f;
    out_cfg->recenter_speed  = 1.0f;
    out_cfg->recenter_delay  = 0.25f;
    out_cfg->recenter_smooth = 0.4f;
    out_cfg->reticle_speed   = 1.0f;
    out_cfg->gun_response    = 8.5f;
    out_cfg->gun_damping     = 0.7f;
    out_cfg->turn_drag       = 0.35f;
    out_cfg->edge_smoothing  = 0.1f;
    out_cfg->camera_sway     = 0.08f;
    out_cfg->walk_sway       = 0.25f;
    out_cfg->ads_zoom        = 1.3f;
    out_cfg->ads_time        = 0.18f;
    out_cfg->ads_sway        = 0.35f;
    out_cfg->ads_zone        = 1.0f;
    out_cfg->ads_sens        = 0.65f;
    out_cfg->ads_height      = -4.0f;
    out_cfg->edge_turn_speed = 0.0f;
    out_cfg->edge_band       = 0.2f;
    out_cfg->edge_influence  = 0.25f;
    out_cfg->edge_vertical   = 0.4f;
    out_cfg->edge_mouse      = 1;
    out_cfg->edge_gyro       = 1;
    out_cfg->edge_stick      = 0;
    out_cfg->reticle_profile = 0;
    fa_reticle_defaults(0, &out_cfg->reticle);

    switch (preset) {
    case FA_PRESET_CLASSIC:
        out_cfg->preset = FA_PRESET_CLASSIC;
        break;

    case FA_PRESET_WEIGHTY:
        break;

    case FA_PRESET_IMMERSIVE:
        out_cfg->preset          = FA_PRESET_IMMERSIVE;
        out_cfg->zone_x          = 11.0f;
        out_cfg->zone_y          = 6.5f;
        out_cfg->camera_share    = 0.5f;
        out_cfg->camera_lead     = 1.4f;
        out_cfg->recenter_speed  = 1.3f;
        out_cfg->recenter_delay  = 0.15f;
        out_cfg->recenter_smooth = 0.6f;
        out_cfg->gun_response    = 4.6f;
        out_cfg->gun_damping     = 0.52f;
        out_cfg->turn_drag       = 0.6f;
        out_cfg->edge_smoothing  = 0.2f;
        out_cfg->camera_sway     = 0.32f;
        out_cfg->walk_sway       = 0.8f;
        out_cfg->ads_zoom        = 1.25f;
        out_cfg->ads_time        = 0.26f;
        out_cfg->ads_sway        = 0.5f;
        out_cfg->ads_zone        = 1.0f;
        out_cfg->ads_sens        = 0.65f;
        out_cfg->reticle.show_hip = 0;
        out_cfg->reticle.show_ads = 0;
        break;

    case FA_PRESET_ARCADE:
        out_cfg->preset          = FA_PRESET_ARCADE;
        out_cfg->zone_x          = 28.0f;
        out_cfg->zone_y          = 18.0f;
        out_cfg->camera_share    = 0.0f;
        out_cfg->camera_lead     = 0.0f;
        out_cfg->recenter_speed  = 0.0f;
        out_cfg->recenter_delay  = 0.25f;
        out_cfg->recenter_smooth = 0.4f;
        out_cfg->gun_response    = 14.0f;
        out_cfg->gun_damping     = 0.85f;
        out_cfg->turn_drag       = 0.1f;
        out_cfg->edge_smoothing  = 0.08f;
        out_cfg->camera_sway     = 0.03f;
        out_cfg->walk_sway       = 0.1f;
        out_cfg->ads_zoom        = 1.3f;
        out_cfg->ads_time        = 0.18f;
        out_cfg->ads_sway        = 0.35f;
        out_cfg->ads_zone        = 1.0f;
        out_cfg->ads_sens        = 0.65f;
        out_cfg->edge_turn_speed = 120.0f;
        out_cfg->edge_band       = 0.3f;
        out_cfg->edge_influence  = 0.8f;
        out_cfg->edge_vertical   = 0.4f;
        out_cfg->edge_mouse      = 1;
        out_cfg->edge_gyro       = 1;
        out_cfg->edge_stick      = 0;
        out_cfg->reticle_profile = 1;
        fa_reticle_defaults(1, &out_cfg->reticle);
        break;

    case FA_PRESET_BORING:
        out_cfg->preset          = FA_PRESET_BORING;
        out_cfg->zone_x          = 0.0f;
        out_cfg->zone_y          = 0.0f;
        out_cfg->camera_share    = 1.0f;
        out_cfg->camera_lead     = 0.0f;
        out_cfg->recenter_speed  = 0.0f;
        out_cfg->recenter_delay  = 0.0f;
        out_cfg->recenter_smooth = 0.0f;
        out_cfg->gun_response    = 20.0f;
        out_cfg->gun_damping     = 1.0f;
        out_cfg->turn_drag       = 0.0f;
        out_cfg->edge_smoothing  = 0.0f;
        out_cfg->camera_sway     = 0.0f;
        out_cfg->walk_sway       = 0.0f;
        out_cfg->ads_zoom        = 1.35f;
        out_cfg->ads_time        = 0.12f;
        out_cfg->ads_sway        = 0.0f;
        out_cfg->ads_zone        = 1.0f;
        out_cfg->ads_sens        = 0.65f;
        break;

    case FA_PRESET_CUSTOM1:
    case FA_PRESET_CUSTOM2:
    case FA_PRESET_CUSTOM3:
        out_cfg->preset = preset;
        break;

    default:
        out_cfg->preset = FA_PRESET_CLASSIC;
        break;
    }
}

void fa_cfg_defaults(fa_cfg_t* cfg) {
    fa_preset_defaults(FA_PRESET_CLASSIC, cfg);
}

void fa_reset_state(fa_state_t* state) {
    if (!state) {
        return;
    }
    state->target[0] = 0.0f;
    state->target[1] = 0.0f;
    state->display[0] = 0.0f;
    state->display[1] = 0.0f;
    state->vel[0] = 0.0f;
    state->vel[1] = 0.0f;
    state->over[0] = 0.0f;
    state->over[1] = 0.0f;
    state->idle_time = 0.0f;
    state->lead_ramp = 0.0f;
    state->lead_input = 0.0f;
    state->last_input = FA_INPUT_NONE;
    state->edge_time = 0.0f;
    state->ads_blend = 0.0f;
    state->assist[0] = 0.0f;
    state->assist[1] = 0.0f;
    state->breath_phase = 0.0f;
    state->drift_phase = 0.0f;
    state->walk_phase = 0.0f;
    state->prev_sway[0] = 0.0f;
    state->prev_sway[1] = 0.0f;
    state->catchup_vel[0] = 0.0f;
    state->catchup_vel[1] = 0.0f;
    state->catchup_env = 0.0f;
    state->last_lead_rate = 0.0f;
    state->last_catchup_rate = 0.0f;
    state->last_zx = 0.0f;
    state->last_zy = 0.0f;
    state->edge_turn_active = 0;
    state->active = 0;
}

static void fa_sync_reticle_profile(fa_profile_bank_t* bank) {
    int want = bank->cfg.reticle_profile ? 1 : 0;
    int have = bank->reticle_live_profile ? 1 : 0;
    if (want == have) {
        return;
    }

    /* Save current profile's display appearance (size/opacity/smooth) */
    bank->reticle_profiles[have].size = bank->cfg.reticle.size;
    bank->reticle_profiles[have].opacity = bank->cfg.reticle.opacity;
    bank->reticle_profiles[have].smooth_reticle = bank->cfg.reticle.smooth_reticle;

    /* Load target profile's display appearance while keeping preset's show_hip/show_ads */
    bank->cfg.reticle.size = bank->reticle_profiles[want].size;
    bank->cfg.reticle.opacity = bank->reticle_profiles[want].opacity;
    bank->cfg.reticle.smooth_reticle = bank->reticle_profiles[want].smooth_reticle;
    bank->reticle_live_profile = want;
}

void fa_profile_bank_init(fa_profile_bank_t* bank) {
    if (!bank) {
        return;
    }
    memset(bank, 0, sizeof(*bank));
    fa_reticle_defaults(0, &bank->reticle_profiles[0]);
    fa_reticle_defaults(1, &bank->reticle_profiles[1]);
    bank->reticle_live_profile = 0;
    bank->last_custom = 0;

    for (int i = 0; i < FA_NUM_CUSTOM_PRESETS; ++i) {
        fa_preset_defaults(FA_PRESET_WEIGHTY, &bank->custom[i]);
        bank->custom[i].preset = (fa_preset_t)(FA_PRESET_CUSTOM1 + i);
    }

    fa_preset_defaults(FA_PRESET_CLASSIC, &bank->cfg);
}

void fa_profile_apply_preset(fa_profile_bank_t* bank, fa_preset_t preset) {
    if (!bank) {
        return;
    }

    /* Save live reticle appearance into its current profile before swapping */
    int live = bank->reticle_live_profile ? 1 : 0;
    bank->reticle_profiles[live].size = bank->cfg.reticle.size;
    bank->reticle_profiles[live].opacity = bank->cfg.reticle.opacity;
    bank->reticle_profiles[live].smooth_reticle = bank->cfg.reticle.smooth_reticle;

    if (fa_preset_is_custom(preset)) {
        int slot = (int)(preset - FA_PRESET_CUSTOM1);
        bank->cfg = bank->custom[slot];
        bank->cfg.preset = preset;
        bank->last_custom = slot;
    } else if (preset == FA_PRESET_CLASSIC) {
        /* Keep tuning values intact; Classic only switches free-aim off and uses normal reticle profile */
        bank->cfg.preset = FA_PRESET_CLASSIC;
        bank->cfg.reticle_profile = 0;
    } else {
        fa_preset_defaults(preset, &bank->cfg);
    }

    /* Apply the target profile's saved reticle appearance */
    int want = bank->cfg.reticle_profile ? 1 : 0;
    bank->cfg.reticle.size = bank->reticle_profiles[want].size;
    bank->cfg.reticle.opacity = bank->reticle_profiles[want].opacity;
    bank->cfg.reticle.smooth_reticle = bank->reticle_profiles[want].smooth_reticle;
    bank->reticle_live_profile = want;
}

void fa_profile_settings_changed(fa_profile_bank_t* bank) {
    if (!bank) {
        return;
    }
    if (!fa_preset_is_custom(bank->cfg.preset)) {
        int slot = bank->last_custom;
        if (slot < 0 || slot >= FA_NUM_CUSTOM_PRESETS) {
            slot = 0;
        }
        bank->cfg.preset = (fa_preset_t)(FA_PRESET_CUSTOM1 + slot);
    }

    int slot = (int)(bank->cfg.preset - FA_PRESET_CUSTOM1);
    bank->custom[slot] = bank->cfg;
    bank->last_custom = slot;
    fa_sync_reticle_profile(bank);
}

void fa_profile_on_load_refresh_builtin(fa_profile_bank_t* bank) {
    if (!bank) {
        return;
    }
    fa_preset_t p = bank->cfg.preset;
    if (p == FA_PRESET_WEIGHTY || p == FA_PRESET_IMMERSIVE ||
        p == FA_PRESET_ARCADE || p == FA_PRESET_BORING) {
        fa_profile_apply_preset(bank, p);
    }
}

float fa_fov_scale(float fov_deg, float ref_fov_deg) {
    float f = fa_clampf(fa_finite_or(fov_deg, 60.0f), 1.0f, 175.0f);
    float r = fa_clampf(fa_finite_or(ref_fov_deg, 60.0f), 1.0f, 175.0f);
    float denom = tanf(FA_DEG2RAD(r * 0.5f));
    if (denom <= 1e-6f) {
        return 1.0f;
    }
    return tanf(FA_DEG2RAD(f * 0.5f)) / denom;
}

static void fa_step_spring(fa_state_t* st, float gun_response, float gun_damping, float dt_sec) {
    float freq = fa_clampf(gun_response, 0.5f, 40.0f);
    float damp = fa_clampf(gun_damping, 0.05f, 3.0f);
    float w = 2.0f * FA_PI * freq;
    float k = w * w;
    float c = 2.0f * damp * w;

    int steps = (int)ceilf(dt_sec / FA_SPRING_SUBSTEP);
    if (steps < 1) {
        steps = 1;
    } else if (steps > FA_SPRING_MAX_SUBSTEPS) {
        steps = FA_SPRING_MAX_SUBSTEPS;
    }

    float h = dt_sec / (float)steps;
    for (int s = 0; s < steps; ++s) {
        for (int i = 0; i < 2; ++i) {
            float goal = st->target[i] + st->over[i] + st->assist[i];
            float acc = k * (goal - st->display[i]) - c * st->vel[i];
            st->vel[i] += acc * h;
            st->display[i] += st->vel[i] * h;
        }
    }
}

void fa_step(
    const fa_cfg_t*   cfg,
    fa_state_t*       state,
    const fa_input_t* in,
    int               force_original_aim,
    fa_output_t*      out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));

    if (!cfg || !state || !in) {
        return;
    }

    /* Pause / invalid dt guard: return zero camera/reticle deltas and preserve state */
    if (!isfinite(in->dt_sec) || in->dt_sec <= 0.0f) {
        out->reticle_deg[0] = state->display[0];
        out->reticle_deg[1] = state->display[1];
        out->reticle_delta_deg[0] = 0.0f;
        out->reticle_delta_deg[1] = 0.0f;
        out->ads_amount = fa_smoothstep(state->ads_blend);
        out->active = state->active;
        out->show_reticle = in->aiming ? cfg->reticle.show_ads : cfg->reticle.show_hip;
        return;
    }

    float dt = in->dt_sec;
    float stick_deg[2] = {
        fa_finite_or(in->stick_deg[0], 0.0f),
        fa_finite_or(in->stick_deg[1], 0.0f)
    };
    float mouse_deg[2] = {
        fa_finite_or(in->mouse_deg[0], 0.0f),
        fa_finite_or(in->mouse_deg[1], 0.0f)
    };
    float gyro_deg[2] = {
        fa_finite_or(in->gyro_deg[0], 0.0f),
        fa_finite_or(in->gyro_deg[1], 0.0f)
    };
    float stick_defl = fa_clampf(fa_finite_or(in->stick_deflection, 0.0f), 0.0f, 1.0f);
    float raw_total[2] = {
        stick_deg[0] + mouse_deg[0] + gyro_deg[0],
        stick_deg[1] + mouse_deg[1] + gyro_deg[1]
    };

    /* Global Force Original Aim bypass: 1:1 unmodded camera delta, zero reticle, no cfg mutation */
    if (force_original_aim) {
        fa_reset_state(state);
        out->cam_delta_deg[0] = in->gameplay_active ? raw_total[0] : 0.0f;
        out->cam_delta_deg[1] = in->gameplay_active ? raw_total[1] : 0.0f;
        out->reticle_deg[0] = 0.0f;
        out->reticle_deg[1] = 0.0f;
        out->reticle_delta_deg[0] = 0.0f;
        out->reticle_delta_deg[1] = 0.0f;
        out->ads_amount = 0.0f;
        out->show_reticle = 1;
        out->active = 0;
        return;
    }

    /* Classic preset (mod off) or inactive gameplay guard */
    if (cfg->preset == FA_PRESET_CLASSIC || !in->gameplay_active) {
        fa_reset_state(state);
        if (in->gameplay_active) {
            out->cam_delta_deg[0] = raw_total[0];
            out->cam_delta_deg[1] = raw_total[1];
        }
        out->reticle_deg[0] = 0.0f;
        out->reticle_deg[1] = 0.0f;
        out->reticle_delta_deg[0] = 0.0f;
        out->reticle_delta_deg[1] = 0.0f;
        out->ads_amount = 0.0f;
        out->show_reticle = in->aiming ? cfg->reticle.show_ads : cfg->reticle.show_hip;
        out->active = 0;
        return;
    }

    float prev_display[2] = { state->display[0], state->display[1] };

    /* 1. Track last_input (stick only claims last_input past 25% deflection) */
    {
        float sm = (stick_defl >= FA_EDGE_STICK_TILT)
            ? (fabsf(stick_deg[0]) + fabsf(stick_deg[1])) : 0.0f;
        float gm = fabsf(gyro_deg[0]) + fabsf(gyro_deg[1]);
        float mm = fabsf(mouse_deg[0]) + fabsf(mouse_deg[1]);

        if (sm + gm + mm > 0.0001f) {
            if (sm >= gm && sm >= mm) {
                state->last_input = FA_INPUT_STICK;
            } else if (gm >= mm) {
                state->last_input = FA_INPUT_GYRO;
            } else {
                state->last_input = FA_INPUT_MOUSE;
            }
        }
    }

    /* 2. ADS blend (linear 0->1 over ads_time; smoothstep when applied) & ads_sens scaling */
    {
        float ads_time = fa_clampf(fa_finite_or(cfg->ads_time, 0.18f), 0.01f, 5.0f);
        float goal = in->aiming ? 1.0f : 0.0f;
        float step = dt / ads_time;

        if (state->ads_blend < goal) {
            state->ads_blend = fa_clampf(state->ads_blend + step, 0.0f, goal);
        } else if (state->ads_blend > goal) {
            state->ads_blend = fa_clampf(state->ads_blend - step, goal, 1.0f);
        }
    }

    float ads = fa_smoothstep(state->ads_blend);
    float ads_sens = fa_clampf(fa_finite_or(cfg->ads_sens, 1.0f), 0.1f, 1.0f);
    float sens_mult = 1.0f + (ads_sens - 1.0f) * ads;

    float req_deg[2] = {
        raw_total[0] * sens_mult,
        raw_total[1] * sens_mult
    };

    /* 3. Effective ADS & FOV-scaled parameters */
    float fov_scale = (isfinite(in->fov_scale) && in->fov_scale > 0.0f) ? in->fov_scale : 1.0f;
    float align = fa_clampf(fa_finite_or(cfg->ads_zone, 1.0f), 0.0f, 2.0f);
    float ads_sway = fa_clampf(fa_finite_or(cfg->ads_sway, 1.0f), 0.0f, 1.0f);

    float eff_zx = fa_finite_or(cfg->zone_x, 0.0f) * (1.0f + (align - 1.0f) * ads) * fov_scale;
    float eff_zy = fa_finite_or(cfg->zone_y, 0.0f) * (1.0f + (align - 1.0f) * ads) * fov_scale;
    float eff_share = fa_finite_or(cfg->camera_share, 0.5f)
        + (1.0f - fa_finite_or(cfg->camera_share, 0.5f)) * ads * (1.0f - align);
    float eff_cam_sway = fa_finite_or(cfg->camera_sway, 0.0f) * (1.0f + (ads_sway - 1.0f) * ads);
    float eff_walk_sway = fa_finite_or(cfg->walk_sway, 0.0f) * (1.0f + (ads_sway - 1.0f) * ads);
    float eff_drag = fa_finite_or(cfg->turn_drag, 0.0f) * (1.0f - 0.6f * ads * (1.0f - align));
    float eff_gun_resp = fa_finite_or(cfg->gun_response, 8.5f) * (1.0f + 0.8f * ads * (1.0f - align));

    float zx = fa_clampf(eff_zx, 0.05f, 45.0f);
    float zy = fa_clampf(eff_zy, 0.05f, 45.0f);
    float share = fa_clampf(eff_share, 0.0f, 1.0f);
    float drag = fa_clampf(eff_drag, 0.0f, 1.0f);
    float rs = (cfg->reticle_speed > 0.001f)
        ? fa_clampf(fa_finite_or(cfg->reticle_speed, 1.0f), 0.25f, 3.0f) : 1.0f;

    float edge_turn_speed = fa_clampf(fa_finite_or(cfg->edge_turn_speed, 0.0f), 0.0f, 360.0f);
    int autoedge = (edge_turn_speed > 0.001f) && (
        (state->last_input == FA_INPUT_MOUSE && cfg->edge_mouse) ||
        (state->last_input == FA_INPUT_GYRO && cfg->edge_gyro) ||
        (state->last_input == FA_INPUT_STICK && cfg->edge_stick)
    );

    float cam_deg[2] = { 0.0f, 0.0f };
    float req_len = hypotf(req_deg[0], req_deg[1]);

    /* 4. Elliptical free-aim zone, Camera Share, Turn Drag & Overflow */
    if (req_len > 0.0001f) {
        for (int i = 0; i < 2; ++i) {
            float direct = req_deg[i] * share;
            cam_deg[i] += direct;
            state->display[i] -= direct * drag;
            state->target[i] += (req_deg[i] - direct) * rs;
        }

        float ex = state->target[0] / zx;
        float ey = state->target[1] / zy;
        float ellipse = ex * ex + ey * ey;
        if (ellipse > 1.0f) {
            float scale = 1.0f / sqrtf(ellipse);
            float keep = autoedge ? fa_clampf(fa_finite_or(cfg->edge_influence, 0.25f), 0.0f, 1.0f) : 1.0f;
            for (int i = 0; i < 2; ++i) {
                float excess = state->target[i] - state->target[i] * scale;
                state->over[i] += (excess * keep) / rs;
                state->target[i] *= scale;
            }
        }
    }

    /* Zone shrink (ADS or live settings change): push newly outside portion into overflow */
    {
        float ex = state->target[0] / zx;
        float ey = state->target[1] / zy;
        float ellipse = ex * ex + ey * ey;
        if (ellipse > 1.0f) {
            float scale = 1.0f / sqrtf(ellipse);
            for (int i = 0; i < 2; ++i) {
                float excess = state->target[i] - state->target[i] * scale;
                state->over[i] += excess;
                state->target[i] *= scale;
            }
        }
    }

    /* 5. Edge smoothing (overflow drain into camera turn) */
    {
        float soft_cap = 0.25f * (zx + zy);
        float over_len = hypotf(state->over[0], state->over[1]);
        float edge_smooth = fa_clampf(fa_finite_or(cfg->edge_smoothing, 0.0f), 0.0f, 5.0f);
        float k = 1.0f;

        if (edge_smooth > 0.001f && over_len > 0.0f) {
            float pressure = over_len / soft_cap;
            float rate = (1.0f / edge_smooth) * (1.0f + pressure * pressure);
            k = 1.0f - expf(-rate * dt);
        }

        for (int i = 0; i < 2; ++i) {
            float c = state->over[i] * k;
            state->over[i] -= c;
            cam_deg[i] += c;
            state->display[i] -= c * (1.0f + drag);
        }

        /* Flush tiny leftovers so Lead / Catch-Up can resume cleanly */
        if (fabsf(state->over[0]) + fabsf(state->over[1]) < 0.01f) {
            for (int i = 0; i < 2; ++i) {
                cam_deg[i] += state->over[i];
                state->display[i] -= state->over[i] * (1.0f + drag);
                state->over[i] = 0.0f;
            }
        }

        /* Hard cap remaining overflow at 3 * soft_cap */
        float rem_len = hypotf(state->over[0], state->over[1]);
        float hard_cap = 3.0f * soft_cap;
        if (rem_len > hard_cap && rem_len > 0.0f) {
            float keep = hard_cap / rem_len;
            for (int i = 0; i < 2; ++i) {
                float excess = state->over[i] * (1.0f - keep);
                state->over[i] -= excess;
                cam_deg[i] += excess;
                state->display[i] -= excess * (1.0f + drag);
            }
        }
    }

    /* 6. Edge Auto-Turn */
    int autoturning = 0;
    {
        float nx = state->target[0] / zx;
        float ny = state->target[1] / zy;
        float e = hypotf(nx, ny);
        float band = fa_clampf(fa_finite_or(cfg->edge_band, 0.2f), 0.05f, 0.9f);
        float depth = (autoedge && e > 0.0001f)
            ? fa_clampf((e - (1.0f - band)) / band, 0.0f, 1.0f) : 0.0f;

        state->edge_time = (depth > 0.0f) ? (state->edge_time + dt) : 0.0f;

        if (depth > 0.0f && state->edge_time > FA_EDGE_TURN_DELAY) {
            float ease = fa_clampf(
                (state->edge_time - FA_EDGE_TURN_DELAY) / FA_EDGE_TURN_EASE, 0.0f, 1.0f);
            float speed = edge_turn_speed * fa_smoothstep(depth) * ease * sens_mult;
            float edge_vert = fa_clampf(fa_finite_or(cfg->edge_vertical, 0.4f), 0.0f, 1.0f);

            cam_deg[0] += (nx / e) * speed * dt;
            cam_deg[1] += (ny / e) * speed * dt * edge_vert;
            autoturning = 1;
        }
    }

    /* 7. Camera Lead & Jerk-Free Catch-Up */
    if (req_len > 0.0001f) {
        state->idle_time = 0.0f;
    } else {
        state->idle_time += dt;
    }

    {
        float ex = state->target[0] / zx;
        float ey = state->target[1] / zy;
        float edgeness = sqrtf(fa_clampf(ex * ex + ey * ey, 0.0f, 1.0f));
        int edgeturn = (state->over[0] != 0.0f || state->over[1] != 0.0f);

        state->lead_ramp = edgeturn
            ? 0.0f
            : fa_clampf(state->lead_ramp + dt / FA_LEAD_EASE_IN, 0.0f, 1.0f);

        float full_dps = (isfinite(in->full_stick_deg_per_sec) && in->full_stick_deg_per_sec > 0.0f)
            ? in->full_stick_deg_per_sec : FA_DEFAULT_FULL_STICK_DPS;
        float full_step = full_dps * dt;
        float input_norm = (full_step > 0.0001f)
            ? fa_clampf(req_len / (full_step * FA_LEAD_FULL_INPUT), 0.0f, 1.0f) : 0.0f;
        state->lead_input += (input_norm - state->lead_input)
            * (1.0f - expf(-dt / FA_LEAD_INPUT_SMOOTH));

        state->last_lead_rate = 0.0f;
        state->last_catchup_rate = 0.0f;

        float recenter_delay = fa_clampf(fa_finite_or(cfg->recenter_delay, 0.25f), 0.0f, 10.0f);
        float recenter_speed = fa_clampf(fa_finite_or(cfg->recenter_speed, 0.0f), 0.0f, 20.0f);
        float recenter_smooth = fa_clampf(fa_finite_or(cfg->recenter_smooth, 0.4f), 0.0f, 5.0f);
        float camera_lead = fa_clampf(fa_finite_or(cfg->camera_lead, 0.0f), 0.0f, 20.0f);

        if (edgeturn || autoturning) {
            state->catchup_vel[0] = 0.0f;
            state->catchup_vel[1] = 0.0f;
            state->catchup_env = 0.0f;
        } else if (state->idle_time > recenter_delay && recenter_speed > 0.0001f) {
            /*
             * Jerk-free Catch-Up:
             * Track an explicit angular velocity catchup_vel that starts strictly at 0 deg/s
             * when idle_time crosses recenter_delay, eases smoothly toward the smoothstepped
             * exponential velocity v_des = (recenter_speed * env) * target, integrates via
             * trapezoidal rule, and clamps each step so target never overshoots zero.
             */
            float t_since = state->idle_time - recenter_delay;
            float smooth_dur = (recenter_smooth > 0.001f) ? recenter_smooth : 0.02f;
            float env = fa_smoothstep(t_since / smooth_dur);
            float eff_rate = recenter_speed * env;

            float tau_accel = smooth_dur * 0.25f;
            if (tau_accel < 0.01f) {
                tau_accel = 0.01f;
            }
            float tau_decel = 0.25f / (recenter_speed > 0.1f ? recenter_speed : 0.1f);
            if (tau_decel > tau_accel) {
                tau_decel = tau_accel;
            }

            state->catchup_env = env;
            state->last_catchup_rate = eff_rate;

            for (int i = 0; i < 2; ++i) {
                float tgt = state->target[i];
                if (fabsf(tgt) < 1e-6f) {
                    state->catchup_vel[i] = 0.0f;
                    continue;
                }

                float v_prev = state->catchup_vel[i];
                float v_des = eff_rate * tgt;
                float tau = (fabsf(v_des) >= fabsf(v_prev)) ? tau_accel : tau_decel;
                float alpha = 1.0f - expf(-dt / tau);
                float v_new = v_prev + (v_des - v_prev) * alpha;

                /* Keep velocity directed strictly toward zero */
                if ((tgt > 0.0f && v_new < 0.0f) || (tgt < 0.0f && v_new > 0.0f)) {
                    v_new = 0.0f;
                }

                float step = 0.5f * (v_prev + v_new) * dt;

                /* Zero-overshoot clamp */
                if (tgt > 0.0f) {
                    if (step < 0.0f) {
                        step = 0.0f;
                    } else if (step >= tgt) {
                        step = tgt;
                        v_new = 0.0f;
                    }
                } else {
                    if (step > 0.0f) {
                        step = 0.0f;
                    } else if (step <= tgt) {
                        step = tgt;
                        v_new = 0.0f;
                    }
                }

                state->catchup_vel[i] = v_new;
                state->target[i] -= step;
                state->display[i] -= step;
                cam_deg[i] += step;
            }
        } else {
            state->catchup_vel[0] = 0.0f;
            state->catchup_vel[1] = 0.0f;
            state->catchup_env = 0.0f;

            float lead_rate = camera_lead * edgeness * state->lead_input
                * fa_smoothstep(state->lead_ramp);
            state->last_lead_rate = lead_rate;

            if (lead_rate > 0.0f) {
                float k = 1.0f - expf(-lead_rate * dt);
                for (int i = 0; i < 2; ++i) {
                    float r = state->target[i] * k;
                    state->target[i] -= r;
                    state->display[i] -= r;
                    cam_deg[i] += r;
                }
            }
        }
    }

    /* 8. ADS pull toward gun when ads_zone < 1.0 */
    if (ads > 0.0f && align < 0.999f) {
        float ads_time = fa_clampf(fa_finite_or(cfg->ads_time, 0.18f), 0.05f, 5.0f);
        float rate = ads * (1.0f - align) * 5.0f / ads_time;
        float k = 1.0f - expf(-rate * dt);
        for (int i = 0; i < 2; ++i) {
            float r = state->target[i] * k;
            state->target[i] -= r;
            state->display[i] -= r;
            cam_deg[i] += r;
        }
    }

    /* 9. Camera & Walk Sway (delta-only so aim never drifts) */
    if (eff_cam_sway > 0.0f || eff_walk_sway > 0.0f) {
        float move_frac = fa_clampf(fa_finite_or(in->move_fraction, 0.0f), 0.0f, 1.0f);
        float two_pi = 2.0f * FA_PI;
        float sway[2];

        state->breath_phase = fmodf(state->breath_phase + dt * 0.27f * two_pi, two_pi);
        state->drift_phase  = fmodf(state->drift_phase  + dt * 0.61f * two_pi, two_pi);
        state->walk_phase   = fmodf(state->walk_phase   + dt * (0.9f + move_frac) * two_pi, two_pi);

        sway[0] = eff_cam_sway * (0.55f * sinf(state->drift_phase)
                                  + 0.25f * sinf(state->breath_phase * 0.5f + 1.3f))
                + eff_walk_sway * move_frac * 0.6f * sinf(state->walk_phase);
        sway[1] = eff_cam_sway * (0.60f * sinf(state->breath_phase)
                                  + 0.20f * sinf(state->drift_phase * 1.7f + 0.7f))
                - eff_walk_sway * move_frac * 0.5f * fabsf(sinf(state->walk_phase));

        for (int i = 0; i < 2; ++i) {
            float delta = sway[i] - state->prev_sway[i];
            cam_deg[i] += delta;
            state->display[i] -= delta * drag * 0.5f;
            state->prev_sway[i] = sway[i];
        }
    } else if (state->prev_sway[0] != 0.0f || state->prev_sway[1] != 0.0f) {
        /* Smoothly return any residual sway offset if sway was turned off mid-session */
        for (int i = 0; i < 2; ++i) {
            float delta = -state->prev_sway[i];
            cam_deg[i] += delta;
            state->display[i] -= delta * drag * 0.5f;
            state->prev_sway[i] = 0.0f;
        }
    }

    /* 10. Aim Assist (pulls reticle only, never the camera) */
    {
        float want[2] = { 0.0f, 0.0f };
        if (in->assist_locked) {
            float ty = fa_clampf(fa_finite_or(in->assist_target_deg[0], 0.0f), -80.0f, 80.0f);
            float tp = fa_clampf(fa_finite_or(in->assist_target_deg[1], 0.0f), -80.0f, 80.0f);
            want[0] = ty - (state->target[0] + state->over[0]);
            want[1] = tp - (state->target[1] + state->over[1]);
        }
        float k = 1.0f - expf(-dt / FA_ASSIST_TAU);
        state->assist[0] += (want[0] - state->assist[0]) * k;
        state->assist[1] += (want[1] - state->assist[1]) * k;
    }

    /* 11. Damped Spring + Lag Cap */
    fa_step_spring(state, eff_gun_resp, cfg->gun_damping, dt);

    {
        float min_z = (zx < zy) ? zx : zy;
        float max_lag = 0.35f * min_z + 1.5f;
        float lag[2] = {
            state->display[0] - (state->target[0] + state->over[0] + state->assist[0]),
            state->display[1] - (state->target[1] + state->over[1] + state->assist[1])
        };
        float lag_len = hypotf(lag[0], lag[1]);
        if (lag_len > max_lag && lag_len > 0.0f) {
            float keep = max_lag / lag_len;
            for (int i = 0; i < 2; ++i) {
                state->display[i] -= lag[i] * (1.0f - keep);
                state->vel[i] *= keep;
            }
        }
    }

    /* Keep displayed reticle bounded on screen even after violent turns */
    state->display[0] = fa_clampf(state->display[0], -zx * 2.0f - 10.0f, zx * 2.0f + 10.0f);
    state->display[1] = fa_clampf(state->display[1], -zy * 2.0f - 10.0f, zy * 2.0f + 10.0f);

    /* Update debug snapshot fields */
    state->last_zx = zx;
    state->last_zy = zy;
    state->edge_turn_active = autoturning;
    state->active = 1;

    /* 12. Populate outputs */
    out->cam_delta_deg[0] = cam_deg[0];
    out->cam_delta_deg[1] = cam_deg[1];
    out->reticle_deg[0] = state->display[0];
    out->reticle_deg[1] = state->display[1];
    out->reticle_delta_deg[0] = state->display[0] - prev_display[0];
    out->reticle_delta_deg[1] = state->display[1] - prev_display[1];
    out->ads_amount = ads;
    out->show_reticle = in->aiming ? cfg->reticle.show_ads : cfg->reticle.show_hip;
    out->active = 1;
}

void fa_project_reticle_ndc(
    const float reticle_deg[2],
    float       world_fovy_deg,
    float       aspect,
    float       out_ndc[2])
{
    if (!out_ndc) {
        return;
    }
    if (!reticle_deg) {
        out_ndc[0] = 0.0f;
        out_ndc[1] = 0.0f;
        return;
    }

    float fovy = fa_clampf(fa_finite_or(world_fovy_deg, 60.0f), 1.0f, 175.0f);
    float asp = (isfinite(aspect) && aspect > 0.01f) ? aspect : (4.0f / 3.0f);
    float tan_half_y = tanf(FA_DEG2RAD(fovy * 0.5f));
    float tan_half_x = tan_half_y * asp;

    float yaw = fa_clampf(fa_finite_or(reticle_deg[0], 0.0f), -85.0f, 85.0f);
    float pitch = fa_clampf(fa_finite_or(reticle_deg[1], 0.0f), -85.0f, 85.0f);

    out_ndc[0] = fa_clampf(tanf(FA_DEG2RAD(yaw)) / tan_half_x, -0.97f, 0.97f);
    out_ndc[1] = fa_clampf(tanf(FA_DEG2RAD(pitch)) / tan_half_y, -0.97f, 0.97f);
}

void fa_compute_shot_dir(
    const float cam_forward[3],
    const float cam_right[3],
    const float cam_up[3],
    const float reticle_deg[2],
    float       out_dir[3])
{
    if (!out_dir) {
        return;
    }
    if (!cam_forward || !cam_right || !cam_up || !reticle_deg) {
        out_dir[0] = 0.0f;
        out_dir[1] = 0.0f;
        out_dir[2] = 1.0f;
        return;
    }

    float yaw = fa_clampf(fa_finite_or(reticle_deg[0], 0.0f), -80.0f, 80.0f);
    float pitch = fa_clampf(fa_finite_or(reticle_deg[1], 0.0f), -80.0f, 80.0f);
    float tx = tanf(FA_DEG2RAD(yaw));
    float ty = tanf(FA_DEG2RAD(pitch));

    float dx = cam_forward[0] + cam_right[0] * tx + cam_up[0] * ty;
    float dy = cam_forward[1] + cam_right[1] * tx + cam_up[1] * ty;
    float dz = cam_forward[2] + cam_right[2] * tx + cam_up[2] * ty;

    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) {
        out_dir[0] = dx / len;
        out_dir[1] = dy / len;
        out_dir[2] = dz / len;
    } else {
        out_dir[0] = cam_forward[0];
        out_dir[1] = cam_forward[1];
        out_dir[2] = cam_forward[2];
    }
}

void fa_compute_viewmodel_offset(
    const float reticle_deg[2],
    float       ads_amount,
    float       ads_height,
    const float barrel_offset[3],
    float       barrel_depth,
    float       out_pos_offset[3],
    float       out_rot_deg[2])
{
    float yaw = reticle_deg ? fa_clampf(fa_finite_or(reticle_deg[0], 0.0f), -80.0f, 80.0f) : 0.0f;
    float pitch = reticle_deg ? fa_clampf(fa_finite_or(reticle_deg[1], 0.0f), -80.0f, 80.0f) : 0.0f;
    float ads = fa_clampf(fa_finite_or(ads_amount, 0.0f), 0.0f, 1.0f);
    float height = fa_clampf(fa_finite_or(ads_height, 0.0f), -10.0f, 10.0f);
    float depth = fa_finite_or(barrel_depth, 0.5f);
    if (depth < 0.05f) {
        depth = 0.05f;
    }

    float bx = barrel_offset ? fa_finite_or(barrel_offset[0], 0.0f) : 0.0f;
    float by = barrel_offset ? fa_finite_or(barrel_offset[1], 0.0f) : 0.0f;

    if (out_pos_offset) {
        float aim_x = tanf(FA_DEG2RAD(yaw)) * depth;
        float aim_y = tanf(FA_DEG2RAD(pitch)) * depth;
        float target_x = aim_x - bx;
        float target_y = aim_y - by - depth * 0.021f + height;

        out_pos_offset[0] = target_x * ads;
        out_pos_offset[1] = target_y * ads;
        out_pos_offset[2] = 0.0f;
    }

    if (out_rot_deg) {
        out_rot_deg[0] = yaw;
        out_rot_deg[1] = pitch;
    }
}

void fa_get_debug_info(const fa_state_t* state, fa_debug_info_t* out_info) {
    if (!out_info) {
        return;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (!state) {
        return;
    }

    out_info->zone_x = state->last_zx;
    out_info->zone_y = state->last_zy;
    out_info->target[0] = state->target[0];
    out_info->target[1] = state->target[1];
    out_info->display[0] = state->display[0];
    out_info->display[1] = state->display[1];
    out_info->over[0] = state->over[0];
    out_info->over[1] = state->over[1];
    out_info->lead_rate = state->last_lead_rate;
    out_info->catchup_rate = state->last_catchup_rate;
    out_info->catchup_vel[0] = state->catchup_vel[0];
    out_info->catchup_vel[1] = state->catchup_vel[1];
    out_info->last_input = state->last_input;
    out_info->edge_turn_active = state->edge_turn_active;
    out_info->ads_blend = state->ads_blend;
    out_info->active = state->active;
}

float fa_scripted_input_sample(float time_sec) {
    if (!isfinite(time_sec)) {
        return 0.0f;
    }
    float t = fmodf(time_sec, FA_SCRIPTED_CYCLE_SEC);
    if (t < 0.0f) {
        t += FA_SCRIPTED_CYCLE_SEC;
    }

    if (t < 0.4f) {
        return 0.5f;   /* Fast turn right */
    }
    if (t < 1.6f) {
        return 0.0f;   /* Stop */
    }
    if (t < 1.8f) {
        return -1.0f;  /* Fast turn left */
    }
    if (t < 3.0f) {
        return 0.0f;   /* Stop */
    }
    if (t < 3.6f) {
        return 0.12f;  /* Slow sweep inside zone */
    }
    return 0.0f;       /* Stop */
}

int fa_format_telemetry_csv_header(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return -1;
    }
    return snprintf(
        buf, buf_size,
        "time,dt,preset,active,in_yaw,in_pitch,target_yaw,target_pitch,"
        "reticle_yaw,reticle_pitch,cam_delta_yaw,cam_delta_pitch,"
        "cam_yaw,cam_pitch,fovy");
}

int fa_format_telemetry_csv_row(
    char*              buf,
    size_t             buf_size,
    float              time_sec,
    float              dt_sec,
    const fa_cfg_t*    cfg,
    const fa_state_t*  state,
    const float        input_deg[2],
    const fa_output_t* out,
    const float        cam_angle_deg[2],
    float              fovy_deg)
{
    if (!buf || buf_size == 0 || !cfg || !state || !input_deg || !out || !cam_angle_deg) {
        return -1;
    }
    return snprintf(
        buf, buf_size,
        "%.4f,%.4f,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.2f",
        (double)fa_finite_or(time_sec, 0.0f),
        (double)fa_finite_or(dt_sec, 0.0f),
        (int)cfg->preset,
        out->active ? 1 : 0,
        (double)fa_finite_or(input_deg[0], 0.0f),
        (double)fa_finite_or(input_deg[1], 0.0f),
        (double)state->target[0],
        (double)state->target[1],
        (double)out->reticle_deg[0],
        (double)out->reticle_deg[1],
        (double)out->cam_delta_deg[0],
        (double)out->cam_delta_deg[1],
        (double)fa_finite_or(cam_angle_deg[0], 0.0f),
        (double)fa_finite_or(cam_angle_deg[1], 0.0f),
        (double)fa_finite_or(fovy_deg, 60.0f));
}
