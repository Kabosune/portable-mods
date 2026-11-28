#pragma once

#include "control_helpers.h"
#include "free_aim.h"
#include "qm_physics.h"
#include "weighty_aim.h"

#include <string>

namespace sb {

// Serializable subset of sandbox preferences saved to plain-text INI (`sandbox_settings.ini`).
struct SandboxSettings {
    // Weapon & Audio
    bool  auto_fire            = true;
    float fire_rpm             = 600.0f;
    float gun_audio_volume     = 0.70f;
    bool  gun_audio_muted      = false;
    bool  hit_confirm_sound    = true;

    // Display & Camera
    bool  fullscreen           = false;
    float base_world_fovy_deg  = 75.0f;
    float viewmodel_fovy_deg   = 54.0f;
    float mouse_sens_deg_per_px = 0.11f;
    bool  show_hud             = true;
    bool  ads_toggle_mode      = false;

    // Controls
    bool  controller_bypasses_menu = true;

    // weighty-aim key stick/gyro preferences (sandbox defaults: 0.03 inner, 0.98 outer)
    int   wa_curve                  = static_cast<int>(WA_CURVE_BALANCED);
    float wa_inner_deadzone         = kSandboxWaInnerDeadzone;
    float wa_outer_deadzone         = kSandboxWaOuterDeadzone;
    float wa_sensitivity            = 1.0f;
    float wa_vertical_sens          = 0.80f;
    float wa_full_stick_deg_per_sec = 210.0f;
    int   wa_gyro_mode              = static_cast<int>(WA_GYRO_OFF);
    float wa_gyro_sensitivity       = 2.5f;

    // Active presets & physics rate
    int   qm_preset       = static_cast<int>(QM_PRESET_QUAKE_VQ3);
    int   physics_tick_hz = 120;
    int   fa_preset       = static_cast<int>(FA_PRESET_WEIGHTY);

    // FPV audio preferences
    float fpv_audio_volume = 0.65f;
    bool  fpv_audio_muted  = false;
};

// Serialize/deserialize to/from plain-text INI string or file path.
std::string serialize_sandbox_settings_ini(const SandboxSettings& s);
bool        deserialize_sandbox_settings_ini(const std::string& text, SandboxSettings& out);

bool save_sandbox_settings_file(const std::string& path, const SandboxSettings& s);
bool load_sandbox_settings_file(const std::string& path, SandboxSettings& out);

} // namespace sb
