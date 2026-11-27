#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sb {

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
