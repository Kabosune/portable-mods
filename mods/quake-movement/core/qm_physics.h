/*
 * qm_physics.h - Decoupled pure-C11 Quake / Source / Apex / Deadlock movement solver.
 *
 * Coordinate frame: right-handed Z-up in meters
 *   +X = Right (at cam_yaw_rad = 0)
 *   +Y = Forward (at cam_yaw_rad = 0)
 *   +Z = Up
 *   cam_yaw_rad = 0 faces +Y; positive yaw rotates counter-clockwise around +Z:
 *     forward = (-sin(yaw),  cos(yaw), 0)
 *     right   = ( cos(yaw),  sin(yaw), 0)
 *
 * Zero OS or third-party dependencies. Compiles cleanly under C11 with strict warnings.
 */

#ifndef QM_PHYSICS_H
#define QM_PHYSICS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum qm_preset_id {
    QM_PRESET_QUAKE_VQ3    = 0,
    QM_PRESET_QUAKE_CPMA   = 1,
    QM_PRESET_HALF_LIFE    = 2,
    QM_PRESET_APEX_LEGENDS = 3,
    QM_PRESET_DEADLOCK     = 4,
    QM_PRESET_CUSTOM       = 5,
    QM_PRESET_COUNT        = 6
} qm_preset_id_t;

typedef enum qm_move_mode {
    QM_MODE_GROUND = 0,
    QM_MODE_AIR    = 1,
    QM_MODE_SLIDE  = 2,
    QM_MODE_DASH   = 3
} qm_move_mode_t;

typedef struct qm_params {
    /* Core ground/air locomotion */
    float max_speed;           /* Target ground max speed (m/s) */
    float ground_accel;        /* Ground PM_Accelerate factor */
    float air_accel;           /* Air PM_AirAccelerate factor */
    float air_wish_cap;        /* Max wishspeed cap in air (m/s) */
    float air_control;         /* CPMA air-control turning strength (0 = off) */
    float air_stop_accel;      /* Extra air braking when counter-strafing (0 = off) */
    float cpm_strafe_accel;    /* CPMA pure A/D strafe accel (0 = use air_accel) */
    float cpm_strafe_wish_cap; /* CPMA pure A/D strafe wish cap (m/s) */
    float friction;            /* Ground friction coefficient */
    float stop_speed;          /* Minimum control speed for friction (m/s) */
    float gravity;             /* Downward gravity magnitude (m/s^2, positive) */
    float jump_speed;          /* Vertical jump launch velocity (m/s) */
    float crouch_jump_boost;   /* Optional extra jump vz when crouching (m/s) */
    float coyote_time_sec;     /* Grace window after leaving ground (s) */
    float jump_buffer_sec;     /* Early jump press buffer before landing (s) */
    int   auto_bhop;           /* 1 = holding jump auto-jumps on landing without ground friction */

    /* Momentum-preserving crouch-slide (Apex / Deadlock / Custom) */
    int   slide_enabled;
    float slide_min_speed;     /* Min horizontal speed to initiate slide (m/s) */
    float slide_exit_speed;    /* Speed below which slide ends (m/s) */
    float slide_boost;         /* Flat-ground slide initiation speed boost (m/s) */
    float slide_friction;      /* Reduced ground friction while sliding */
    float slide_steer_accel;   /* Mild lateral steering while sliding */
    float slide_slope_accel;   /* Downhill slope acceleration factor (m/s^2) */
    float slide_max_speed;     /* Hard speed cap while sliding (m/s) */
    float slide_cooldown_sec;  /* Cooldown before another flat slide boost (s) */
    float slide_jump_boost;    /* Horizontal boost when jumping out of a slide (m/s) */

    /* Tap-strafe / air-lurch window right after jump (Apex / Custom) */
    int   tap_strafe_enabled;
    float tap_strafe_window_sec; /* Duration after jump where lurching redirects velocity (s) */
    float tap_strafe_strength;   /* Redirect responsiveness (0..1+) */

    /* Directional ground/air dash & timed dash-jump (Deadlock / Custom) */
    int   dash_enabled;
    int   dash_max_stamina;          /* Max stamina pips (e.g. 3) */
    float dash_stamina_cost;         /* Pips spent per dash (e.g. 1.0) */
    float dash_recharge_sec;         /* Seconds to recharge 1 pip */
    float dash_impulse;              /* Directional dash speed (m/s) */
    float dash_duration_sec;         /* Active dash duration (s) */
    float dash_jump_window_start_sec;/* Earliest elapsed time after dash for dash-jump (s) */
    float dash_jump_window_end_sec;  /* Latest elapsed time after dash for dash-jump (s) */
    float dash_jump_boost;           /* Forward speed boost on timed dash-jump (m/s) */
    float dash_jump_up_bonus;        /* Extra jump vz on timed dash-jump (m/s) */

    /* Optional wall-bounce hook (consumes wall probe callback when supplied) */
    int   wall_bounce_enabled;
    float wall_bounce_probe_dist; /* Probe radius in meters */
    float wall_bounce_push;       /* Outward speed along wall normal (m/s) */
    float wall_bounce_up;         /* Upward Z launch on wall-bounce (m/s) */
    float wall_bounce_cooldown;   /* Cooldown between wall-bounces (s) */
} qm_params_t;

typedef struct qm_wall_probe_result {
    int   hit;
    float normal[3];   /* Outward unit normal in Z-up world coords */
    float distance_m;
} qm_wall_probe_result_t;

/* Optional surface normal callback for host raycast or sweep queries */
typedef int (*qm_wall_probe_fn_t)(
    const float             pos[3],
    const float             probe_dir[3],
    float                   max_dist_m,
    qm_wall_probe_result_t* out_hit,
    void*                   user_ctx);

typedef struct qm_input {
    float dt_sec;
    float wish_forward;     /* -1..+1 (W=+1, S=-1) */
    float wish_right;       /* -1..+1 (D=+1, A=-1) */
    float cam_yaw_rad;      /* 0=+Y Forward, +CCW around +Z */
    int   jump_held;
    int   jump_pressed;
    int   crouch_held;
    int   crouch_pressed;
    int   dash_pressed;
    int   on_ground;
    float slope_normal[3];  /* Surface normal; (0,0,0) treated as flat (0,0,1) */

    /* Optional wall-bounce surface probe hook (NULL = disabled for this tick) */
    qm_wall_probe_fn_t wall_probe;
    void*              wall_probe_ctx;
} qm_input_t;

typedef struct qm_state {
    float pos[3];
    float vel[3];

    /* Persistent timers & mechanic state */
    float coyote_timer;
    float jump_buffer_timer;
    int   sliding;
    float slide_cooldown;
    float tap_strafe_timer;
    float last_wish_dir[2];
    int   has_last_wish;

    /* Dash & stamina state */
    float stamina;
    float dash_timer;
    float dash_elapsed;
    int   dash_active_window;
    int   dash_jumped_this_dash;
    float dash_dir[2];

    /* Wall-bounce cooldown */
    float wall_bounce_timer;

    /* Per-step telemetry / event flags */
    qm_move_mode_t mode;
    float          horiz_speed;
    float          peak_speed;
    int            jumped;
    int            slide_started;
    int            slide_jumped;
    int            tap_strafed;
    int            dashed;
    int            dash_jumped;
    int            wall_bounced;
} qm_state_t;

/* Preset metadata & helpers */
const char* qm_preset_name(qm_preset_id_t preset);
const char* qm_preset_key(qm_preset_id_t preset);
const char* qm_mode_name(qm_move_mode_t mode);
int         qm_preset_from_name(const char* name, qm_preset_id_t* out_preset);

/* Populate parameter struct with preset defaults */
void qm_preset_defaults(qm_preset_id_t preset, qm_params_t* out_params);

/* Initialize or reset persistent movement state cleanly for given parameters */
void qm_state_init(qm_state_t* state, const qm_params_t* params);

/* Compute horizontal speed hypot(vel[0], vel[1]) */
float qm_horiz_speed(const float vel[3]);

/* Compute camera forward & right unit vectors in Z-up world coords */
void qm_yaw_basis(float cam_yaw_rad, float out_forward[2], float out_right[2]);

/*
 * Deterministic single-tick movement physics step.
 * `in_state` and `out_state` may alias.
 */
void qm_step(
    const qm_params_t* params,
    const qm_input_t*  input,
    const qm_state_t*  in_state,
    qm_state_t*        out_state);

#ifdef __cplusplus
}
#endif

#endif /* QM_PHYSICS_H */
