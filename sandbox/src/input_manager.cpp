#include "input_manager.h"
#include "math_types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sb {

InputManager::~InputManager() {
    shutdown();
}

void InputManager::init() {
    scan_existing_gamepads();
}

void InputManager::shutdown() {
    close_gamepad();
}

void InputManager::scan_existing_gamepads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        if (!gamepad_) {
            try_open_gamepad(ids[i]);
        }
    }
    SDL_free(ids);
}

void InputManager::try_open_gamepad(SDL_JoystickID id) {
    if (gamepad_) return;
    SDL_Gamepad* gp = SDL_OpenGamepad(id);
    if (!gp) return;

    gamepad_    = gp;
    gamepad_id_ = id;
    const char* name = SDL_GetGamepadName(gp);
    gamepad_name_ = name ? name : "Gamepad";

    has_gyro_  = SDL_GamepadHasSensor(gp, SDL_SENSOR_GYRO);
    has_accel_ = SDL_GamepadHasSensor(gp, SDL_SENSOR_ACCEL);
    if (has_gyro_)  SDL_SetGamepadSensorEnabled(gp, SDL_SENSOR_GYRO, true);
    if (has_accel_) SDL_SetGamepadSensorEnabled(gp, SDL_SENSOR_ACCEL, true);

    sensor_ = {};
    sensor_.available = has_gyro_ ? 1 : 0;
    last_raw_right_stick_[0] = 0.0f;
    last_raw_right_stick_[1] = 0.0f;
    SDL_Log("[input] Gamepad connected: %s (gyro=%s, accel=%s)",
            gamepad_name_.c_str(), has_gyro_ ? "yes" : "no", has_accel_ ? "yes" : "no");
}

void InputManager::close_gamepad() {
    if (gamepad_) {
        SDL_Log("[input] Gamepad disconnected: %s", gamepad_name_.c_str());
        SDL_CloseGamepad(gamepad_);
        gamepad_    = nullptr;
        gamepad_id_ = 0;
        gamepad_name_.clear();
        has_gyro_   = false;
        has_accel_  = false;
        sensor_     = {};
        last_raw_right_stick_[0] = 0.0f;
        last_raw_right_stick_[1] = 0.0f;
    }
}

void InputManager::begin_frame() {
    // Accumulators persist until consumed by simulation step
}

bool InputManager::is_start_held() const {
    if (!gamepad_) return false;
    return SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_START);
}

void InputManager::process_event(const SDL_Event& e, bool mouse_captured) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        try_open_gamepad(e.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad_ && e.gdevice.which == gamepad_id_) {
            close_gamepad();
            scan_existing_gamepads();
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (mouse_captured) {
            mouse_dx_accum_ += e.motion.xrel;
            mouse_dy_accum_ += e.motion.yrel;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (mouse_captured) {
            if (e.button.button == SDL_BUTTON_LEFT)  lmb_held_ = true;
            if (e.button.button == SDL_BUTTON_RIGHT) {
                if (!rmb_held_) rmb_pressed_accum_ = true;
                rmb_held_ = true;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT)  lmb_held_ = false;
        if (e.button.button == SDL_BUTTON_RIGHT) rmb_held_ = false;
        break;
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        if (gamepad_ && e.gsensor.which == gamepad_id_) {
            if (e.gsensor.sensor == SDL_SENSOR_GYRO) {
                // SDL reports rad/s; weighty_aim expects deg/s
                sensor_.available     = 1;
                sensor_.gyro_deg_s[0] = e.gsensor.data[0] * kRadToDeg;
                sensor_.gyro_deg_s[1] = e.gsensor.data[1] * kRadToDeg;
                sensor_.gyro_deg_s[2] = e.gsensor.data[2] * kRadToDeg;
            } else if (e.gsensor.sensor == SDL_SENSOR_ACCEL) {
                sensor_.accel[0] = e.gsensor.data[0];
                sensor_.accel[1] = e.gsensor.data[1];
                sensor_.accel[2] = e.gsensor.data[2];
            }
        }
        break;
    default:
        break;
    }
}

static inline float normalize_axis(Sint16 raw) {
    // Raw un-deadzoned conversion to [-1, +1] so weighty_aim owns radial deadzones exclusively
    return raw < 0 ? (static_cast<float>(raw) / 32768.0f) : (static_cast<float>(raw) / 32767.0f);
}

InputFrame InputManager::sample(bool allow_kbm, bool allow_gamepad) {
    InputFrame out{};
    const bool* keys = SDL_GetKeyboardState(nullptr);

    float move_fwd = 0.0f;
    float move_rgt = 0.0f;
    if (allow_kbm && keys) {
        if (keys[SDL_SCANCODE_W]) move_fwd += 1.0f;
        if (keys[SDL_SCANCODE_S]) move_fwd -= 1.0f;
        if (keys[SDL_SCANCODE_D]) move_rgt += 1.0f;
        if (keys[SDL_SCANCODE_A]) move_rgt -= 1.0f;
    }

    bool jump_now   = allow_kbm && keys && keys[SDL_SCANCODE_SPACE];
    bool crouch_now = allow_kbm && keys && (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_C]);
    bool dash_now   = allow_kbm && keys && keys[SDL_SCANCODE_LSHIFT];
    bool fire_now   = allow_kbm && lmb_held_;

    const bool raw_kbm_ads     = rmb_held_;
    const bool kbm_ads_now     = allow_kbm && raw_kbm_ads;
    const bool kbm_ads_pressed = allow_kbm && (rmb_pressed_accum_ || (raw_kbm_ads && !prev_kbm_ads_));
    rmb_pressed_accum_ = false;

    bool raw_gp_ads     = false;
    bool gp_ads_now     = false;
    bool gp_ads_pressed = false;

    if (gamepad_) {
        // Always refresh motion sensors & raw right stick for live UI diagnostics / gyro calibration
        if (has_gyro_) {
            float g[3] = {0, 0, 0};
            if (SDL_GetGamepadSensorData(gamepad_, SDL_SENSOR_GYRO, g, 3)) {
                sensor_.available     = 1;
                sensor_.gyro_deg_s[0] = g[0] * kRadToDeg;
                sensor_.gyro_deg_s[1] = g[1] * kRadToDeg;
                sensor_.gyro_deg_s[2] = g[2] * kRadToDeg;
            }
        }
        if (has_accel_) {
            float a[3] = {0, 0, 0};
            if (SDL_GetGamepadSensorData(gamepad_, SDL_SENSOR_ACCEL, a, 3)) {
                sensor_.accel[0] = a[0];
                sensor_.accel[1] = a[1];
                sensor_.accel[2] = a[2];
            }
        }

        // SDL Y axes are +down; flip to +up so stick_y = +1 pitches up / moves forward
        const float raw_rx =  normalize_axis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTX));
        const float raw_ry = -normalize_axis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHTY));
        last_raw_right_stick_[0] = raw_rx;
        last_raw_right_stick_[1] = raw_ry;

        out.gp_start = SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_START);

        const float raw_lt = std::clamp(normalize_axis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)), 0.0f, 1.0f);
        raw_gp_ads         = (raw_lt > 0.35f);
        gp_ads_now         = allow_gamepad && raw_gp_ads;
        gp_ads_pressed     = allow_gamepad && raw_gp_ads && !prev_gp_ads_;

        if (allow_gamepad) {
            out.left_stick_x  =  normalize_axis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX));
            out.left_stick_y  = -normalize_axis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY));
            out.look_stick_x  = raw_rx;
            out.look_stick_y  = raw_ry;
            out.left_trigger  = raw_lt;
            out.right_trigger = std::clamp(normalize_axis(SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)), 0.0f, 1.0f);

            out.gp_south = SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_SOUTH);
            out.gp_east  = SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_EAST);
            out.gp_west  = SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_WEST);
            out.gp_north = SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_NORTH);
            out.gp_lb    = SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            out.gp_rb    = SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

            // Apply tiny movement stick deadzone only for locomotion (never for look stick!)
            const float ls_mag = std::hypot(out.left_stick_x, out.left_stick_y);
            if (ls_mag > 0.12f) {
                move_rgt = std::clamp(move_rgt + out.left_stick_x, -1.0f, 1.0f);
                move_fwd = std::clamp(move_fwd + out.left_stick_y, -1.0f, 1.0f);
            }

            jump_now   = jump_now   || out.gp_south;
            crouch_now = crouch_now || out.gp_east || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
            dash_now   = dash_now   || out.gp_lb || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_LEFT_STICK);
            fire_now   = fire_now   || (out.right_trigger > 0.45f);
        }
    }

    out.move_forward    = move_fwd;
    out.move_right      = move_rgt;
    out.jump_held       = jump_now;
    out.jump_pressed    = jump_now && !prev_jump_;
    out.crouch_held     = crouch_now;
    out.crouch_pressed  = crouch_now && !prev_crouch_;
    out.dash_pressed    = dash_now && !prev_dash_;
    out.fire_held       = fire_now;
    out.fire_pressed    = fire_now && !prev_fire_;
    out.ads_held        = kbm_ads_now || gp_ads_now;
    out.kbm_ads_pressed = kbm_ads_pressed;
    out.gp_ads_pressed  = gp_ads_pressed;
    out.ads_pressed     = kbm_ads_pressed || gp_ads_pressed;

    out.mouse_dx = allow_kbm ? mouse_dx_accum_ : 0.0f;
    out.mouse_dy = allow_kbm ? mouse_dy_accum_ : 0.0f;
    out.sensor   = sensor_;

    // Always track edges even while paused/unlocked to prevent phantom edges on resume
    prev_jump_    = jump_now;
    prev_crouch_  = crouch_now;
    prev_dash_    = dash_now;
    prev_fire_    = fire_now;
    prev_kbm_ads_ = raw_kbm_ads;
    prev_gp_ads_  = raw_gp_ads;

    return out;
}

void InputManager::consume_mouse_delta() {
    mouse_dx_accum_ = 0.0f;
    mouse_dy_accum_ = 0.0f;
}

} // namespace sb
