#pragma once

#include <SDL3/SDL.h>
#include "weighty_aim.h"

#include <string>

namespace sb {

struct InputFrame {
    // Movement wish in [-1, 1]
    float move_forward = 0.0f;
    float move_right   = 0.0f;

    // Action states (held & rising edges)
    bool jump_held      = false;
    bool jump_pressed   = false;
    bool crouch_held    = false;
    bool crouch_pressed = false;
    bool dash_pressed   = false;
    bool fire_held      = false;
    bool fire_pressed   = false;
    bool ads_held       = false;

    // Raw mouse look delta accumulated since last step (pixels)
    float mouse_dx = 0.0f;
    float mouse_dy = 0.0f;

    // Raw un-deadzoned right look stick [-1, 1] (+X right, +Y up)
    float look_stick_x = 0.0f;
    float look_stick_y = 0.0f;

    // Raw un-deadzoned left stick [-1, 1] (used for FPV throttle/yaw too)
    float left_stick_x = 0.0f;
    float left_stick_y = 0.0f;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;

    // Controller motion sensors (degrees/sec gyro, m/s^2 accel)
    wa_gyro_sensor_t sensor{};

    // Gamepad buttons useful for FPV AUX switches
    bool gp_south = false;
    bool gp_east  = false;
    bool gp_west  = false;
    bool gp_north = false;
    bool gp_lb    = false;
    bool gp_rb    = false;
};

class InputManager {
public:
    InputManager() = default;
    ~InputManager();

    void init();
    void shutdown();

    void begin_frame();
    void process_event(const SDL_Event& e, bool mouse_captured);

    // Build current input snapshot (updating edge trackers cleanly)
    InputFrame sample(bool mouse_captured);
    // Reset mouse delta accumulator after consuming in simulation step
    void consume_mouse_delta();

    bool        has_gamepad() const { return gamepad_ != nullptr; }
    std::string gamepad_name() const { return gamepad_name_; }
    bool        has_gyro() const { return has_gyro_; }
    bool        has_accel() const { return has_accel_; }
    const wa_gyro_sensor_t& last_sensor() const { return sensor_; }

private:
    void try_open_gamepad(SDL_JoystickID id);
    void close_gamepad();
    void scan_existing_gamepads();

    SDL_Gamepad*     gamepad_      = nullptr;
    SDL_JoystickID   gamepad_id_   = 0;
    std::string      gamepad_name_;
    bool             has_gyro_     = false;
    bool             has_accel_    = false;
    wa_gyro_sensor_t sensor_{};

    float mouse_dx_accum_ = 0.0f;
    float mouse_dy_accum_ = 0.0f;
    bool  lmb_held_       = false;
    bool  rmb_held_       = false;

    bool prev_jump_   = false;
    bool prev_crouch_ = false;
    bool prev_dash_   = false;
    bool prev_fire_   = false;
};

} // namespace sb
