#include "ui_hud.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sb {

static constexpr float kMpsToUps = 33.68f; // Quake 3 conversion (9.5 m/s ~= 320 UPS)

static void draw_curve_graph_widget(const wa_stick_cfg_t& cfg, float live_mag, ImVec2 size) {
    wa_curve_graph_t g{};
    wa_build_curve_graph(&cfg, live_mag, 65, &g);

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto to_pt = [&](float nx, float ny) -> ImVec2 {
        return ImVec2(p0.x + nx * size.x, p1.y - ny * size.y);
    };

    // Background & border
    dl->AddRectFilled(p0, p1, IM_COL32(18, 22, 28, 240), 4.0f);
    dl->AddRect(p0, p1, IM_COL32(95, 110, 130, 220), 4.0f);

    // Inner & outer deadzone bands (non-Classic)
    if (g.inner_deadzone_x > 0.001f) {
        dl->AddRectFilled(p0, ImVec2(p0.x + g.inner_deadzone_x * size.x, p1.y), IM_COL32(210, 70, 70, 52));
    }
    if (g.outer_deadzone_x < 0.999f) {
        dl->AddRectFilled(ImVec2(p0.x + g.outer_deadzone_x * size.x, p0.y), p1, IM_COL32(70, 145, 235, 56));
    }

    // 25 / 50 / 75% grid lines
    for (int i = 0; i < 3; ++i) {
        const float t = g.grid_ticks[i];
        dl->AddLine(to_pt(t, 0.0f), to_pt(t, 1.0f), IM_COL32(255, 255, 255, 28));
        dl->AddLine(to_pt(0.0f, t), to_pt(1.0f, t), IM_COL32(255, 255, 255, 28));
    }

    // Linear reference
    if (g.sample_count >= 2) {
        for (int i = 0; i + 1 < g.sample_count; i += 2) {
            const float x0 = static_cast<float>(i) / static_cast<float>(g.sample_count - 1);
            const float x1 = static_cast<float>(i + 1) / static_cast<float>(g.sample_count - 1);
            dl->AddLine(to_pt(x0, g.linear_norm[i]), to_pt(x1, g.linear_norm[i + 1]), IM_COL32(140, 155, 170, 120), 1.2f);
        }

        // Active response curve
        for (int i = 0; i + 1 < g.sample_count; ++i) {
            const float x0 = static_cast<float>(i) / static_cast<float>(g.sample_count - 1);
            const float x1 = static_cast<float>(i + 1) / static_cast<float>(g.sample_count - 1);
            dl->AddLine(to_pt(x0, g.curve_norm[i]), to_pt(x1, g.curve_norm[i + 1]), IM_COL32(76, 224, 168, 255), 2.4f);
        }
    }

    // Custom Bezier control handles
    if (g.has_custom_handles) {
        const ImVec2 a0 = to_pt(g.anchor0[0],  g.anchor0[1]);
        const ImVec2 c1 = to_pt(g.control1[0], g.control1[1]);
        const ImVec2 c2 = to_pt(g.control2[0], g.control2[1]);
        const ImVec2 a1 = to_pt(g.anchor1[0],  g.anchor1[1]);
        dl->AddLine(a0, c1, IM_COL32(255, 195, 80, 180), 1.4f);
        dl->AddLine(a1, c2, IM_COL32(255, 195, 80, 180), 1.4f);
        dl->AddCircleFilled(c1, 4.5f, IM_COL32(255, 195, 80, 255));
        dl->AddCircleFilled(c2, 4.5f, IM_COL32(255, 195, 80, 255));
    }

    // Live stick deflection marker
    if (g.has_live_marker) {
        const ImVec2 lm = to_pt(g.live_x, g.live_y);
        dl->AddCircleFilled(lm, 4.8f, IM_COL32(255, 95, 95, 255));
        dl->AddCircle(lm, 6.8f, IM_COL32(255, 255, 255, 220), 12, 1.5f);
    }

    char ylbl[48];
    std::snprintf(ylbl, sizeof(ylbl), "Response (y_max %.2fx)", g.y_max);
    dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 4.0f), IM_COL32(200, 212, 226, 215), ylbl);
    ImGui::Dummy(size);
}

static void draw_stick_visualizer_widget(
    const wa_stick_cfg_t& cfg,
    const float raw_xy[2],
    const float out_xy[2],
    float side_px) {
    const ImVec2 size(side_px, side_px);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(p0, p1, IM_COL32(18, 22, 28, 240), 4.0f);
    dl->AddRect(p0, p1, IM_COL32(95, 110, 130, 220), 4.0f);

    const ImVec2 center(p0.x + size.x * 0.5f, p0.y + size.y * 0.5f + 4.0f);
    const float radius = size.x * 0.40f;

    auto stick_to_pt = [&](float x, float y) -> ImVec2 {
        return ImVec2(center.x + x * radius, center.y - y * radius);
    };

    // Axes
    dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(255, 255, 255, 36));
    dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), IM_COL32(255, 255, 255, 36));

    // Outer unit circle (1.0)
    dl->AddCircle(center, radius, IM_COL32(145, 160, 180, 175), 48, 1.3f);

    // Outer & inner deadzone rings (active for non-Classic curves)
    const bool classic = (cfg.curve == WA_CURVE_CLASSIC);
    const float dz_in  = classic ? 0.0f : std::clamp(cfg.inner_deadzone, 0.0f, 0.40f);
    const float dz_out = classic ? 1.0f : std::clamp(cfg.outer_deadzone, std::min(1.0f, dz_in + 0.05f), 1.0f);

    if (dz_out < 0.999f) {
        dl->AddCircle(center, radius * dz_out, IM_COL32(82, 168, 255, 220), 48, 1.5f);
    }
    if (dz_in > 0.001f) {
        const float r_in = std::max(2.0f, radius * dz_in);
        dl->AddCircleFilled(center, r_in, IM_COL32(225, 78, 78, 72), 32);
        dl->AddCircle(center, r_in, IM_COL32(235, 92, 92, 220), 32, 1.4f);
    }

    // Live raw stick vector & dot (coral-orange)
    const float rx = std::clamp(raw_xy[0], -1.0f, 1.0f);
    const float ry = std::clamp(raw_xy[1], -1.0f, 1.0f);
    const ImVec2 raw_pt = stick_to_pt(rx, ry);
    if (std::hypot(rx, ry) > 0.005f) {
        dl->AddLine(center, raw_pt, IM_COL32(255, 118, 92, 185), 1.6f);
    }
    dl->AddCircleFilled(raw_pt, 4.2f, IM_COL32(255, 108, 84, 255));

    // Post-weighty-aim scaled output vector & dot (mint green, normalized by y_max)
    const float y_max = std::max(1.0f, cfg.sensitivity);
    const float ox = std::clamp(out_xy[0] / y_max, -1.0f, 1.0f);
    const float oy = std::clamp(out_xy[1] / y_max, -1.0f, 1.0f);
    const ImVec2 out_pt = stick_to_pt(ox, oy);
    if (std::hypot(ox, oy) > 0.005f) {
        dl->AddLine(center, out_pt, IM_COL32(76, 232, 168, 235), 2.2f);
    }
    dl->AddCircleFilled(out_pt, 4.6f, IM_COL32(76, 232, 168, 255));
    dl->AddCircle(out_pt, 6.2f, IM_COL32(255, 255, 255, 185), 16, 1.2f);

    // Compact title & deadzone labels
    char hdr[48];
    std::snprintf(hdr, sizeof(hdr), "Stick DZ %.2f/%.2f", dz_in, dz_out);
    dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 4.0f), IM_COL32(200, 212, 226, 215), hdr);

    char ftr[48];
    std::snprintf(ftr, sizeof(ftr), "R:%.2f,%.2f O:%.2f,%.2f", raw_xy[0], raw_xy[1], out_xy[0], out_xy[1]);
    dl->AddText(ImVec2(p0.x + 5.0f, p1.y - 16.0f), IM_COL32(165, 188, 210, 220), ftr);
    ImGui::Dummy(size);
}

static void draw_reticle_shape(ImDrawList* dl, float rx, float ry, const ReticleConfig& rc) {
    const float alpha_f = std::clamp(rc.opacity, 0.10f, 1.0f);
    const int   a_fill  = static_cast<int>(alpha_f * 255.0f + 0.5f);
    const int   a_out   = static_cast<int>(std::min(1.0f, alpha_f * 0.96f) * 255.0f + 0.5f);
    const ImU32 col = IM_COL32(
        static_cast<int>(std::clamp(rc.color[0], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<int>(std::clamp(rc.color[1], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<int>(std::clamp(rc.color[2], 0.0f, 1.0f) * 255.0f + 0.5f),
        a_fill);
    const ImU32 out_col = IM_COL32(0, 0, 0, a_out);

    const float arm  = std::clamp(rc.arm_size, 2.0f, 32.0f);
    const float gap  = std::clamp(rc.gap, 0.0f, 20.0f);
    const float ht   = std::clamp(rc.thickness, 1.0f, 6.0f) * 0.5f;
    const float ot   = rc.outline ? std::clamp(rc.outline_thickness, 0.5f, 4.0f) : 0.0f;
    const float dotr = std::clamp(rc.dot_size, 0.0f, 8.0f);

    const bool has_arms   = (rc.style == ReticleStyle::CrosshairDot || rc.style == ReticleStyle::Crosshair);
    const bool has_circle = (rc.style == ReticleStyle::CircleDot);
    const bool has_dot    = (rc.style == ReticleStyle::CrosshairDot || rc.style == ReticleStyle::Dot || rc.style == ReticleStyle::CircleDot) && dotr > 0.1f;

    auto arm_rect = [&](int idx, float pad, ImU32 c) {
        if (idx == 0) dl->AddRectFilled(ImVec2(rx - gap - arm - pad, ry - ht - pad), ImVec2(rx - gap + pad, ry + ht + pad), c);
        if (idx == 1) dl->AddRectFilled(ImVec2(rx + gap - pad, ry - ht - pad), ImVec2(rx + gap + arm + pad, ry + ht + pad), c);
        if (idx == 2) dl->AddRectFilled(ImVec2(rx - ht - pad, ry - gap - arm - pad), ImVec2(rx + ht + pad, ry - gap + pad), c);
        if (idx == 3) dl->AddRectFilled(ImVec2(rx - ht - pad, ry + gap - pad), ImVec2(rx + ht + pad, ry + gap + arm + pad), c);
    };

    // Pass 1: Solid dark outlines underneath so strokes stay crisp on bright & dark surfaces
    if (rc.outline && ot > 0.0f) {
        if (has_arms) {
            for (int i = 0; i < 4; ++i) arm_rect(i, ot, out_col);
        }
        if (has_circle) {
            const float rad = std::max(3.0f, arm + gap * 0.5f);
            dl->AddCircle(ImVec2(rx, ry), rad, out_col, 40, ht * 2.0f + ot * 2.0f);
        }
        if (has_dot) {
            dl->AddCircleFilled(ImVec2(rx, ry), dotr + ot, out_col, 24);
        }
    }

    // Pass 2: Bright high-contrast colored fill on top
    if (has_arms) {
        for (int i = 0; i < 4; ++i) arm_rect(i, 0.0f, col);
    }
    if (has_circle) {
        const float rad = std::max(3.0f, arm + gap * 0.5f);
        dl->AddCircle(ImVec2(rx, ry), rad, col, 40, ht * 2.0f);
    }
    if (has_dot) {
        dl->AddCircleFilled(ImVec2(rx, ry), dotr, col, 24);
    }
}

static void draw_reticle_preview_swatch(const ReticleConfig& rc) {
    const ImVec2 size(118.0f, 52.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    const float  mid_x = p0.x + size.x * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Split bright / dark background to inspect contrast on both surfaces simultaneously
    dl->AddRectFilled(p0, ImVec2(mid_x, p1.y), IM_COL32(215, 224, 234, 255), 3.0f, ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(mid_x, p0.y), p1, IM_COL32(24, 28, 36, 255), 3.0f, ImDrawFlags_RoundCornersRight);
    dl->AddRect(p0, p1, IM_COL32(95, 110, 130, 220), 3.0f);
    dl->PushClipRect(p0, p1, true);
    draw_reticle_shape(dl, mid_x, p0.y + size.y * 0.5f, rc);
    dl->PopClipRect();
    ImGui::Dummy(size);
}

static void draw_always_visible_hud(const SandboxApp& app, int win_w, int win_h) {
    if (!app.show_hud) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float w = static_cast<float>(win_w);
    const float h = static_cast<float>(win_h);
    const float aspect = w / std::max(1.0f, h);

    // 1. Top-left compact status bar
    {
        char line1[192];
        if (!app.fpv_mode) {
            std::snprintf(line1, sizeof(line1), "MODE: FPS  |  Move: %s  |  Aim: %s  |  %s %.0f RPM  |  %.0f FPS",
                          qm_preset_name(app.qm_preset),
                          fa_preset_name(app.fa_bank.cfg.preset),
                          app.weapon_fire.auto_fire ? "AUTO" : "SEMI",
                          app.weapon_fire.fire_rpm,
                          app.fps_smoothed);
        } else {
            const char* cam_str = (app.fpv_cam_view == FpvCamView::Fpv) ? "FPV"
                                : (app.fpv_cam_view == FpvCamView::Chase) ? "Chase" : "LOS";
            std::snprintf(line1, sizeof(line1), "MODE: FPV DRONE (%s)  |  %s  |  %.0f FPS",
                          cam_str,
                          (app.fpv.frame().flags & FPV_DRONE_ARMED) ? "ARMED" : "DISARMED",
                          app.fps_smoothed);
        }
        char line2[200];
        if (app.ui_open) {
            std::snprintf(line2, sizeof(line2),
                          "UI UNLOCKED%s  |  Tab/Esc/Start: Resume  |  F11: Fullscreen  |  F8: libfpv Menu",
                          app.controller_bypasses_menu ? " [Gamepad Bypass ON]" : "");
        } else if (app.fpv_mode) {
            std::snprintf(line2, sizeof(line2),
                          "Enter: Arm | W/S: Hover | A/D: Yaw | Arrows: Pitch/Roll | R: Reset Drone | V: Cam | F: FPS | F11: Full");
        } else {
            std::snprintf(line2, sizeof(line2),
                          "Tab/Esc/Start: Tuning UI  |  F: FPV Drone  |  R: Reset  |  F11: Fullscreen  |  F1: HUD");
        }
        const float box_w = std::max(ImGui::CalcTextSize(line1).x, ImGui::CalcTextSize(line2).x) + 24.0f;
        const ImVec2 p0(14.0f, 14.0f);
        const ImVec2 p1(p0.x + box_w, 66.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 24, 195), 6.0f);
        dl->AddRect(p0, p1, IM_COL32(90, 120, 155, 165), 6.0f);
        dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 8.0f), IM_COL32(238, 244, 252, 255), line1);
        dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 28.0f), IM_COL32(170, 190, 212, 235), line2);
    }

    if (!app.fpv_mode) {
        // 1b. Top-right Aiming Range stats badge (visible once shooting or inside Aiming Range)
        if (app.aim_stats.shots_fired > 0 || app.last_teleport == TeleportId::AimingRange) {
            char st1[96];
            char st2[96];
            std::snprintf(st1, sizeof(st1), "AIM RANGE  |  Acc: %5.1f%%", app.aim_stats.accuracy_pct());
            std::snprintf(st2, sizeof(st2), "Hits: %d / %d  |  Streak: %d (Best %d)",
                          app.aim_stats.target_hits,
                          app.aim_stats.shots_fired,
                          app.aim_stats.current_streak,
                          app.aim_stats.best_streak);
            const float sw = std::max(ImGui::CalcTextSize(st1).x, ImGui::CalcTextSize(st2).x) + 24.0f;
            const ImVec2 sp0(w - sw - 14.0f, 14.0f);
            const ImVec2 sp1(w - 14.0f, 66.0f);
            dl->AddRectFilled(sp0, sp1, IM_COL32(14, 18, 24, 195), 6.0f);
            dl->AddRect(sp0, sp1, IM_COL32(130, 165, 110, 175), 6.0f);
            dl->AddText(ImVec2(sp0.x + 12.0f, sp0.y + 8.0f), IM_COL32(230, 252, 212, 255), st1);
            dl->AddText(ImVec2(sp0.x + 12.0f, sp0.y + 28.0f), IM_COL32(182, 214, 192, 235), st2);
        }

        // 2. free-aim Debug Ellipse (optional)
        if (app.fa_debug_ellipse) {
            fa_debug_info_t dbg{};
            fa_get_debug_info(&app.fa_state, &dbg);

            // When paused in menu without controller bypass, derive zone live from fa_bank.cfg
            const bool live_stepping = !app.ui_open || app.controller_bypasses_menu;
            const float live_fov_scale = fa_fov_scale(app.current_world_fovy, app.base_world_fovy_deg);
            const float zx = std::max(0.1f, (live_stepping && dbg.active) ? dbg.zone_x : (app.fa_bank.cfg.zone_x * live_fov_scale));
            const float zy = std::max(0.1f, (live_stepping && dbg.active) ? dbg.zone_y : (app.fa_bank.cfg.zone_y * live_fov_scale));
            constexpr int kSegs = 64;
            ImVec2 pts[kSegs];
            for (int i = 0; i < kSegs; ++i) {
                const float a = (static_cast<float>(i) / static_cast<float>(kSegs)) * 2.0f * kPi;
                const float ang[2] = {std::cos(a) * zx, std::sin(a) * zy};
                float ndc[2] = {0, 0};
                fa_project_reticle_ndc(ang, app.current_world_fovy, aspect, ndc);
                pts[i] = ImVec2((ndc[0] * 0.5f + 0.5f) * w, (0.5f - ndc[1] * 0.5f) * h);
            }
            dl->AddPolyline(pts, kSegs, IM_COL32(80, 200, 255, 190), ImDrawFlags_Closed, 1.5f);

            auto proj_pt = [&](const float deg[2]) -> ImVec2 {
                float ndc[2] = {0, 0};
                fa_project_reticle_ndc(deg, app.current_world_fovy, aspect, ndc);
                return ImVec2((ndc[0] * 0.5f + 0.5f) * w, (0.5f - ndc[1] * 0.5f) * h);
            };
            const ImVec2 target_px  = proj_pt(dbg.target);
            const ImVec2 display_px = proj_pt(dbg.display);
            const float  tot[2]     = {dbg.target[0] + dbg.over[0], dbg.target[1] + dbg.over[1]};
            const ImVec2 over_px    = proj_pt(tot);

            dl->AddLine(target_px, over_px, IM_COL32(255, 155, 50, 220), 2.0f);
            dl->AddCircleFilled(target_px, 4.0f, IM_COL32(80, 220, 255, 255));
            dl->AddCircle(display_px, 6.0f, IM_COL32(90, 255, 140, 255), 16, 1.5f);

            char dtxt[256];
            std::snprintf(dtxt, sizeof(dtxt),
                          "FA Debug | zone=(%.1f, %.1f) deg | lead=%.2f catchup=%.2f (v=%.1f,%.1f) | src=%s%s",
                          zx, zy, dbg.lead_rate, dbg.catchup_rate,
                          dbg.catchup_vel[0], dbg.catchup_vel[1],
                          fa_input_source_name(dbg.last_input),
                          dbg.edge_turn_active ? " [EDGE-TURN]" : "");
            const float dw = ImGui::CalcTextSize(dtxt).x + 20.0f;
            dl->AddRectFilled(ImVec2(14.0f, 74.0f), ImVec2(14.0f + dw, 98.0f), IM_COL32(14, 18, 24, 180), 4.0f);
            dl->AddText(ImVec2(24.0f, 79.0f), IM_COL32(130, 230, 255, 255), dtxt);
        }

        // 3. free-aim Sub-Pixel Unlocked Reticle (projected with WORLD FOV, never viewmodel FOV)
        float ndc[2] = {0.0f, 0.0f};
        fa_project_reticle_ndc(app.fa_out.reticle_deg, app.current_world_fovy, aspect, ndc);
        float rx = (ndc[0] * 0.5f + 0.5f) * w;
        float ry = (0.5f - ndc[1] * 0.5f) * h;
        if (!app.fa_bank.cfg.reticle.smooth_reticle) {
            rx = std::floor(rx) + 0.5f;
            ry = std::floor(ry) + 0.5f;
        }

        if (app.fa_out.show_reticle) {
            draw_reticle_shape(dl, rx, ry, app.reticle);
        }

        // Hit marker X flash at reticle position
        if (app.hit_marker_timer > 0.0f && app.hit_marker_was_target) {
            const float t = std::clamp(app.hit_marker_timer / 0.22f, 0.0f, 1.0f);
            const ImU32 hc = IM_COL32(255, 90, 75, static_cast<int>(t * 255.0f));
            const float d0 = 5.0f, d1 = 13.0f;
            dl->AddLine(ImVec2(rx - d1, ry - d1), ImVec2(rx - d0, ry - d0), hc, 2.2f);
            dl->AddLine(ImVec2(rx + d0, ry - d0), ImVec2(rx + d1, ry - d1), hc, 2.2f);
            dl->AddLine(ImVec2(rx - d1, ry + d1), ImVec2(rx - d0, ry + d0), hc, 2.2f);
            dl->AddLine(ImVec2(rx + d0, ry + d0), ImVec2(rx + d1, ry + d1), hc, 2.2f);
        }

        // Last shot telemetry toast
        if (!app.last_hit_text.empty()) {
            const float tw = std::max(230.0f, ImGui::CalcTextSize(app.last_hit_text.c_str()).x + 24.0f);
            const ImVec2 tp(w * 0.5f - tw * 0.5f, h - 134.0f);
            dl->AddRectFilled(tp, ImVec2(tp.x + tw, tp.y + 24.0f), IM_COL32(16, 20, 26, 175), 4.0f);
            dl->AddText(ImVec2(tp.x + 12.0f, tp.y + 4.0f), IM_COL32(255, 224, 120, 255), app.last_hit_text.c_str());
        }

        // 4. Bottom-center Quake Speedometer
        {
            const float box_w = 340.0f;
            const float box_h = 84.0f;
            const ImVec2 p0(w * 0.5f - box_w * 0.5f, h - box_h - 18.0f);
            const ImVec2 p1(p0.x + box_w, p0.y + box_h);
            dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 24, 205), 8.0f);
            dl->AddRect(p0, p1, IM_COL32(90, 125, 165, 180), 8.0f);

            const float spd_mps = app.qm_state.horiz_speed * app.qm_speed_scale;
            const float spd_ups = spd_mps * kMpsToUps;
            const float pk_mps  = app.qm_state.peak_speed * app.qm_speed_scale;
            const float slope_deg = std::acos(std::clamp(app.player_char.slope_normal.z, -1.0f, 1.0f)) * kRadToDeg;

            char spd_line[128];
            std::snprintf(spd_line, sizeof(spd_line), "%5.2f m/s   (%4.0f UPS)   Peak: %.1f",
                          spd_mps, spd_ups, pk_mps);
            dl->AddText(ImVec2(p0.x + 14.0f, p0.y + 10.0f), IM_COL32(245, 250, 255, 255), spd_line);

            char state_line[128];
            std::snprintf(state_line, sizeof(state_line), "State: %-6s | Ground: %s | Slope: %4.1f deg",
                          qm_mode_name(app.qm_state.mode),
                          app.player_char.on_ground ? "YES" : "AIR",
                          slope_deg);
            dl->AddText(ImVec2(p0.x + 14.0f, p0.y + 30.0f), IM_COL32(175, 205, 232, 240), state_line);

            // Color-coded horizontal speed bar (0..22 m/s)
            const ImVec2 bar0(p0.x + 14.0f, p0.y + 52.0f);
            const ImVec2 bar1(p1.x - 14.0f, p0.y + 68.0f);
            dl->AddRectFilled(bar0, bar1, IM_COL32(34, 42, 52, 230), 3.0f);
            const float frac = std::clamp(spd_mps / 22.0f, 0.0f, 1.0f);
            const ImU32 bar_col = (spd_mps < 9.5f)  ? IM_COL32(72, 196, 255, 240)
                                : (spd_mps < 14.5f) ? IM_COL32(84, 235, 136, 240)
                                                    : IM_COL32(255, 152, 56, 245);
            dl->AddRectFilled(bar0, ImVec2(bar0.x + (bar1.x - bar0.x) * frac, bar1.y), bar_col, 3.0f);

            // Dash stamina pips when dash is enabled
            if (app.qm_params.dash_enabled && app.qm_params.dash_max_stamina > 0) {
                const int pips = std::min(app.qm_params.dash_max_stamina, 5);
                for (int i = 0; i < pips; ++i) {
                    const float fill = std::clamp(app.qm_state.stamina - static_cast<float>(i), 0.0f, 1.0f);
                    const ImVec2 sp0(p1.x - 14.0f - static_cast<float>(pips - i) * 20.0f, p0.y + 72.0f);
                    const ImVec2 sp1(sp0.x + 16.0f, sp0.y + 6.0f);
                    dl->AddRectFilled(sp0, sp1, IM_COL32(42, 48, 58, 220), 2.0f);
                    dl->AddRectFilled(sp0, ImVec2(sp0.x + 16.0f * fill, sp1.y), IM_COL32(190, 120, 255, 255), 2.0f);
                }
            }
        }
    }
}

void draw_sandbox_ui(SandboxApp& app, int win_w, int win_h) {
    draw_always_visible_hud(app, win_w, win_h);

    if (!app.ui_open) return;

    ImGui::SetNextWindowPos(ImVec2(16.0f, 106.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(492.0f, std::min(620.0f, static_cast<float>(win_h) - 122.0f)), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Portable Mods Testing Sandbox - Tuning Panel", &app.ui_open)) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(-185.0f);

    auto tab_flags = [&](int idx) -> ImGuiTabItemFlags {
        return (app.requested_ui_tab == idx) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    };

    if (ImGui::BeginTabBar("SandboxTabs")) {
        // ===================================================================
        // TAB 1: Dedicated Sandbox Options Tab
        // ===================================================================
        if (ImGui::BeginTabItem("Sandbox", nullptr, tab_flags(0))) {
            // --- Weapon ---
            ImGui::SeparatorText("Weapon");
            const char* fire_modes[] = {"Continuous (Full-Auto)", "Semi-Auto"};
            int fm_idx = app.weapon_fire.auto_fire ? 0 : 1;
            if (ImGui::Combo("Fire Mode", &fm_idx, fire_modes, 2)) {
                app.weapon_fire.auto_fire = (fm_idx == 0);
            }
            char rpm_fmt[48];
            std::snprintf(rpm_fmt, sizeof(rpm_fmt), "%%.0f RPM (%.1f/s)",
                          std::clamp(app.weapon_fire.fire_rpm, 60.0f, 1500.0f) / 60.0f);
            ImGui::SliderFloat("Fire Rate (RPM)", &app.weapon_fire.fire_rpm, 60.0f, 1500.0f, rpm_fmt);

            float gvol = app.weapon_audio.volume();
            if (ImGui::SliderFloat("Gun Audio Volume", &gvol, 0.0f, 1.5f, "%.2f")) {
                app.weapon_audio.set_volume(gvol);
            }
            bool gmuted = app.weapon_audio.muted();
            if (ImGui::Checkbox("Mute Gun Audio", &gmuted)) {
                app.weapon_audio.set_muted(gmuted);
            }
            ImGui::SameLine();
            bool hit_snd = app.weapon_audio.hit_confirm_enabled();
            if (ImGui::Checkbox("Hit-Confirm Sound", &hit_snd)) {
                app.weapon_audio.set_hit_confirm_enabled(hit_snd);
            }

            // --- Reticle Settings ---
            if (ImGui::CollapsingHeader("Reticle Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& rc = app.reticle;
                draw_reticle_preview_swatch(rc);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextDisabled("High-Contrast Presets:");
                auto color_preset_btn = [&](const char* id, const char* tip, float r, float g, float b) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r, g, b, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(1.0f, r + 0.12f), std::min(1.0f, g + 0.12f), std::min(1.0f, b + 0.12f), 1.0f));
                    if (ImGui::Button(id, ImVec2(24.0f, 22.0f))) {
                        rc.color[0] = r; rc.color[1] = g; rc.color[2] = b;
                    }
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                };
                color_preset_btn("##rc_green",  "Neon Green",    0.16f, 1.00f, 0.37f); ImGui::SameLine();
                color_preset_btn("##rc_cyan",   "Bright Cyan",   0.08f, 0.96f, 1.00f); ImGui::SameLine();
                color_preset_btn("##rc_yellow", "Bright Yellow", 1.00f, 0.94f, 0.12f); ImGui::SameLine();
                color_preset_btn("##rc_pink",   "Hot Pink",      1.00f, 0.18f, 0.72f); ImGui::SameLine();
                color_preset_btn("##rc_red",    "Bright Red",    1.00f, 0.20f, 0.18f); ImGui::SameLine();
                color_preset_btn("##rc_white",  "Crisp White",   1.00f, 1.00f, 1.00f);
                if (ImGui::SmallButton("Reset Reticle")) {
                    rc = ReticleConfig{};
                }
                ImGui::EndGroup();

                const char* styles[] = {"Crosshair + Dot", "Crosshair", "Dot", "Circle + Dot"};
                int st_idx = static_cast<int>(rc.style);
                if (ImGui::Combo("Reticle Style", &st_idx, styles, 4)) {
                    rc.style = static_cast<ReticleStyle>(st_idx);
                }
                ImGui::ColorEdit3("Reticle Color", rc.color);
                ImGui::SliderFloat("Reticle Opacity", &rc.opacity, 0.10f, 1.00f, "%.2f");
                ImGui::SliderFloat("Arm / Circle Size", &rc.arm_size, 2.0f, 32.0f, "%.1f px");
                ImGui::SliderFloat("Center Gap", &rc.gap, 0.0f, 20.0f, "%.1f px");
                ImGui::SliderFloat("Stroke Thickness", &rc.thickness, 1.0f, 6.0f, "%.1f px");
                ImGui::SliderFloat("Center Dot Size", &rc.dot_size, 0.0f, 8.0f, "%.1f px");
                ImGui::Checkbox("Dark Outline", &rc.outline);
                if (rc.outline) {
                    ImGui::SliderFloat("Outline Thickness", &rc.outline_thickness, 0.5f, 4.0f, "%.1f px");
                }
            }

            // --- Display ---
            ImGui::SeparatorText("Display");
            ImGui::Checkbox("Fullscreen (F11 / Alt+Enter)", &app.fullscreen);
            ImGui::SliderFloat("World Vertical FOV", &app.base_world_fovy_deg, 50.0f, 110.0f, "%.1f deg");
            ImGui::SliderFloat("Viewmodel FOV", &app.viewmodel_fovy_deg, 35.0f, 90.0f, "%.1f deg");
            ImGui::SliderFloat("Mouse Sens (deg/px)", &app.mouse_sens_deg_per_px, 0.02f, 0.40f, "%.3f");
            ImGui::Checkbox("Always-Visible HUD (F1)", &app.show_hud);
            ImGui::SameLine();
            ImGui::Checkbox("ADS Toggle Mode (RMB)", &app.ads_toggle_mode);

            // --- Controls ---
            ImGui::SeparatorText("Controls");
            ImGui::Checkbox("Controller Bypasses Menu", &app.controller_bypasses_menu);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("While Tuning Panel is open, gamepad sticks, triggers, buttons & gyro\n"
                                  "continue controlling FPS/FPV live while mouse & keyboard drive UI.");
            }
            ImGui::Text("Gamepad: %s", app.input.has_gamepad() ? app.input.gamepad_name().c_str() : "None connected");
            ImGui::Text("Sensors: Gyro=%s | Accel=%s",
                        app.input.has_gyro() ? "ON" : "N/A",
                        app.input.has_accel() ? "ON" : "N/A");
            ImGui::TextDisabled("Tip: Press controller Start button anytime to toggle Tuning Panel");

            // --- Gym & Reset ---
            ImGui::SeparatorText("Gym & Reset");
            if (ImGui::Button("Hub Spawn", ImVec2(106, 0)))      app.teleport(TeleportId::HubSpawn);
            ImGui::SameLine();
            if (ImGui::Button("Movement Wing", ImVec2(114, 0)))  app.teleport(TeleportId::MovementWing);
            ImGui::SameLine();
            if (ImGui::Button("Aiming Range", ImVec2(108, 0)))   app.teleport(TeleportId::AimingRange);
            ImGui::SameLine();
            if (ImGui::Button("FPV Yard", ImVec2(92, 0)))        app.teleport(TeleportId::FpvYard);

            if (ImGui::Button("Reset Player (R)"))          app.reset_player();
            ImGui::SameLine();
            if (ImGui::Button("Reset Drone Position (R in FPV)")) app.fpv.reset_drone();
            if (ImGui::Button(app.fpv_mode ? "Switch to FPS Mode (F)" : "Switch to FPV Drone (F)")) {
                app.toggle_fpv_mode();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Aiming Stats")) app.reset_aim_stats();

            ImGui::Text("Aim Stats: %d/%d hits (%.1f%%) | Streak: %d (Best %d)",
                        app.aim_stats.target_hits,
                        app.aim_stats.shots_fired,
                        app.aim_stats.accuracy_pct(),
                        app.aim_stats.current_streak,
                        app.aim_stats.best_streak);
            ImGui::TextDisabled("Player Pos: (%.2f, %.2f, %.2f) m",
                                app.player_char.feet_pos.x, app.player_char.feet_pos.y, app.player_char.feet_pos.z);

            ImGui::SeparatorText("Local Settings (sandbox_settings.ini)");
            if (ImGui::Button("Save Settings"))      app.save_settings();
            ImGui::SameLine();
            if (ImGui::Button("Load Settings"))      app.load_settings(true);
            ImGui::SameLine();
            if (ImGui::Button("Reset All Defaults")) app.reset_all_defaults();
            if (!app.settings_status.empty()) {
                ImGui::TextDisabled("%s", app.settings_status.c_str());
            }

            ImGui::EndTabItem();
        }

        // ===================================================================
        // TAB 2: quake-movement
        // ===================================================================
        if (ImGui::BeginTabItem("Quake Move", nullptr, tab_flags(1))) {
            int p_idx = static_cast<int>(app.qm_preset);
            if (ImGui::BeginCombo("Preset", qm_preset_name(app.qm_preset))) {
                for (int i = 0; i < QM_PRESET_COUNT; ++i) {
                    const auto id = static_cast<qm_preset_id_t>(i);
                    const bool sel = (p_idx == i);
                    if (ImGui::Selectable(qm_preset_name(id), sel)) {
                        app.set_qm_preset(id);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Physics tick rate selector (60 / 120 / 240 Hz)
            const char* ticks[] = {"60 Hz", "120 Hz", "240 Hz"};
            int tick_idx = (app.physics_tick_hz == 60) ? 0 : (app.physics_tick_hz == 240) ? 2 : 1;
            if (ImGui::Combo("Physics Tick Rate", &tick_idx, ticks, 3)) {
                app.physics_tick_hz = (tick_idx == 0) ? 60 : (tick_idx == 2) ? 240 : 120;
            }
            ImGui::SliderFloat("Adapter Speed Scale", &app.qm_speed_scale, 0.5f, 2.0f, "%.2fx");
            if (ImGui::Button("Reset Peak Speed")) app.qm_state.peak_speed = 0.0f;

            ImGui::Separator();
            ImGui::SliderFloat("Max Speed (m/s)",     &app.qm_params.max_speed, 3.0f, 18.0f);
            ImGui::SliderFloat("Ground Accel",        &app.qm_params.ground_accel, 1.0f, 30.0f);
            ImGui::SliderFloat("Air Accel",           &app.qm_params.air_accel, 0.5f, 150.0f);
            ImGui::SliderFloat("Air Wish Cap (m/s)",  &app.qm_params.air_wish_cap, 0.5f, 15.0f);
            ImGui::SliderFloat("CPMA Air Control",    &app.qm_params.air_control, 0.0f, 15.0f);
            ImGui::SliderFloat("Friction",            &app.qm_params.friction, 1.0f, 15.0f);
            ImGui::SliderFloat("Gravity (m/s^2)",     &app.qm_params.gravity, 5.0f, 40.0f);
            ImGui::SliderFloat("Jump Speed (m/s)",    &app.qm_params.jump_speed, 3.0f, 14.0f);
            bool abhop = app.qm_params.auto_bhop != 0;
            if (ImGui::Checkbox("Auto-Bhop", &abhop)) app.qm_params.auto_bhop = abhop ? 1 : 0;

            if (ImGui::CollapsingHeader("Crouch-Slide & Slope Surf")) {
                bool se = app.qm_params.slide_enabled != 0;
                if (ImGui::Checkbox("Slide Enabled", &se)) app.qm_params.slide_enabled = se ? 1 : 0;
                ImGui::SliderFloat("Slide Boost (m/s)",     &app.qm_params.slide_boost, 0.0f, 10.0f);
                ImGui::SliderFloat("Slide Friction",        &app.qm_params.slide_friction, 0.1f, 5.0f);
                ImGui::SliderFloat("Slope Accel (m/s^2)",   &app.qm_params.slide_slope_accel, 0.0f, 40.0f);
                ImGui::SliderFloat("Slide Jump Boost",      &app.qm_params.slide_jump_boost, 0.0f, 6.0f);
            }
            if (ImGui::CollapsingHeader("Dash & Wall-Bounce")) {
                bool de = app.qm_params.dash_enabled != 0;
                if (ImGui::Checkbox("Dash Enabled", &de)) app.qm_params.dash_enabled = de ? 1 : 0;
                ImGui::SliderFloat("Dash Impulse (m/s)", &app.qm_params.dash_impulse, 5.0f, 25.0f);
                ImGui::SliderFloat("Dash-Jump Boost",    &app.qm_params.dash_jump_boost, 0.0f, 10.0f);
                bool wb = app.qm_params.wall_bounce_enabled != 0;
                if (ImGui::Checkbox("Wall-Bounce Enabled", &wb)) app.qm_params.wall_bounce_enabled = wb ? 1 : 0;
                ImGui::SliderFloat("Wall-Bounce Push",   &app.qm_params.wall_bounce_push, 2.0f, 14.0f);
                ImGui::SliderFloat("Wall-Bounce Up",     &app.qm_params.wall_bounce_up, 2.0f, 12.0f);
            }
            ImGui::EndTabItem();
        }

        // ===================================================================
        // TAB 3: weighty-aim
        // ===================================================================
        if (ImGui::BeginTabItem("Weighty Aim", nullptr, tab_flags(2))) {
            auto& sc = app.wa_player.stick_cfg;
            auto& gc = app.wa_player.gyro_cfg;

            ImGui::Checkbox("Force Original Aim (1:1 Bypass)", &app.wa_force_original_aim);

            if (ImGui::BeginCombo("Look Curve", wa_curve_name(sc.curve))) {
                for (int i = 0; i < WA_CURVE_COUNT; ++i) {
                    const auto c = static_cast<wa_curve_t>(i);
                    if (ImGui::Selectable(wa_curve_name(c), sc.curve == c)) {
                        wa_select_curve(&sc, c);
                    }
                }
                ImGui::EndCombo();
            }

            // Live Bezier response curve graph + Live 2D circular stick & deadzone visualizer
            draw_curve_graph_widget(sc, app.last_look_stick_mag, ImVec2(280.0f, 174.0f));
            ImGui::SameLine();
            draw_stick_visualizer_widget(sc, app.last_raw_stick, app.last_wa_stick_out, 174.0f);

            ImGui::SliderFloat("Sensitivity",          &sc.sensitivity, 0.25f, 2.50f, "%.2fx");
            ImGui::SliderFloat("Vertical Sensitivity", &sc.vertical_sens, 0.25f, 2.00f, "%.2fx");
            ImGui::SliderFloat("Inner Deadzone",       &sc.inner_deadzone, 0.0f, 0.40f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Inner radial deadzone threshold (sandbox default: 0.03).");
            }
            ImGui::SliderFloat("Outer Deadzone",       &sc.outer_deadzone, 0.10f, 1.00f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Outer saturation radius where output reaches 100%% (sandbox default: 0.98 = 2%% outer margin).");
            }
            if (sc.outer_deadzone < sc.inner_deadzone + 0.05f) {
                sc.outer_deadzone = std::min(1.0f, sc.inner_deadzone + 0.05f);
            }
            ImGui::SliderFloat("Full Stick Speed (deg/s)", &app.full_stick_deg_per_sec, 60.0f, 450.0f, "%.0f");

            bool bez_changed = false;
            bez_changed |= ImGui::SliderFloat("Bezier X1", &sc.bezier[0], 0.0f, 1.0f, "%.2f");
            bez_changed |= ImGui::SliderFloat("Bezier Y1", &sc.bezier[1], 0.0f, 1.0f, "%.2f");
            bez_changed |= ImGui::SliderFloat("Bezier X2", &sc.bezier[2], 0.0f, 1.0f, "%.2f");
            bez_changed |= ImGui::SliderFloat("Bezier Y2", &sc.bezier[3], 0.0f, 1.0f, "%.2f");
            if (bez_changed) wa_curve_points_changed(&sc);

            if (ImGui::Button("Reset Defaults (0.03 / 0.98)")) {
                reset_sandbox_wa_stick_response(sc);
            }

            if (ImGui::CollapsingHeader("Look Acceleration (Turn Boost)")) {
                if (ImGui::BeginCombo("Boost Mode", wa_boost_mode_name(sc.boost_mode))) {
                    for (int i = 0; i < WA_BOOST_COUNT; ++i) {
                        const auto m = static_cast<wa_boost_mode_t>(i);
                        if (ImGui::Selectable(wa_boost_mode_name(m), sc.boost_mode == m)) sc.boost_mode = m;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("Boost Amount",    &sc.boost_amount, 1.0f, 4.0f, "%.2fx");
                ImGui::SliderFloat("Boost Threshold", &sc.boost_threshold, 0.3f, 1.0f, "%.2f");
                ImGui::SliderFloat("Boost Delay (s)", &sc.boost_delay, 0.0f, 0.5f, "%.2f");
                ImGui::SliderFloat("Boost Ramp (s)",  &sc.boost_time, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Boost Vertical",  &sc.boost_vertical, 0.0f, 1.0f, "%.2f");
                if (ImGui::Button("Reset Turn Boost")) wa_reset_boost(&sc);
            }

            if (ImGui::CollapsingHeader("Gyro Aim (GyroWiki)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("Status: %s", wa_gyro_status_text(&app.wa_player.gyro_state));
                const auto& sen = app.input.last_sensor();
                ImGui::Text("Gyro (deg/s):  P=%+6.1f  Y=%+6.1f  R=%+6.1f",
                            sen.gyro_deg_s[0], sen.gyro_deg_s[1], sen.gyro_deg_s[2]);
                ImGui::Text("Accel (m/s^2): X=%+5.2f  Y=%+5.2f  Z=%+5.2f",
                            sen.accel[0], sen.accel[1], sen.accel[2]);

                if (ImGui::BeginCombo("Gyro Mode", wa_gyro_mode_name(gc.mode))) {
                    for (int i = 0; i < WA_GYRO_COUNT; ++i) {
                        const auto gm = static_cast<wa_gyro_mode_t>(i);
                        if (ImGui::Selectable(wa_gyro_mode_name(gm), gc.mode == gm)) gc.mode = gm;
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Gyro Space", wa_gyro_space_name(gc.space))) {
                    for (int i = 0; i < WA_GYRO_SPACE_COUNT; ++i) {
                        const auto gs = static_cast<wa_gyro_space_t>(i);
                        if (ImGui::Selectable(wa_gyro_space_name(gs), gc.space == gs)) gc.space = gs;
                    }
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("Gyro Sensitivity", &gc.sensitivity, 0.1f, 20.0f, "%.2f");
                ImGui::SliderFloat("Gyro Vertical",    &gc.vertical, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Tightening",       &gc.tightening, 0.0f, 30.0f, "%.1f deg/s");
                ImGui::SliderFloat("Smoothing",        &gc.smoothing, 0.0f, 30.0f, "%.1f deg/s");
                ImGui::SliderFloat("Acceleration",     &gc.acceleration, 1.0f, 4.0f, "%.2fx");
                bool inv = gc.invert_y != 0;
                if (ImGui::Checkbox("Invert Gyro Y", &inv)) gc.invert_y = inv ? 1 : 0;
                if (ImGui::Button("Calibrate Gyro (Hold Still)")) {
                    wa_gyro_start_calibration(&app.wa_player.gyro_state);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Gyro Tuning")) wa_reset_gyro(&gc);
            }
            ImGui::EndTabItem();
        }

        // ===================================================================
        // TAB 4: free-aim
        // ===================================================================
        if (ImGui::BeginTabItem("Free-Aim", nullptr, tab_flags(3))) {
            auto& fc = app.fa_bank.cfg;
            ImGui::Checkbox("Force Original Aim (Free-Aim Off)", &app.fa_force_original_aim);

            if (ImGui::BeginCombo("Preset", fa_preset_name(fc.preset))) {
                for (int i = 0; i < FA_PRESET_COUNT; ++i) {
                    const auto pr = static_cast<fa_preset_t>(i);
                    if (ImGui::Selectable(fa_preset_name(pr), fc.preset == pr)) {
                        fa_profile_apply_preset(&app.fa_bank, pr);
                    }
                }
                ImGui::EndCombo();
            }

            bool chg = false;
            chg |= ImGui::SliderFloat("Zone Width (deg)",   &fc.zone_x, 0.0f, 35.0f, "%.1f");
            chg |= ImGui::SliderFloat("Zone Height (deg)",  &fc.zone_y, 0.0f, 25.0f, "%.1f");
            chg |= ImGui::SliderFloat("Camera Share",       &fc.camera_share, 0.0f, 1.0f, "%.2f");
            chg |= ImGui::SliderFloat("Camera Lead",        &fc.camera_lead, 0.0f, 6.0f, "%.2f");
            chg |= ImGui::SliderFloat("Catch-Up Speed",     &fc.recenter_speed, 0.0f, 6.0f, "%.2f");
            chg |= ImGui::SliderFloat("Catch-Up Delay (s)", &fc.recenter_delay, 0.0f, 2.0f, "%.2f");
            chg |= ImGui::SliderFloat("Catch-Up Smooth (s)",&fc.recenter_smooth, 0.02f, 1.5f, "%.2f");
            chg |= ImGui::SliderFloat("Gun Response (Hz)",  &fc.gun_response, 1.0f, 30.0f, "%.1f");
            chg |= ImGui::SliderFloat("Gun Damping",        &fc.gun_damping, 0.1f, 2.0f, "%.2f");
            chg |= ImGui::SliderFloat("Turn Drag",          &fc.turn_drag, 0.0f, 1.0f, "%.2f");
            chg |= ImGui::SliderFloat("Reticle Speed",      &fc.reticle_speed, 0.25f, 3.0f, "%.2f");
            chg |= ImGui::SliderFloat("Edge Smoothing (s)", &fc.edge_smoothing, 0.0f, 0.5f, "%.2f");

            if (ImGui::CollapsingHeader("ADS, Sway & Edge Auto-Turn")) {
                chg |= ImGui::SliderFloat("ADS Zoom",         &fc.ads_zoom, 1.0f, 3.0f, "%.2fx");
                chg |= ImGui::SliderFloat("ADS Time (s)",     &fc.ads_time, 0.05f, 0.6f, "%.2f");
                chg |= ImGui::SliderFloat("ADS Zone Scale",   &fc.ads_zone, 0.0f, 2.0f, "%.2f");
                chg |= ImGui::SliderFloat("ADS Sensitivity",  &fc.ads_sens, 0.1f, 1.0f, "%.2f");
                chg |= ImGui::SliderFloat("Camera Sway",      &fc.camera_sway, 0.0f, 2.0f, "%.2f");
                chg |= ImGui::SliderFloat("Walk Sway",        &fc.walk_sway, 0.0f, 2.0f, "%.2f");
                chg |= ImGui::SliderFloat("Edge Auto-Turn",   &fc.edge_turn_speed, 0.0f, 240.0f, "%.0f deg/s");
            }
            if (ImGui::CollapsingHeader("Reticle & Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool sh_hip = fc.reticle.show_hip != 0;
                bool sh_ads = fc.reticle.show_ads != 0;
                bool smooth = fc.reticle.smooth_reticle != 0;
                if (ImGui::Checkbox("Show Reticle (Hip)", &sh_hip)) { fc.reticle.show_hip = sh_hip ? 1 : 0; chg = true; }
                if (ImGui::Checkbox("Show Reticle (ADS)", &sh_ads)) { fc.reticle.show_ads = sh_ads ? 1 : 0; chg = true; }
                if (ImGui::Checkbox("Smooth Sub-Pixel Reticle", &smooth)) { fc.reticle.smooth_reticle = smooth ? 1 : 0; chg = true; }
                chg |= ImGui::SliderFloat("Reticle Size",    &fc.reticle.size, 0.5f, 5.0f, "%.1f");
                chg |= ImGui::SliderFloat("Reticle Opacity", &fc.reticle.opacity, 0.05f, 1.0f, "%.2f");

                ImGui::Checkbox("Draw Debug Ellipse (fa_get_debug_info)", &app.fa_debug_ellipse);
                ImGui::Checkbox("Scripted Look Pattern (4.8s cycle)", &app.fa_scripted_look);
                bool log_csv = app.fa_csv_logging;
                if (ImGui::Checkbox("Record CSV Telemetry (telemetry_free_aim.csv)", &log_csv)) {
                    app.toggle_csv_logging(log_csv);
                }
            }
            if (chg) fa_profile_settings_changed(&app.fa_bank);
            ImGui::EndTabItem();
        }

        // ===================================================================
        // TAB 5: fpv-drone (libfpv)
        // ===================================================================
        if (ImGui::BeginTabItem("FPV Drone", nullptr, tab_flags(4))) {
            if (!app.fpv.is_ready()) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "libfpv failed to initialize.");
            } else {
                if (ImGui::Button(app.fpv_mode ? "Return to FPS Mode (F)" : "Enter FPV Drone Mode (F)")) {
                    app.toggle_fpv_mode();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Drone Position (R)")) app.fpv.reset_drone();

                ImGui::SeparatorText("Launch Pads");
                if (ImGui::Button("FPV Yard Pad", ImVec2(105, 0)))   app.set_fpv_launch_pad(0);
                ImGui::SameLine();
                if (ImGui::Button("Hub Plaza", ImVec2(95, 0)))       app.set_fpv_launch_pad(1);
                ImGui::SameLine();
                if (ImGui::Button("Movement Wing", ImVec2(114, 0)))  app.set_fpv_launch_pad(2);
                ImGui::SameLine();
                if (ImGui::Button("Aiming Range", ImVec2(104, 0)))   app.set_fpv_launch_pad(3);
                if (ImGui::Button("Set Drone Spawn Here (In Front of Player)")) {
                    app.set_drone_spawn_at_player();
                }
                const Vec3 sp = app.fpv.spawn_pos();
                ImGui::SameLine();
                ImGui::TextDisabled("(%.1f, %.1f, %.2f)", sp.x, sp.y, sp.z);

                ImGui::Separator();
                const char* views[] = {"FPV Goggles", "Chase Cam", "Line of Sight"};
                int v_idx = static_cast<int>(app.fpv_cam_view);
                if (ImGui::Combo("Camera View (V)", &v_idx, views, 3)) {
                    app.fpv_cam_view = static_cast<FpvCamView>(v_idx);
                    app.fpv.set_active_mode(app.fpv_mode, app.fpv_cam_view);
                }

                // Quad preset picker
                const auto& presets = app.fpv.preset_names();
                if (!presets.empty()) {
                    const int cur = app.fpv.current_preset_index();
                    if (ImGui::BeginCombo("Quad Preset", presets[cur].c_str())) {
                        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
                            if (ImGui::Selectable(presets[i].c_str(), i == cur)) {
                                app.fpv.select_preset(i);
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::Separator();
                const bool armed_now = (app.fpv.frame().flags & FPV_DRONE_ARMED) != 0;
                if (ImGui::Button(armed_now ? "Disarm Drone (Enter / South)" : "Arm Drone (Enter / South)")) {
                    app.fpv.trigger_arm_toggle(app.input.has_gamepad());
                }
                ImGui::SameLine();
                ImGui::Checkbox("Angle Mode (Gamepad)", &app.fpv.ui_angle_mode);
                ImGui::Checkbox("Gamepad Mode-2 RC Input", &app.fpv.use_gamepad_rc);
                ImGui::Checkbox("Non-Centering Throttle Gimbal", &app.fpv.gp_rc.non_centering_throttle);
                ImGui::TextDisabled("KB Flight: Enter=Arm, W/S=Hover, A/D=Yaw, Arrows=Pitch/Roll, R=Reset, T=Turtle, B=Beeper");

                ImGui::SliderFloat("Extra Camera Tilt", &app.fpv.camera_tilt_offset_deg, -25.0f, 35.0f, "%+.0f deg");

                float rscale = app.fpv.rates_scale;
                if (ImGui::SliderFloat("RC Rates Scale", &rscale, 0.4f, 1.8f, "%.2fx")) {
                    app.fpv.rates_scale = rscale;
                }
                if (ImGui::Button("Apply Rates (Session)")) app.fpv.apply_rates_scale(app.fpv.rates_scale, false);
                ImGui::SameLine();
                if (ImGui::Button("Save to EEPROM")) app.fpv.apply_rates_scale(app.fpv.rates_scale, true);
                if (!app.fpv.last_cli_status.empty()) {
                    ImGui::TextDisabled("%s", app.fpv.last_cli_status.c_str());
                }

                ImGui::Separator();
                ImGui::Checkbox("Show Betaflight OSD", &app.fpv.show_osd);
                ImGui::TextDisabled("OSD style, lens & video feed settings: press F8 > OSD / Camera");

                float vol = app.fpv.audio_volume;
                if (ImGui::SliderFloat("Motor Audio Volume", &vol, 0.0f, 1.5f, "%.2f")) app.fpv.set_audio_volume(vol);
                float bvol = app.fpv.beep_volume;
                if (ImGui::SliderFloat("Beeper Volume", &bvol, 0.0f, 1.5f, "%.2f")) app.fpv.set_beep_volume(bvol);
                bool muted = app.fpv.audio_muted;
                if (ImGui::Checkbox("Mute Drone Audio", &muted)) app.fpv.set_muted(muted);

                if (ImGui::Button("Open libfpv Settings Overlay (F8)")) {
                    const bool open_now = !app.fpv.overlay_is_open();
                    app.fpv.set_overlay_open(open_now);
                    if (open_now) app.ui_open = false;
                }

                ImGui::Separator();
                const fpv_frame& fr = app.fpv.frame();
                ImGui::Text("Status: %s | Ground: %s",
                            (fr.flags & FPV_DRONE_ARMED) ? "ARMED" : "DISARMED",
                            (fr.flags & FPV_DRONE_ON_GROUND) ? "YES" : "AIR");
                if (fr.arming_disable_text[0] != '\0') {
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Arming check: %s", fr.arming_disable_text);
                }
                ImGui::Text("Speed: %.1f km/h | Alt: %.2f m | Batt: %.2f V",
                            fr.speed_kmh, fr.altitude, fr.battery.voltage);
                ImGui::Text("Motors RPM: M1=%.0f  M2=%.0f  M3=%.0f  M4=%.0f",
                            fr.motors[0].rpm, fr.motors[1].rpm, fr.motors[2].rpm, fr.motors[3].rpm);
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    app.requested_ui_tab = -1;
    ImGui::PopItemWidth();
    ImGui::End();
}

} // namespace sb
