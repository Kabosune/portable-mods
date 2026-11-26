/*
 * weighty_aim.h - Engine-agnostic pure-C11 stick response, turn boost, and GyroWiki gyro aim.
 *
 * Per-player, zero OS or engine dependencies. Compiles cleanly under C11 with strict warnings.
 *
 * Coordinate / sign conventions:
 *   Stick input:  normalized [-1, +1] per axis (x = right, y = up/down as supplied by host).
 *   Gyro sensor:  SDL-style right-handed angular velocity in degrees/second:
 *                   gyro_deg_s[0] = pitch (+ tilts far edge up)
 *                   gyro_deg_s[1] = yaw   (+ turns controller left)
 *                   gyro_deg_s[2] = roll  (+ rolls controller clockwise)
 *                 Accelerometer vector in any consistent gravity unit (m/s^2 or g).
 *   Gyro output:  in-game rotation delta in degrees for this frame:
 *                   out_yaw_deg   (+ turns camera right)
 *                   out_pitch_deg (+ pitches camera up; inverted when invert_y != 0)
 */

#ifndef WEIGHTY_AIM_H
#define WEIGHTY_AIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WA_NUM_CUSTOM_CURVES 3
#define WA_GRAPH_MAX_SAMPLES 65

typedef enum wa_curve {
    WA_CURVE_CLASSIC  = 0, /* Host game's native response (sensitivity & boost still apply) */
    WA_CURVE_LINEAR   = 1, /* out = n */
    WA_CURVE_BALANCED = 2, /* out = n^1.5 */
    WA_CURVE_CUSTOM1  = 3, /* Cubic Bezier (0,0)->(1,1) slot 1 */
    WA_CURVE_CUSTOM2  = 4, /* Cubic Bezier (0,0)->(1,1) slot 2 */
    WA_CURVE_CUSTOM3  = 5, /* Cubic Bezier (0,0)->(1,1) slot 3 */
    WA_CURVE_COUNT    = 6
} wa_curve_t;

typedef enum wa_boost_mode {
    WA_BOOST_OFF     = 0,
    WA_BOOST_INSTANT = 1,
    WA_BOOST_RAMPED  = 2,
    WA_BOOST_COUNT   = 3
} wa_boost_mode_t;

typedef enum wa_gyro_mode {
    WA_GYRO_OFF    = 0,
    WA_GYRO_ALWAYS = 1,
    WA_GYRO_AIMING = 2,
    WA_GYRO_COUNT  = 3
} wa_gyro_mode_t;

typedef enum wa_gyro_space {
    WA_GYRO_SPACE_PLAYER = 0, /* Gravity-aligned yaw from turning and leaning */
    WA_GYRO_SPACE_LOCAL  = 1, /* Controller's local yaw axis only */
    WA_GYRO_SPACE_COUNT  = 2
} wa_gyro_space_t;

typedef enum wa_gyro_status {
    WA_GYRO_STATUS_NOT_FOUND    = 0,
    WA_GYRO_STATUS_CALIBRATING  = 1,
    WA_GYRO_STATUS_READY        = 2,
    WA_GYRO_STATUS_UNCALIBRATED = 3
} wa_gyro_status_t;

/* Per-player stick response & turn boost configuration */
typedef struct wa_stick_cfg {
    wa_curve_t curve;                            /* Default: WA_CURVE_CLASSIC */
    float      inner_deadzone;                   /* 0.00..0.40, default 0.01 */
    float      outer_deadzone;                   /* 0.10..1.00, default 0.95 (kept >= inner + 0.05) */
    float      bezier[4];                        /* Active custom curve: x1, y1, x2, y2 in [0,1] */
    float      bezier_profiles[WA_NUM_CUSTOM_CURVES][4];
    int        last_custom_curve;                /* 0..2: slot edited when moving sliders from built-in curve */
    int        game_builtin_deadzone;            /* 1 = keep host's built-in deadzone, 0 = bypass it */
    float      sensitivity;                      /* 0.25..2.50, default 1.0 */
    float      vertical_sens;                    /* 0.25..2.00, default 0.80 */

    /* Look acceleration ("turn boost") */
    wa_boost_mode_t boost_mode;                  /* Default: WA_BOOST_RAMPED */
    float           boost_amount;                /* 1.0..4.0, default 2.0 */
    float           boost_threshold;             /* 0.3..1.0, default 0.9 */
    float           boost_delay;                 /* Seconds before ramp starts, default 0.1 */
    float           boost_time;                  /* Seconds to ramp 0->1 (smoothstep), default 0.25 */
    float           boost_vertical;              /* Vertical share of boost (0..1), default 0.35 */
} wa_stick_cfg_t;

/* Per-player persistent stick runtime state */
typedef struct wa_stick_state {
    float boost_held;   /* Seconds held at or above boost threshold */
    float boost_level;  /* Current boost amount 0..1 */
} wa_stick_state_t;

/* Per-player GyroWiki gyro configuration */
typedef struct wa_gyro_cfg {
    wa_gyro_mode_t  mode;             /* Default: WA_GYRO_OFF */
    float           sensitivity;      /* In-game deg per real deg: 0.1..20.0, default 2.5 */
    float           vertical;         /* Vertical share of horizontal: 0.0..2.0, default 0.90 */
    wa_gyro_space_t space;            /* Default: WA_GYRO_SPACE_PLAYER */
    int             invert_y;         /* 0 = normal, 1 = invert vertical */
    float           acceleration;     /* 1.0..4.0, default 1.0 (off) */
    float           accel_threshold;  /* deg/s for full accel: 5..500, default 75.0 */
    float           tightening;       /* deg/s below which input scales linearly: 0..30, default 0.0 */
    float           smoothing;        /* deg/s soft tiered smoothing threshold: 0..30, default 4.0 */
    int             auto_calibrate;   /* 1 = auto-settle bias when still (default ON) */
    int             pause_with_stick; /* 1 = ignore gyro while look stick moves (default OFF) */
} wa_gyro_cfg_t;

/* Per-player persistent gyro runtime state */
typedef struct wa_gyro_state {
    int   available;     /* Non-zero if controller reported a gyro on last sample */
    float bias[3];       /* Calibration offset in deg/s (pitch, yaw, roll) */
    int   calibrated;    /* Non-zero once manual or auto calibration has settled */
    float calib_time;    /* Remaining manual calibration time in seconds */
    float calib_sum[3];  /* Running sum during manual calibration */
    int   calib_count;   /* Sample count during manual calibration */
    float still_time;    /* Seconds continuously below stillness threshold */
    float grav[3];       /* Smoothed unit gravity vector from accelerometer */
    float smooth[2];     /* Smoothed (yaw, pitch) rates in deg/s */
} wa_gyro_state_t;

/* Convenience per-player bundle */
typedef struct wa_player {
    wa_stick_cfg_t   stick_cfg;
    wa_stick_state_t stick_state;
    wa_gyro_cfg_t    gyro_cfg;
    wa_gyro_state_t  gyro_state;
} wa_player_t;

/* Raw controller motion sensor sample */
typedef struct wa_gyro_sensor {
    int   available;     /* 0 = no gyro on this controller */
    float gyro_deg_s[3]; /* Angular velocity in deg/s: { pitch, yaw, roll } */
    float accel[3];      /* Accelerometer reading (any non-zero gravity unit) */
} wa_gyro_sensor_t;

/* Precomputed data for rendering the Stick Response curve graph in any UI */
typedef struct wa_curve_graph {
    float y_max;                              /* max(sensitivity, 1.0f) */
    float inner_deadzone_x;                   /* [0..1], 0 for Classic */
    float outer_deadzone_x;                   /* [0..1], 1 for Classic */
    float grid_ticks[3];                      /* { 0.25f, 0.50f, 0.75f } */
    int   sample_count;                       /* Number of populated samples (<= WA_GRAPH_MAX_SAMPLES) */
    float curve_raw[WA_GRAPH_MAX_SAMPLES];    /* Curve output in turn-speed units */
    float curve_norm[WA_GRAPH_MAX_SAMPLES];   /* curve_raw / y_max, clamped to [0..1] */
    float linear_norm[WA_GRAPH_MAX_SAMPLES];  /* Linear reference (x / y_max), clamped to [0..1] */
    int   has_custom_handles;                 /* 1 if active curve is Custom 1-3 */
    float anchor0[2];                         /* Normalized graph coords (x, y) of (0,0) anchor */
    float control1[2];                        /* Normalized graph coords (x, y) of P1 */
    float control2[2];                        /* Normalized graph coords (x, y) of P2 */
    float anchor1[2];                         /* Normalized graph coords (x, y) of (1,1) anchor */
    int   has_live_marker;                    /* 1 if current stick deflection > 0.02 */
    float live_x;                             /* Normalized X of live stick deflection */
    float live_y;                             /* Normalized Y of live curve output */
} wa_curve_graph_t;

/* Metadata helpers */
const char* wa_curve_name(wa_curve_t curve);
const char* wa_boost_mode_name(wa_boost_mode_t mode);
const char* wa_gyro_mode_name(wa_gyro_mode_t mode);
const char* wa_gyro_space_name(wa_gyro_space_t space);
int         wa_is_custom_curve(wa_curve_t curve);

/* Defaults, initialization & separate reset helpers */
void wa_stick_cfg_defaults(wa_stick_cfg_t* cfg);
void wa_gyro_cfg_defaults(wa_gyro_cfg_t* cfg);
void wa_stick_state_init(wa_stick_state_t* st);
void wa_gyro_state_init(wa_gyro_state_t* st);
void wa_player_init(wa_player_t* player);

void wa_reset_stick_response(wa_stick_cfg_t* cfg);
void wa_reset_boost(wa_stick_cfg_t* cfg);
void wa_reset_gyro(wa_gyro_cfg_t* cfg);

/* Custom curve selection & editing */
void wa_select_curve(wa_stick_cfg_t* cfg, wa_curve_t curve);
void wa_curve_points_changed(wa_stick_cfg_t* cfg);

/* Pure math helpers */
float wa_bezier_eval(const float pts[4], float x);
float wa_curve_output(const wa_stick_cfg_t* cfg, float deflection);
int   wa_game_builtin_deadzone_wanted(const wa_stick_cfg_t* cfg, int force_original_aim);

/*
 * Process normalized look stick input (-1..+1 per axis) into turn rates.
 *
 * When force_original_aim != 0: returns 1:1 unmodded pass-through (stick_x, stick_y)
 * without mutating saved settings and clears boost runtime state.
 * When dt_sec <= 0 or non-finite: returns (0, 0) and preserves runtime state.
 */
void wa_process_stick(
    const wa_stick_cfg_t* cfg,
    wa_stick_state_t*     state,
    float                 stick_x,
    float                 stick_y,
    float                 dt_sec,
    int                   force_original_aim,
    float                 out_rate[2]);

/* Populate UI-agnostic graph geometry for the Stick Response menu */
void wa_build_curve_graph(
    const wa_stick_cfg_t* cfg,
    float                 current_stick_mag,
    int                   sample_count,
    wa_curve_graph_t*     out_graph);

/* Gyro calibration, status & menu tick helpers */
void             wa_gyro_start_calibration(wa_gyro_state_t* state);
wa_gyro_status_t wa_gyro_status(const wa_gyro_state_t* state);
const char*      wa_gyro_status_text(const wa_gyro_state_t* state);

/*
 * Advance gyro sampling and calibration while the settings menu is open (game paused).
 * Falls back to 1/60 s when real_dt_sec <= 0, > 0.25 s, or non-finite.
 */
void wa_gyro_menu_tick(
    const wa_gyro_cfg_t*    cfg,
    wa_gyro_state_t*        state,
    const wa_gyro_sensor_t* sensor,
    float                   real_dt_sec);

/*
 * Process one frame of gyro input into in-game camera rotation deltas (degrees).
 * out_deg[0] = yaw (+right), out_deg[1] = pitch (+up, flipped if invert_y).
 *
 * When force_original_aim != 0: returns (0, 0) and resets smoothing state without
 * mutating saved settings.
 * When dt_sec <= 0 or non-finite: returns (0, 0) and preserves runtime state.
 */
void wa_process_gyro(
    const wa_gyro_cfg_t*    cfg,
    wa_gyro_state_t*        state,
    const wa_gyro_sensor_t* sensor,
    int                     aiming,
    int                     stick_moving,
    float                   dt_sec,
    int                     force_original_aim,
    float                   out_deg[2]);

#ifdef __cplusplus
}
#endif

#endif /* WEIGHTY_AIM_H */
