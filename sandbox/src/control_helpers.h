#pragma once

#include "weighty_aim.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sb {

// Sandbox reticle style & appearance (high-contrast defaults visible on bright & dark surfaces)
enum class ReticleStyle : int {
    CrosshairDot = 0,
    Crosshair    = 1,
    Dot          = 2,
    CircleDot    = 3,
    Count        = 4,
};

struct ReticleConfig {
    ReticleStyle style             = ReticleStyle::CrosshairDot;
    float        arm_size          = 10.0f; // px (or circle radius in CircleDot mode)
    float        gap               = 4.0f;  // px
    float        thickness         = 2.4f;  // px
    float        dot_size          = 2.5f;  // px radius
    bool         outline           = true;
    float        outline_thickness = 1.6f;  // px dark border around strokes/dots
    float        color[3]          = {0.16f, 1.00f, 0.37f}; // Crisp neon green
    float        opacity           = 0.95f; // 0.10..1.00
};

// Sandbox-specific weighty-aim defaults (0.03 inner deadzone, 0.98 outer saturation radius,
// Balanced curve). Does not alter core weighty_aim.c defaults so core unit tests stay untouched.
constexpr float kSandboxWaInnerDeadzone = 0.03f;
constexpr float kSandboxWaOuterDeadzone = 0.98f;

inline void apply_sandbox_wa_defaults(wa_stick_cfg_t& cfg) {
    wa_stick_cfg_defaults(&cfg);
    wa_select_curve(&cfg, WA_CURVE_BALANCED);
    cfg.inner_deadzone = kSandboxWaInnerDeadzone;
    cfg.outer_deadzone = kSandboxWaOuterDeadzone;
}

inline void reset_sandbox_wa_stick_response(wa_stick_cfg_t& cfg) {
    wa_reset_stick_response(&cfg);
    wa_select_curve(&cfg, WA_CURVE_BALANCED);
    cfg.inner_deadzone = kSandboxWaInnerDeadzone;
    cfg.outer_deadzone = kSandboxWaOuterDeadzone;
}

// Latches rising-edge button presses across frames where render FPS > physics tick rate
// (so 0-substep frames never drop jump/crouch/dash edges before the next physics tick).
struct InputEdgeLatch {
    bool jump_pressed   = false;
    bool crouch_pressed = false;
    bool dash_pressed   = false;

    void accumulate(bool jump, bool crouch, bool dash) {
        jump_pressed   = jump_pressed   || jump;
        crouch_pressed = crouch_pressed || crouch;
        dash_pressed   = dash_pressed   || dash;
    }

    void clear() {
        jump_pressed   = false;
        crouch_pressed = false;
        dash_pressed   = false;
    }
};

// Continuous (Full-Auto) & Semi-Auto weapon cadence controller.
// - Full-Auto (default): holding Fire fires cleanly at 60.0 / fire_rpm seconds per shot.
//   Releasing & re-pressing respects remaining cooldown (no burst exploit), and pressing
//   after cooldown reaches 0 fires immediately with zero dropped first shot.
// - Semi-Auto: fires at most once per rising-edge press, also respecting fire_rpm cooldown.
struct WeaponFireController {
    bool  auto_fire    = true;
    float fire_rpm     = 600.0f; // 60..1500 RPM
    float cooldown_sec = 0.0f;

    float shot_interval_sec() const {
        const float rpm = std::clamp(fire_rpm, 60.0f, 1500.0f);
        return 60.0f / rpm;
    }

    void reset() {
        cooldown_sec = 0.0f;
    }

    // Returns number of shots triggered on this frame (typically 0 or 1; capped at 4 for huge dt).
    int update(float dt_sec, bool active, bool fire_held, bool fire_pressed) {
        const float dt = std::max(0.0f, dt_sec);
        const float interval = shot_interval_sec();

        if (!active) {
            cooldown_sec = std::max(0.0f, cooldown_sec - dt);
            return 0;
        }

        if (!auto_fire) {
            cooldown_sec = std::max(0.0f, cooldown_sec - dt);
            if (fire_pressed && cooldown_sec <= 0.0f) {
                cooldown_sec = interval;
                return 1;
            }
            return 0;
        }

        // Full-auto mode:
        // If trigger is not held (and not just pressed), decay cooldown cleanly down to 0.
        if (!fire_held && !fire_pressed) {
            cooldown_sec = std::max(0.0f, cooldown_sec - dt);
            return 0;
        }

        // Trigger is held or newly pressed: advance timer and fire whenever cooldown <= 0,
        // carrying fractional remainder so average cadence matches fire_rpm accurately.
        int shots = 0;
        if (cooldown_sec <= 0.0f) {
            // Immediate first shot on rising press or ready hold
            shots = 1;
            cooldown_sec = interval;
        } else {
            cooldown_sec -= dt;
            while (cooldown_sec <= 1e-6f && shots < 4) {
                shots++;
                cooldown_sec += interval;
            }
            if (cooldown_sec < 0.0f) {
                cooldown_sec = 0.0f;
            }
        }
        return shots;
    }
};

// Pure routing policy for Tuning Panel menu bypass & gamepad Start-button toggle.
struct MenuRoutingResult {
    bool allow_kbm     = false; // Keyboard + Mouse drive game simulation
    bool allow_gamepad = false; // Gamepad sticks, triggers, buttons & gyro drive game simulation
    bool toggle_ui     = false; // Rising-edge Start button requested toggling ui_open
};

struct MenuInputRouter {
    bool prev_start = false;

    void reset() {
        prev_start = false;
    }

    MenuRoutingResult evaluate(
        bool window_focused,
        bool ui_open,
        bool fpv_overlay_wants_input,
        bool controller_bypasses_menu,
        bool start_held) {
        MenuRoutingResult r{};
        // Start button rising edge always toggles the menu cleanly regardless of bypass setting
        r.toggle_ui = start_held && !prev_start;
        prev_start = start_held;

        const bool effective_ui_open = r.toggle_ui ? !ui_open : ui_open;
        r.allow_kbm = window_focused && !effective_ui_open && !fpv_overlay_wants_input;
        r.allow_gamepad = window_focused && !fpv_overlay_wants_input &&
                          (!effective_ui_open || controller_bypasses_menu);
        return r;
    }
};

// Aiming Range live accuracy & hit-streak statistics
struct AimStats {
    int shots_fired    = 0;
    int target_hits    = 0;
    int current_streak = 0;
    int best_streak    = 0;

    float accuracy_pct() const {
        return shots_fired > 0
            ? (100.0f * static_cast<float>(target_hits) / static_cast<float>(shots_fired))
            : 0.0f;
    }

    void record_shot(bool hit_target) {
        shots_fired++;
        if (hit_target) {
            target_hits++;
            current_streak++;
            if (current_streak > best_streak) {
                best_streak = current_streak;
            }
        } else {
            current_streak = 0;
        }
    }

    void reset() {
        shots_fired    = 0;
        target_hits    = 0;
        current_streak = 0;
        best_streak    = 0;
    }
};

// Mode-2 gamepad -> Betaflight RC channel mapper matching libfpv's mapGamepad():
//   - Self-centering left stick Y: center (0) maps to idle throttle (-1.0), +1 maps to full (+1.0)
//   - Optional non-centering throttle gimbal mode maps full [-1, +1] directly
//   - Rising-edge latched toggles for Arm (South) and Turtle (North)
struct FpvGamepadInput {
    float left_x  = 0.0f; // Yaw
    float left_y  = 0.0f; // Throttle (+up)
    float right_x = 0.0f; // Roll
    float right_y = 0.0f; // Pitch (+up)
    bool  btn_arm    = false; // South (A / Cross)
    bool  btn_beeper = false; // East (B / Circle)
    bool  btn_turtle = false; // North (Y / Triangle)
};

struct FpvGamepadRcMapper {
    bool armed           = false;
    bool turtle          = false;
    bool prev_arm_btn    = false;
    bool prev_turtle_btn = false;
    bool non_centering_throttle = false;
    float deadband_radius       = 0.08f;

    static float apply_deadband(float v, float db) {
        if (std::fabs(v) < db) return 0.0f;
        return std::clamp((v - std::copysign(db, v)) / (1.0f - db), -1.0f, 1.0f);
    }

    void reset_switches() {
        armed  = false;
        turtle = false;
    }

    // Fills AETR + AUX1..AUX6 (count >= 10)
    void map_channels(const FpvGamepadInput& in, bool angle_mode, bool show_osd, float out_ch[10]) {
        if (in.btn_arm && !prev_arm_btn) {
            armed = !armed;
        }
        prev_arm_btn = in.btn_arm;

        if (in.btn_turtle && !prev_turtle_btn) {
            turtle = !turtle;
        }
        prev_turtle_btn = in.btn_turtle;

        float thr = -1.0f;
        if (non_centering_throttle) {
            thr = std::clamp(in.left_y, -1.0f, 1.0f);
        } else {
            const float pos = std::max(0.0f, apply_deadband(in.left_y, deadband_radius));
            thr = pos * 2.0f - 1.0f; // Center (0) -> -1.0 idle, full up (+1) -> +1.0
        }

        out_ch[0] = apply_deadband(in.right_x, deadband_radius); // Aileron (Roll)
        out_ch[1] = apply_deadband(in.right_y, deadband_radius); // Elevator (Pitch)
        out_ch[2] = thr;                                         // Throttle
        out_ch[3] = apply_deadband(in.left_x,  deadband_radius); // Rudder (Yaw)
        out_ch[4] = armed ? 1.0f : -1.0f;                        // AUX1: ARM
        out_ch[5] = angle_mode ? 1.0f : -1.0f;                   // AUX2: ANGLE mode
        out_ch[6] = in.btn_beeper ? 1.0f : -1.0f;                // AUX3: BEEPER
        out_ch[7] = turtle ? 1.0f : -1.0f;                       // AUX4: TURTLE
        out_ch[8] = 1.0f;                                        // AUX5: AIR MODE
        out_ch[9] = show_osd ? -1.0f : 1.0f;                     // AUX6: OSD DISABLE
    }
};

} // namespace sb
