#include "sandbox_settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sb {

static std::string trim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

static bool parse_bool(const std::string& val, bool fallback) {
    if (val == "1" || val == "true" || val == "TRUE" || val == "on" || val == "ON") return true;
    if (val == "0" || val == "false" || val == "FALSE" || val == "off" || val == "OFF") return false;
    return fallback;
}

static float parse_float(const std::string& val, float fallback, float lo, float hi) {
    char* end = nullptr;
    const float v = std::strtof(val.c_str(), &end);
    if (!end || end == val.c_str() || !std::isfinite(v)) return fallback;
    return std::clamp(v, lo, hi);
}

static int parse_int(const std::string& val, int fallback, int lo, int hi) {
    char* end = nullptr;
    const long v = std::strtol(val.c_str(), &end, 10);
    if (!end || end == val.c_str()) return fallback;
    return std::clamp(static_cast<int>(v), lo, hi);
}

std::string serialize_sandbox_settings_ini(const SandboxSettings& s) {
    std::ostringstream oss;
    oss << "# portable-mods Testing Sandbox local settings (gitignored)\n\n";

    oss << "[weapon]\n";
    oss << "auto_fire=" << (s.auto_fire ? 1 : 0) << "\n";
    oss << "fire_rpm=" << s.fire_rpm << "\n";
    oss << "gun_audio_volume=" << s.gun_audio_volume << "\n";
    oss << "gun_audio_muted=" << (s.gun_audio_muted ? 1 : 0) << "\n";
    oss << "hit_confirm_sound=" << (s.hit_confirm_sound ? 1 : 0) << "\n\n";

    oss << "[display]\n";
    oss << "fullscreen=" << (s.fullscreen ? 1 : 0) << "\n";
    oss << "base_world_fovy_deg=" << s.base_world_fovy_deg << "\n";
    oss << "viewmodel_fovy_deg=" << s.viewmodel_fovy_deg << "\n";
    oss << "mouse_sens_deg_per_px=" << s.mouse_sens_deg_per_px << "\n";
    oss << "show_hud=" << (s.show_hud ? 1 : 0) << "\n";
    oss << "ads_toggle_mode=" << (s.ads_toggle_mode ? 1 : 0) << "\n\n";

    oss << "[controls]\n";
    oss << "controller_bypasses_menu=" << (s.controller_bypasses_menu ? 1 : 0) << "\n\n";

    oss << "[weighty_aim]\n";
    oss << "wa_curve=" << s.wa_curve << "\n";
    oss << "wa_inner_deadzone=" << s.wa_inner_deadzone << "\n";
    oss << "wa_outer_deadzone=" << s.wa_outer_deadzone << "\n";
    oss << "wa_sensitivity=" << s.wa_sensitivity << "\n";
    oss << "wa_vertical_sens=" << s.wa_vertical_sens << "\n";
    oss << "wa_full_stick_deg_per_sec=" << s.wa_full_stick_deg_per_sec << "\n";
    oss << "wa_gyro_mode=" << s.wa_gyro_mode << "\n";
    oss << "wa_gyro_sensitivity=" << s.wa_gyro_sensitivity << "\n\n";

    oss << "[presets]\n";
    oss << "qm_preset=" << s.qm_preset << "\n";
    oss << "physics_tick_hz=" << s.physics_tick_hz << "\n";
    oss << "fa_preset=" << s.fa_preset << "\n";
    oss << "fpv_audio_volume=" << s.fpv_audio_volume << "\n";
    oss << "fpv_audio_muted=" << (s.fpv_audio_muted ? 1 : 0) << "\n";
    return oss.str();
}

bool deserialize_sandbox_settings_ini(const std::string& text, SandboxSettings& out) {
    SandboxSettings s = out; // Start from current/defaults so missing keys preserve defaults
    std::istringstream iss(text);
    std::string line;
    int parsed_keys = 0;

    while (std::getline(iss, line)) {
        // Strip inline comments
        const size_t hash_pos = line.find('#');
        if (hash_pos != std::string::npos) line.resize(hash_pos);
        const size_t semi_pos = line.find(';');
        if (semi_pos != std::string::npos) line.resize(semi_pos);

        line = trim_ws(line);
        if (line.empty() || line[0] == '[') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim_ws(line.substr(0, eq));
        const std::string val = trim_ws(line.substr(eq + 1));
        if (key.empty() || val.empty()) continue;

        if (key == "auto_fire")                      { s.auto_fire = parse_bool(val, s.auto_fire); parsed_keys++; }
        else if (key == "fire_rpm")                  { s.fire_rpm = parse_float(val, s.fire_rpm, 60.0f, 1500.0f); parsed_keys++; }
        else if (key == "gun_audio_volume")          { s.gun_audio_volume = parse_float(val, s.gun_audio_volume, 0.0f, 1.5f); parsed_keys++; }
        else if (key == "gun_audio_muted")           { s.gun_audio_muted = parse_bool(val, s.gun_audio_muted); parsed_keys++; }
        else if (key == "hit_confirm_sound")         { s.hit_confirm_sound = parse_bool(val, s.hit_confirm_sound); parsed_keys++; }
        else if (key == "fullscreen")                { s.fullscreen = parse_bool(val, s.fullscreen); parsed_keys++; }
        else if (key == "base_world_fovy_deg")       { s.base_world_fovy_deg = parse_float(val, s.base_world_fovy_deg, 50.0f, 110.0f); parsed_keys++; }
        else if (key == "viewmodel_fovy_deg")        { s.viewmodel_fovy_deg = parse_float(val, s.viewmodel_fovy_deg, 35.0f, 90.0f); parsed_keys++; }
        else if (key == "mouse_sens_deg_per_px")     { s.mouse_sens_deg_per_px = parse_float(val, s.mouse_sens_deg_per_px, 0.02f, 0.40f); parsed_keys++; }
        else if (key == "show_hud")                  { s.show_hud = parse_bool(val, s.show_hud); parsed_keys++; }
        else if (key == "ads_toggle_mode")           { s.ads_toggle_mode = parse_bool(val, s.ads_toggle_mode); parsed_keys++; }
        else if (key == "controller_bypasses_menu")  { s.controller_bypasses_menu = parse_bool(val, s.controller_bypasses_menu); parsed_keys++; }
        else if (key == "wa_curve")                  { s.wa_curve = parse_int(val, s.wa_curve, 0, WA_CURVE_COUNT - 1); parsed_keys++; }
        else if (key == "wa_inner_deadzone")         { s.wa_inner_deadzone = parse_float(val, s.wa_inner_deadzone, 0.0f, 0.40f); parsed_keys++; }
        else if (key == "wa_outer_deadzone")         { s.wa_outer_deadzone = parse_float(val, s.wa_outer_deadzone, 0.10f, 1.00f); parsed_keys++; }
        else if (key == "wa_sensitivity")            { s.wa_sensitivity = parse_float(val, s.wa_sensitivity, 0.25f, 2.50f); parsed_keys++; }
        else if (key == "wa_vertical_sens")          { s.wa_vertical_sens = parse_float(val, s.wa_vertical_sens, 0.25f, 2.00f); parsed_keys++; }
        else if (key == "wa_full_stick_deg_per_sec") { s.wa_full_stick_deg_per_sec = parse_float(val, s.wa_full_stick_deg_per_sec, 60.0f, 450.0f); parsed_keys++; }
        else if (key == "wa_gyro_mode")              { s.wa_gyro_mode = parse_int(val, s.wa_gyro_mode, 0, WA_GYRO_COUNT - 1); parsed_keys++; }
        else if (key == "wa_gyro_sensitivity")       { s.wa_gyro_sensitivity = parse_float(val, s.wa_gyro_sensitivity, 0.1f, 20.0f); parsed_keys++; }
        else if (key == "qm_preset")                 { s.qm_preset = parse_int(val, s.qm_preset, 0, QM_PRESET_COUNT - 1); parsed_keys++; }
        else if (key == "physics_tick_hz") {
            const int hz = parse_int(val, s.physics_tick_hz, 60, 240);
            s.physics_tick_hz = (hz <= 90) ? 60 : (hz >= 180 ? 240 : 120);
            parsed_keys++;
        }
        else if (key == "fa_preset")                 { s.fa_preset = parse_int(val, s.fa_preset, 0, FA_PRESET_COUNT - 1); parsed_keys++; }
        else if (key == "fpv_audio_volume")          { s.fpv_audio_volume = parse_float(val, s.fpv_audio_volume, 0.0f, 1.5f); parsed_keys++; }
        else if (key == "fpv_audio_muted")           { s.fpv_audio_muted = parse_bool(val, s.fpv_audio_muted); parsed_keys++; }
    }

    if (s.wa_outer_deadzone < s.wa_inner_deadzone + 0.05f) {
        s.wa_outer_deadzone = std::min(1.0f, s.wa_inner_deadzone + 0.05f);
    }

    out = s;
    return parsed_keys > 0;
}

bool save_sandbox_settings_file(const std::string& path, const SandboxSettings& s) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    const std::string data = serialize_sandbox_settings_ini(s);
    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

bool load_sandbox_settings_file(const std::string& path, SandboxSettings& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return deserialize_sandbox_settings_ini(ss.str(), out);
}

} // namespace sb
