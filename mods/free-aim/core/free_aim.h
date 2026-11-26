/*
 * free_aim.h - Engine-agnostic pure-C11 unlocked free-aim / dynamic reticle core.
 *
 * Per-player, zero OS or engine dependencies. Compiles cleanly under C11 with strict warnings.
 *
 * Coordinate / sign conventions:
 *   Angles:       degrees relative to the camera centre.
 *                   yaw   (+ = right)
 *                   pitch (+ = up)
 *   Look inputs:  per-frame rotation requests in degrees (stick, mouse, gyro).
 *   Camera delta: per-frame rotation to apply to the camera in degrees (+yaw right, +pitch up).
 *   Reticle NDC:  normalized device coordinates in [-1, +1] (+x right, +y up).
 */

#ifndef FREE_AIM_H
#define FREE_AIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FA_NUM_CUSTOM_PRESETS 3

/* Edge Auto-Turn and Camera Lead tuning constants */
#define FA_EDGE_TURN_DELAY    0.15f /* Seconds in edge band before Edge Auto-Turn starts */
#define FA_EDGE_TURN_EASE     0.20f /* Seconds for Edge Auto-Turn to ease to full speed */
#define FA_EDGE_STICK_TILT    0.25f /* Stick deflection threshold to claim last_input */
#define FA_LEAD_EASE_IN       0.30f /* Seconds for Camera Lead to fade back in after an edge turn */
#define FA_LEAD_FULL_INPUT    0.50f /* Look speed fraction of full stick giving full Camera Lead */
#define FA_LEAD_INPUT_SMOOTH  0.10f /* Time constant (s) for smoothing lead_input */
#define FA_SCRIPTED_CYCLE_SEC 4.80f /* Period (s) of the repeatable scripted look pattern */

typedef enum fa_preset {
    FA_PRESET_CLASSIC   = 0, /* Mod off: 1:1 original game aiming (default) */
    FA_PRESET_WEIGHTY   = 1, /* Balanced free-aim with moderate weapon heft and sway */
    FA_PRESET_IMMERSIVE = 2, /* Wider zone, heavier flowing weapon, no reticle while aiming */
    FA_PRESET_ARCADE    = 3, /* Wide pointer zone, Edge Auto-Turn, own reticle profile */
    FA_PRESET_BORING    = 4, /* Modern centered FPS: zero zone, 100% camera share, no sway */
    FA_PRESET_CUSTOM1   = 5, /* User-editable custom slot 1 */
    FA_PRESET_CUSTOM2   = 6, /* User-editable custom slot 2 */
    FA_PRESET_CUSTOM3   = 7, /* User-editable custom slot 3 */
    FA_PRESET_COUNT     = 8
} fa_preset_t;

typedef enum fa_input_source {
    FA_INPUT_NONE  = 0,
    FA_INPUT_MOUSE = 1,
    FA_INPUT_GYRO  = 2,
    FA_INPUT_STICK = 3
} fa_input_source_t;

/* Per-player reticle display settings (UI/renderer consumes these) */
typedef struct fa_reticle_cfg {
    int   show_hip;       /* 1 = show reticle while hip-firing, 0 = hide */
    int   show_ads;       /* 1 = show reticle while aiming (ADS), 0 = hide */
    float size;           /* Reticle scale/size index (default 2.0 normal, 3.0 arcade) */
    float opacity;        /* 0.0..1.0 alpha (default ~0.157 normal, ~0.314 arcade) */
    int   smooth_reticle; /* 1 = sub-pixel reticle positioning, 0 = whole-pixel */
} fa_reticle_cfg_t;

/* Per-player free-aim configuration */
typedef struct fa_cfg {
    fa_preset_t preset;

    /* Free-aim ellipse & camera coupling */
    float zone_x;          /* Elliptical zone half-width in degrees (0..45) */
    float zone_y;          /* Elliptical zone half-height in degrees (0..45) */
    float camera_share;    /* Share of look input turning camera immediately (0..1) */
    float camera_lead;     /* Camera drift rate toward reticle while aiming (1/s at edge, 0..10) */
    float recenter_speed;  /* Idle catch-up rate (1/s, 0 = off, 0..10) */
    float recenter_delay;  /* Idle seconds before catch-up begins (0..5) */
    float recenter_smooth; /* Seconds over which catch-up eases in smoothly (0..2) */
    float reticle_speed;   /* Inside-zone reticle speed multiplier (0.25..3.0, 1.0 = 1:1) */

    /* Weapon inertia spring & edge feel */
    float gun_response;    /* Spring natural frequency in Hz (0.5..40; lower = heavier) */
    float gun_damping;     /* Spring damping ratio (0.05..3.0; 1 = critically damped) */
    float turn_drag;       /* Weapon lag when camera rotates (0..1) */
    float edge_smoothing;  /* Overflow drain time constant in seconds (0 = rigid, 0..1) */

    /* Camera & walk sway */
    float camera_sway;     /* Idle breathing + drift sway amplitude in degrees (0..5) */
    float walk_sway;       /* Footstep sway amplitude at full walk speed in degrees (0..5) */

    /* Aim Down Sights (ADS) */
    float ads_zoom;        /* Zoom multiplier while aiming (1.0..3.0) */
    float ads_time;        /* Blend duration in seconds (0.01..1.0) */
    float ads_sway;        /* Fraction of sway retained while aiming (0..1) */
    float ads_zone;        /* Aim feel kept while aiming (0..2.0; <1 pulls center, >1 exaggerates) */
    float ads_sens;        /* Look sensitivity multiplier while aiming (0.1..1.0) */
    float ads_height;      /* Viewmodel vertical height offset while ADS (-10..10) */

    /* Edge Auto-Turn */
    float edge_turn_speed; /* Auto-turn angular speed at outer edge in deg/s (0 = off, 0..360) */
    float edge_band;       /* Outer band fraction of ellipse (0.05..0.9) */
    float edge_influence;  /* Share of manual edge overflow kept while auto-turning (0..1) */
    float edge_vertical;   /* Vertical auto-turn scale relative to horizontal (0..1) */
    int   edge_mouse;      /* 1 = allow Edge Auto-Turn when mouse moved reticle last */
    int   edge_gyro;       /* 1 = allow Edge Auto-Turn when gyro moved reticle last */
    int   edge_stick;      /* 1 = allow Edge Auto-Turn when stick moved reticle last */

    /* Reticle profile & display settings */
    int              reticle_profile; /* 0 = normal profile, 1 = arcade profile */
    fa_reticle_cfg_t reticle;
} fa_cfg_t;

/* Per-player preset & reticle profile manager */
typedef struct fa_profile_bank {
    fa_cfg_t         cfg;                               /* Live configuration */
    fa_cfg_t         custom[FA_NUM_CUSTOM_PRESETS];     /* Saved Custom 1-3 slots */
    int              last_custom;                       /* 0..2: last custom slot edited */
    fa_reticle_cfg_t reticle_profiles[2];               /* 0 = normal, 1 = arcade */
    int              reticle_live_profile;              /* Active reticle profile index (0 or 1) */
} fa_profile_bank_t;

/* Per-player persistent runtime state (angles in degrees from camera centre) */
typedef struct fa_state {
    float             target[2];         /* Desired gun aim offset (yaw, pitch) */
    float             display[2];        /* Spring-driven displayed reticle offset (yaw, pitch) */
    float             vel[2];            /* Spring angular velocity (deg/s) */
    float             over[2];           /* Overflow past zone edge waiting to turn camera (deg) */
    float             idle_time;         /* Seconds since last look input */
    float             lead_ramp;         /* 0..1 Camera Lead ease-in after edge turn */
    float             lead_input;        /* 0..1 smoothed input speed factor for Camera Lead */
    fa_input_source_t last_input;        /* Which input device moved the reticle last */
    float             edge_time;         /* Seconds spent continuously inside edge band */
    float             ads_blend;         /* 0..1 linear ADS blend (smoothstepped on use) */
    float             assist[2];         /* Smoothed aim-assist reticle offset (deg) */
    float             breath_phase;      /* Breathing sway oscillator phase (rad) */
    float             drift_phase;       /* Slow drift sway oscillator phase (rad) */
    float             walk_phase;        /* Footstep sway oscillator phase (rad) */
    float             prev_sway[2];      /* Previous frame sway offset for delta-only application */
    float             catchup_vel[2];    /* Explicit catch-up angular velocity (deg/s) */
    float             catchup_env;       /* 0..1 smoothstepped catch-up envelope */
    float             last_lead_rate;    /* Last frame effective Camera Lead rate (1/s) */
    float             last_catchup_rate; /* Last frame effective Catch-Up rate (1/s) */
    float             last_zx;           /* Last frame effective ellipse half-width (deg) */
    float             last_zy;           /* Last frame effective ellipse half-height (deg) */
    int               edge_turn_active;  /* 1 if Edge Auto-Turn drove camera last frame */
    int               active;            /* 1 if free-aim was active last frame */
} fa_state_t;

/* Per-frame input sample */
typedef struct fa_input {
    float stick_deg[2];            /* Stick look delta in degrees (already curved) */
    float mouse_deg[2];            /* Mouse look delta in degrees */
    float gyro_deg[2];             /* Gyro look delta in degrees (treated like mouse) */
    float stick_deflection;        /* Stick deflection magnitude 0..1 (for >25% last_input rule) */
    float full_stick_deg_per_sec;  /* Reference full-stick speed in deg/s (<=0 defaults to 210) */
    float dt_sec;                  /* Frame delta time in seconds */
    int   aiming;                  /* 1 while holding ADS/aim button */
    int   gameplay_active;         /* 1 when alive, able to look, and in normal gameplay */
    float move_fraction;           /* 0..1 player movement speed fraction for walk sway */
    float fov_scale;               /* FOV scale from fa_fov_scale() (<=0 defaults to 1.0) */
    int   assist_locked;           /* 1 if host auto-aim is locked onto a target */
    float assist_target_deg[2];    /* Auto-aim target angle from camera centre (yaw, pitch deg) */
} fa_input_t;

/* Per-frame output */
typedef struct fa_output {
    float cam_delta_deg[2];     /* Camera rotation to apply this frame (yaw, pitch deg) */
    float reticle_deg[2];       /* Displayed reticle offset from camera centre (yaw, pitch deg) */
    float reticle_delta_deg[2]; /* Change in reticle_deg since previous frame (deg) */
    float ads_amount;           /* Smoothstepped ADS blend 0..1 */
    int   show_reticle;         /* 1 if reticle should be rendered this frame */
    int   active;               /* 1 if free-aim drove aim this frame */
} fa_output_t;

/* Debug overlay snapshot (off by default; host calls fa_get_debug_info when enabled) */
typedef struct fa_debug_info {
    float             zone_x;
    float             zone_y;
    float             target[2];
    float             display[2];
    float             over[2];
    float             lead_rate;
    float             catchup_rate;
    float             catchup_vel[2];
    fa_input_source_t last_input;
    int               edge_turn_active;
    float             ads_blend;
    int               active;
} fa_debug_info_t;

/* Metadata helpers */
const char* fa_preset_name(fa_preset_t preset);
const char* fa_input_source_name(fa_input_source_t source);
int         fa_preset_is_custom(fa_preset_t preset);
int         fa_preset_persists_edits(fa_preset_t preset);

/* Preset, reticle profile & state initialization */
void fa_reticle_defaults(int arcade_profile, fa_reticle_cfg_t* out_reticle);
void fa_preset_defaults(fa_preset_t preset, fa_cfg_t* out_cfg);
void fa_cfg_defaults(fa_cfg_t* cfg); /* Defaults to FA_PRESET_CLASSIC (mod off) */
void fa_reset_state(fa_state_t* state);

void fa_profile_bank_init(fa_profile_bank_t* bank);
void fa_profile_apply_preset(fa_profile_bank_t* bank, fa_preset_t preset);
void fa_profile_settings_changed(fa_profile_bank_t* bank);
void fa_profile_on_load_refresh_builtin(fa_profile_bank_t* bank);

/*
 * Step the free-aim pipeline for one frame.
 *
 * - force_original_aim != 0: returns 1:1 unmodded camera delta, zero reticle offset,
 *   zero sway/spring, resets runtime state, and never mutates cfg.
 * - dt_sec <= 0 or non-finite: returns zero camera/reticle deltas and preserves state.
 * - gameplay_active == 0 or cfg->preset == FA_PRESET_CLASSIC: resets state and passes
 *   raw input through (or zero if !gameplay_active).
 */
void fa_step(
    const fa_cfg_t*   cfg,
    fa_state_t*       state,
    const fa_input_t* in,
    int               force_original_aim,
    fa_output_t*      out);

/* FOV scaling helper: tan(fov/2) / tan(ref_fov/2) */
float fa_fov_scale(float fov_deg, float ref_fov_deg);

/*
 * Project reticle angular offset onto screen NDC [-0.97, +0.97] using the WORLD camera's
 * vertical FOV and aspect ratio (explicitly NOT the viewmodel FOV).
 * out_ndc[0]: +right, out_ndc[1]: +up.
 */
void fa_project_reticle_ndc(
    const float reticle_deg[2],
    float       world_fovy_deg,
    float       aspect,
    float       out_ndc[2]);

/*
 * Compute normalized world-space shot direction from camera basis vectors and
 * displayed reticle yaw/pitch angles (degrees).
 */
void fa_compute_shot_dir(
    const float cam_forward[3],
    const float cam_right[3],
    const float cam_up[3],
    const float reticle_deg[2],
    float       out_dir[3]);

/*
 * Compute camera-space viewmodel position offset (for ADS barrel alignment) and
 * angular offset (yaw, pitch deg) so the weapon aligns with the free-aim line.
 * barrel_offset is camera-space (x, y, z) from weapon origin to muzzle.
 */
void fa_compute_viewmodel_offset(
    const float reticle_deg[2],
    float       ads_amount,
    float       ads_height,
    const float barrel_offset[3],
    float       barrel_depth,
    float       out_pos_offset[3],
    float       out_rot_deg[2]);

/* Debug overlay, repeatable scripted input, and CSV telemetry helpers */
void  fa_get_debug_info(const fa_state_t* state, fa_debug_info_t* out_info);
float fa_scripted_input_sample(float time_sec);
int   fa_format_telemetry_csv_header(char* buf, size_t buf_size);
int   fa_format_telemetry_csv_row(
    char*             buf,
    size_t            buf_size,
    float             time_sec,
    float             dt_sec,
    const fa_cfg_t*   cfg,
    const fa_state_t* state,
    const float       input_deg[2],
    const fa_output_t* out,
    const float       cam_angle_deg[2],
    float             fovy_deg);

#ifdef __cplusplus
}
#endif

#endif /* FREE_AIM_H */
