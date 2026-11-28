#include "control_helpers.h"
#include "gym_map.h"
#include "physics_world.h"
#include "sandbox_settings.h"
#include "weapon_audio.h"
#include "qm_physics.h"
#include "weighty_aim.h"
#include "free_aim.h"
#include "libfpv.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) {                                                                \
            g_pass++;                                                              \
            std::printf("[PASS] %s\n", msg);                                       \
        } else {                                                                   \
            g_fail++;                                                              \
            std::fprintf(stderr, "[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);  \
        }                                                                          \
    } while (0)

static void test_gym_and_jolt_physics(const sb::GymMap& gym, sb::PhysicsWorld& physics) {
    CHECK(gym.static_mesh.vertices.size() > 200, "Gym mesh has substantial geometry");
    CHECK(gym.static_mesh.indices.size() >= 300 && (gym.static_mesh.indices.size() % 3 == 0),
          "Gym mesh has valid triangle index count");
    CHECK(gym.targets.size() >= 10, "Aiming range contains static and moving targets");

    bool all_finite = true;
    for (const auto& v : gym.static_mesh.vertices) {
        if (!std::isfinite(v.pos[0]) || !std::isfinite(v.pos[1]) || !std::isfinite(v.pos[2]) ||
            !std::isfinite(v.normal[0]) || !std::isfinite(v.normal[1]) || !std::isfinite(v.normal[2])) {
            all_finite = false;
            break;
        }
    }
    CHECK(all_finite, "All gym vertices and normals are finite");

    // 1. Raycast flat floor at hub spawn
    const sb::RaycastHit floor_hit = physics.raycast_world({0.0f, -6.0f, 3.0f}, {0.0f, 0.0f, -1.0f}, 10.0f);
    CHECK(floor_hit.hit && std::fabs(floor_hit.point.z - 0.04f) < 0.1f && floor_hit.normal.z > 0.99f,
          "Jolt downward raycast hits flat floor with +Z normal");

    // 2. Raycast 30-deg surf ramp (ramp30_sample_xy)
    const sb::RaycastHit ramp_hit = physics.raycast_world(
        {gym.ramp30_sample_xy.x, gym.ramp30_sample_xy.y, 12.0f}, {0.0f, 0.0f, -1.0f}, 20.0f);
    const float ramp_angle_deg = std::acos(std::clamp(ramp_hit.normal.z, -1.0f, 1.0f)) * sb::kRadToDeg;
    CHECK(ramp_hit.hit && std::fabs(ramp_angle_deg - 30.0f) < 1.5f && ramp_hit.normal.y < -0.45f,
          "Jolt raycast on 30-deg surf ramp reports ~30 deg downhill slope normal");

    // 3. Raycast wall-bounce corridor wall
    const sb::RaycastHit wall_hit = physics.raycast_world(gym.wall_lane_center, {1.0f, 0.0f, 0.0f}, 6.0f);
    CHECK(wall_hit.hit && wall_hit.normal.x < -0.95f && std::fabs(wall_hit.normal.z) < 0.05f,
          "Jolt horizontal raycast hits wall-bounce corridor wall with inward -X normal");

    // 4. Hitscan raycast hits Aiming Range near center target at (0, 24, 1.5)
    const sb::RaycastHit target_hit = physics.raycast_hitscan(
        {0.0f, 14.0f, 1.5f}, {0.0f, 1.0f, 0.0f}, 50.0f, gym.targets);
    CHECK(target_hit.hit && target_hit.target_id == 2 && std::fabs(target_hit.distance - 9.88f) < 0.3f,
          "Hitscan raycast hits Near Center aiming target with accurate distance and target ID");

    // 5. Capsule sweep on flat floor + crouch height transition
    physics.teleport_player({-18.0f, -20.0f, 0.25f});
    sb::CharacterMoveResult ch{};
    for (int i = 0; i < 30; ++i) {
        ch = physics.step_character({0.0f, 6.0f, -2.0f}, 1.0f / 120.0f, false, false);
    }
    CHECK(ch.on_ground && ch.feet_pos.y > -19.0f && ch.slope_normal.z > 0.99f,
          "Jolt CharacterVirtual walks cleanly on flat runway");

    // Crouch-slide into low tunnel at (-41, -14, clearance=1.35m)
    physics.teleport_player({-41.0f, -21.0f, 0.05f});
    for (int i = 0; i < 80; ++i) {
        ch = physics.step_character({0.0f, 8.0f, -1.0f}, 1.0f / 120.0f, true, false);
    }
    CHECK(ch.crouching && ch.feet_pos.y > -16.5f,
          "Jolt CharacterVirtual crouches smoothly through 1.35m crouch-slide tunnel");

    // While inside the tunnel, requesting uncrouch must stay crouched due to overhead clearance
    ch = physics.step_character({0.0f, 0.0f, -1.0f}, 1.0f / 120.0f, false, false);
    CHECK(ch.crouching, "Jolt CharacterVirtual blocks uncrouch under low tunnel ceiling");

    // Regression (P0-3): Jumping into low tunnel ceiling must clip upward velocity (no ceiling glue)
    sb::CharacterMoveResult ceil_res{};
    for (int i = 0; i < 20; ++i) {
        ceil_res = physics.step_character({0.0f, 0.0f, 7.0f}, 1.0f / 120.0f, true, true);
    }
    CHECK(ceil_res.velocity.z < 0.25f,
          "Jolt ceiling contact clips upward velocity (no ceiling glue)");

    // Regression (P0-3): Running head-first into corridor wall must clip into-wall velocity (no phantom speed)
    physics.teleport_player({-47.55f, 9.0f, 0.05f});
    sb::CharacterMoveResult wall_res{};
    for (int i = 0; i < 20; ++i) {
        wall_res = physics.step_character({9.5f, 0.0f, 0.0f}, 1.0f / 120.0f, false, false);
    }
    CHECK(std::fabs(wall_res.velocity.x) < 0.25f,
          "Jolt wall contact clips into-wall velocity (no phantom speedometer velocity)");

    // Regression (P2-1): Movement Wing access ramp top connects flush to connecting walkway (Z ~= 4.0m)
    const sb::RaycastHit access_top = physics.raycast_world({-17.0f, 23.4f, 10.0f}, {0.0f, 0.0f, -1.0f}, 15.0f);
    const sb::RaycastHit walkway_hit = physics.raycast_world({-17.0f, 24.5f, 10.0f}, {0.0f, 0.0f, -1.0f}, 15.0f);
    CHECK(access_top.hit && walkway_hit.hit && std::fabs(access_top.point.z - walkway_hit.point.z) < 0.25f,
          "Movement Wing access ramp connects flush to top connecting walkway");
}

static void test_input_latch_and_fpv_gamepad_mapper() {
    // Regression (P1-1): Latched edges survive 0-substep frames when render FPS > physics tick Hz
    sb::InputEdgeLatch latch{};
    const float fixed_dt = 1.0f / 60.0f;
    const float frame_dt = 1.0f / 240.0f;
    float accum = 0.0f;
    int substeps_executed = 0;
    bool delivered_jump = false, delivered_crouch = false, delivered_dash = false;

    for (int frame = 0; frame < 5; ++frame) {
        if (frame == 0) {
            latch.accumulate(true, true, true); // Press on frame 0 (accum = 1/240 < 1/60 -> 0 substeps!)
        } else {
            latch.accumulate(false, false, false);
        }
        accum += frame_dt;
        while (accum >= fixed_dt) {
            substeps_executed++;
            if (latch.jump_pressed)   delivered_jump   = true;
            if (latch.crouch_pressed) delivered_crouch = true;
            if (latch.dash_pressed)   delivered_dash   = true;
            latch.clear();
            accum -= fixed_dt;
        }
        if (frame < 3) {
            CHECK(substeps_executed == 0 && latch.jump_pressed && latch.crouch_pressed && latch.dash_pressed,
                  "InputEdgeLatch retains jump/crouch/dash across 0-substep render frames");
        }
    }
    CHECK(substeps_executed == 1 && delivered_jump && delivered_crouch && delivered_dash && !latch.jump_pressed,
          "InputEdgeLatch delivers all latched edges on next physics substep then clears cleanly");

    // Regression (P0-2): Mode-2 self-centering gamepad throttle & latched arm/turtle switches
    sb::FpvGamepadRcMapper rc{};
    float ch[10] = {};
    sb::FpvGamepadInput neutral{};
    rc.map_channels(neutral, true, true, ch);
    CHECK(ch[2] <= -0.99f && ch[4] < 0.0f && ch[7] < 0.0f,
          "Gamepad Mode-2 self-centering center stick maps to -1.0 idle throttle and disarmed");

    // Press South -> arms; release South & center sticks -> stays armed (no mid-air disarm!)
    sb::FpvGamepadInput press_arm{};
    press_arm.btn_arm = true;
    press_arm.left_y  = 0.0f;
    rc.map_channels(press_arm, true, true, ch);
    CHECK(ch[4] > 0.5f && ch[2] <= -0.99f,
          "Gamepad South press toggles armed ON while centered stick keeps idle throttle");

    rc.map_channels(neutral, true, true, ch);
    CHECK(ch[4] > 0.5f && ch[2] <= -0.99f,
          "Releasing South and centering sticks keeps drone armed (latched toggle)");

    // Half-up left stick gives mid throttle (~0.0), full-up gives +1.0, down stays idle -1.0
    sb::FpvGamepadInput half_up{};
    half_up.left_y = 0.54f;
    rc.map_channels(half_up, true, true, ch);
    CHECK(ch[2] > -0.15f && ch[2] < 0.20f,
          "Self-centering gamepad half-up stick maps near mid throttle");

    sb::FpvGamepadInput full_up{};
    full_up.left_y = 1.0f;
    rc.map_channels(full_up, true, true, ch);
    CHECK(ch[2] > 0.99f, "Self-centering gamepad full-up stick maps to +1.0 full throttle");

    // Second South press disarms
    rc.map_channels(press_arm, true, true, ch);
    CHECK(ch[4] < -0.5f, "Second South press disarms drone");

    // Turtle latch (North)
    sb::FpvGamepadInput press_turtle{};
    press_turtle.btn_turtle = true;
    rc.map_channels(press_turtle, true, true, ch);
    rc.map_channels(neutral, true, true, ch);
    CHECK(ch[7] > 0.5f, "Gamepad North press latches turtle mode ON across button release");

    // Non-centering throttle gimbal option passes [-1, +1] directly
    rc.non_centering_throttle = true;
    rc.map_channels(neutral, true, true, ch);
    CHECK(std::fabs(ch[2]) < 0.01f, "Non-centering throttle gimbal maps 0.0 stick to 0.0 mid throttle");
}

static void test_quake_movement_integration(const sb::GymMap& gym, sb::PhysicsWorld& physics) {
    // Verify all 6 presets step deterministically and accelerate on ground
    for (int p = 0; p < QM_PRESET_COUNT; ++p) {
        qm_params_t params{};
        qm_preset_defaults(static_cast<qm_preset_id_t>(p), &params);
        qm_state_t st{};
        qm_state_init(&st, &params);
        qm_input_t in{};
        in.dt_sec       = 1.0f / 120.0f;
        in.wish_forward = 1.0f;
        in.on_ground    = 1;
        in.slope_normal[2] = 1.0f;
        for (int i = 0; i < 60; ++i) {
            qm_step(&params, &in, &st, &st);
        }
        CHECK(st.horiz_speed > params.max_speed * 0.85f && std::isfinite(st.horiz_speed),
              (std::string("qm_physics preset accelerates cleanly: ") + qm_preset_name(static_cast<qm_preset_id_t>(p))).c_str());
    }

    // Apex downhill 30-deg slope surf using real Jolt ramp normal
    qm_params_t apex{};
    qm_preset_defaults(QM_PRESET_APEX_LEGENDS, &apex);
    qm_state_t st{};
    qm_state_init(&st, &apex);
    const sb::RaycastHit ramp_hit = physics.raycast_world(
        {gym.ramp30_sample_xy.x, gym.ramp30_sample_xy.y, 12.0f}, {0.0f, 0.0f, -1.0f}, 20.0f);
    st.vel[1] = -apex.max_speed; // Moving downhill (-Y)
    qm_input_t surf_in{};
    surf_in.dt_sec         = 1.0f / 120.0f;
    surf_in.wish_forward   = 1.0f;
    surf_in.cam_yaw_rad    = sb::kPi; // Facing -Y (downhill)
    surf_in.crouch_held    = 1;
    surf_in.crouch_pressed = 1;
    surf_in.on_ground      = 1;
    surf_in.slope_normal[0] = ramp_hit.normal.x;
    surf_in.slope_normal[1] = ramp_hit.normal.y;
    surf_in.slope_normal[2] = ramp_hit.normal.z;
    const float initial_spd = qm_horiz_speed(st.vel);
    for (int i = 0; i < 90; ++i) {
        qm_step(&apex, &surf_in, &st, &st);
        surf_in.crouch_pressed = 0;
    }
    CHECK(st.sliding != 0 && st.horiz_speed > initial_spd + 2.0f,
          "Apex crouch-slide accelerates downhill using real Jolt 30-deg ramp normal");

    // Wall-bounce using real Jolt wall_probe callback beside the right wall of the corridor (X = -47.0)
    qm_state_t wb{};
    qm_state_init(&wb, &apex);
    wb.pos[0] = -47.6f; // ~0.6m from wall face at X = -47.0
    wb.pos[1] = 9.0f;
    wb.pos[2] = 1.2f;
    wb.vel[0] = 3.0f;
    wb.vel[2] = 1.0f;
    qm_input_t wb_in{};
    wb_in.dt_sec         = 1.0f / 120.0f;
    wb_in.cam_yaw_rad    = 0.0f;
    wb_in.jump_held      = 1;
    wb_in.jump_pressed   = 1;
    wb_in.on_ground      = 0;
    wb_in.wall_probe     = &sb::PhysicsWorld::qm_wall_probe_callback;
    wb_in.wall_probe_ctx = &physics;
    qm_step(&apex, &wb_in, &wb, &wb);
    CHECK(wb.wall_bounced != 0 && wb.vel[0] < -2.0f && wb.vel[2] > 3.0f,
          "qm_physics wall-bounce triggers off Jolt corridor wall and pushes player outward + upward");
}

static void test_weighty_aim_and_free_aim_composition() {
    wa_player_t wa{};
    wa_player_init(&wa);
    wa_select_curve(&wa.stick_cfg, WA_CURVE_CUSTOM1);

    // Verify curve graph generation
    wa_curve_graph_t graph{};
    wa_build_curve_graph(&wa.stick_cfg, 0.5f, 65, &graph);
    CHECK(graph.sample_count == 65 && graph.has_custom_handles == 1 && graph.has_live_marker == 1,
          "wa_build_curve_graph populates 65 samples, custom handles, and live marker");

    // Compose weighty-aim stick + gyro -> free-aim Weighty preset
    fa_profile_bank_t bank{};
    fa_profile_bank_init(&bank);
    fa_profile_apply_preset(&bank, FA_PRESET_WEIGHTY);
    fa_state_t fa_st{};
    fa_reset_state(&fa_st);
    fa_output_t fa_out{};

    const float dt = 1.0f / 120.0f;
    wa.gyro_cfg.mode = WA_GYRO_ALWAYS;
    wa.gyro_state.calibrated = 1;
    wa_gyro_sensor_t sensor{};
    sensor.available     = 1;
    sensor.gyro_deg_s[0] = 10.0f;
    sensor.gyro_deg_s[1] = -25.0f; // +right yaw in weighty_aim
    sensor.accel[1]      = 9.81f;

    for (int i = 0; i < 30; ++i) {
        float stick_rate[2] = {};
        wa_process_stick(&wa.stick_cfg, &wa.stick_state, 0.65f, 0.15f, dt, 0, stick_rate);
        float gyro_deg[2] = {};
        wa_process_gyro(&wa.gyro_cfg, &wa.gyro_state, &sensor, 0, 1, dt, 0, gyro_deg);

        fa_input_t fin{};
        fin.stick_deg[0]           = stick_rate[0] * 210.0f * dt;
        fin.stick_deg[1]           = stick_rate[1] * 210.0f * dt;
        fin.gyro_deg[0]            = gyro_deg[0];
        fin.gyro_deg[1]            = gyro_deg[1];
        fin.stick_deflection       = std::hypot(0.65f, 0.15f);
        fin.full_stick_deg_per_sec = 210.0f;
        fin.dt_sec                 = dt;
        fin.gameplay_active        = 1;
        fin.fov_scale              = fa_fov_scale(75.0f, 75.0f);
        fa_step(&bank.cfg, &fa_st, &fin, 0, &fa_out);
    }

    CHECK(fa_out.active == 1 && fa_out.reticle_deg[0] > 0.5f && fa_out.cam_delta_deg[0] > 0.0f,
          "weighty_aim -> free_aim composition drives unlocked reticle and camera delta");

    fa_debug_info_t dbg{};
    fa_get_debug_info(&fa_st, &dbg);
    CHECK(dbg.active == 1 && dbg.zone_x > 1.0f && dbg.zone_y > 1.0f,
          "fa_get_debug_info reports valid active ellipse and reticle telemetry");

    float ndc[2] = {};
    fa_project_reticle_ndc(fa_out.reticle_deg, 75.0f, 16.0f / 9.0f, ndc);
    CHECK(std::isfinite(ndc[0]) && std::isfinite(ndc[1]) && ndc[0] > 0.0f && ndc[0] < 0.97f,
          "fa_project_reticle_ndc maps reticle offset cleanly into screen NDC");

    char csv_hdr[256] = {}, csv_row[512] = {};
    const float in_d[2] = {1.0f, 0.2f}, cam_d[2] = {12.0f, 3.0f};
    const int hlen = fa_format_telemetry_csv_header(csv_hdr, sizeof(csv_hdr));
    const int rlen = fa_format_telemetry_csv_row(csv_row, sizeof(csv_row), 0.25f, dt, &bank.cfg, &fa_st, in_d, &fa_out, cam_d, 75.0f);
    CHECK(hlen > 20 && rlen > 20, "free-aim CSV telemetry header and row format cleanly");
}

static void test_libfpv_integration(const sb::GymMap& gym) {
    const std::string tmp_dir = (std::filesystem::temp_directory_path() / "portable_mods_sandbox_fpv_test").string();
    std::filesystem::create_directories(tmp_dir);

    fpv_global_config cfg{};
    fpv_global_config_defaults(&cfg);
    cfg.data_dir               = tmp_dir.c_str();
    cfg.coords.right           = FPV_AXIS_POS_X;
    cfg.coords.up              = FPV_AXIS_POS_Z;
    cfg.coords.forward         = FPV_AXIS_POS_Y;
    cfg.coords.units_per_meter = 1.0f;
    cfg.timing                 = FPV_TIMING_MANUAL;
    cfg.flags                  = FPV_INIT_NO_DEVICE_INPUT | FPV_INIT_NO_CONFIGURATOR;

    const fpv_result init_res = fpv_global_init(&cfg);
    CHECK(init_res == FPV_OK, "libfpv initializes cleanly in Z-up manual timing mode");
    if (init_res != FPV_OK) {
        std::fprintf(stderr, "  libfpv error: %s\n", fpv_last_error());
        return;
    }

    fpv_mesh mesh{};
    mesh.positions      = gym.static_mesh.vertices[0].pos;
    mesh.vertex_count   = static_cast<uint32_t>(gym.static_mesh.vertices.size());
    mesh.vertex_stride  = sizeof(sb::GymVertex);
    mesh.indices        = gym.static_mesh.indices.data();
    mesh.triangle_count = static_cast<uint32_t>(gym.static_mesh.indices.size() / 3);
    CHECK(fpv_static_surfaces_load(&mesh, 1) == FPV_OK,
          "fpv_static_surfaces_load accepts unified Testing Gym triangles");

    fpv_drone_desc desc{};
    fpv_drone_desc_defaults(&desc);
    desc.position[0] = gym.drone_spawn.x;
    desc.position[1] = gym.drone_spawn.y;
    desc.position[2] = gym.drone_spawn.z + 1.0f; // Drop 1m onto launch pad to verify gravity + collision
    fpv_drone* drone = nullptr;
    CHECK(fpv_drone_create(&desc, &drone) == FPV_OK && drone != nullptr,
          "fpv_drone_create spawns drone in FPV Yard");

    if (drone) {
        CHECK(fpv_audio_configure(48000, 2) == FPV_OK, "fpv_audio_configure configures 48kHz stereo");

        fpv_frame frame{};
        frame.struct_size = sizeof(frame);
        for (int i = 0; i < 60; ++i) {
            fpv_step(1.0f / 60.0f);
            fpv_drone_tick(drone, 1.0f / 60.0f, &frame);
        }
        CHECK(frame.sim_time > 0.5 && std::isfinite(frame.position[2]) && frame.position[2] >= 0.0f && frame.position[2] < 1.2f,
              "libfpv steps Betaflight SimITL and lands drone cleanly on Testing Gym launch pad");

        std::vector<float> pcm(512 * 2, 0.0f);
        const uint32_t got = fpv_audio_read(pcm.data(), 512);
        bool audio_finite = true;
        for (float s : pcm) {
            if (!std::isfinite(s)) { audio_finite = false; break; }
        }
        CHECK(got == 512 && audio_finite, "fpv_audio_read returns 512 finite stereo PCM frames");

        // Regression (P0-1): Keyboard FPV mode with no resting throttle override arms cleanly on Enter
        fpv_input_clear_channels();
        fpv_input_key(FPV_KEY_ENTER, 1);
        for (int i = 0; i < 6; ++i) {
            fpv_step(1.0f / 60.0f);
            fpv_drone_tick(drone, 1.0f / 60.0f, &frame);
        }
        fpv_input_key(FPV_KEY_ENTER, 0);
        for (int i = 0; i < 20; ++i) {
            fpv_step(1.0f / 60.0f);
            fpv_drone_tick(drone, 1.0f / 60.0f, &frame);
        }
        CHECK(frame.channels[2] <= -0.95f && (frame.flags & FPV_DRONE_ARMED) != 0,
              "Keyboard FPV Enter arms Betaflight cleanly with idle -1.0 throttle (no THROTTLE block)");

        // Disarm cleanly after arm test
        fpv_input_key(FPV_KEY_ENTER, 1);
        fpv_step(1.0f / 60.0f);
        fpv_input_key(FPV_KEY_ENTER, 0);
        fpv_step(1.0f / 60.0f);

        // Regression: FPV drone reset restores exact upright launch-pad pose, zero velocity, and disarmed state
        {
            // Push drone off the launch pad with strong linear velocity while armed via host channels
            float armed_ch[FPV_CHANNEL_COUNT] = {};
            armed_ch[0] = 0.4f;
            armed_ch[1] = 0.3f;
            armed_ch[2] = 0.2f;
            armed_ch[4] = 1.0f; // Armed
            armed_ch[5] = 1.0f;
            fpv_input_set_channels(armed_ch, FPV_CHANNEL_COUNT);
            const float launch_vel[3] = {8.0f, 6.0f, 9.0f};
            fpv_drone_push(drone, launch_vel, 1);
            for (int i = 0; i < 30; ++i) {
                fpv_step(1.0f / 60.0f);
                fpv_drone_tick(drone, 1.0f / 60.0f, &frame);
            }
            const float displaced_dist = std::hypot(frame.position[0] - gym.drone_spawn.x,
                                                    frame.position[1] - gym.drone_spawn.y);
            CHECK(displaced_dist > 1.0f, "Drone displaces away from launch pad after push & flight");

            // Execute full reset: disarm host channels to idle throttle, repair, and reset to upright launch-pad pose
            float disarmed_ch[FPV_CHANNEL_COUNT] = {};
            disarmed_ch[2] = -1.0f; // Idle throttle
            disarmed_ch[4] = -1.0f; // Disarmed
            disarmed_ch[5] =  1.0f;
            disarmed_ch[6] = -1.0f;
            disarmed_ch[7] = -1.0f;
            disarmed_ch[8] =  1.0f;
            disarmed_ch[9] = -1.0f;
            fpv_input_set_channels(disarmed_ch, FPV_CHANNEL_COUNT);
            fpv_drone_repair(drone);
            const float spawn_pos[3] = {gym.drone_spawn.x, gym.drone_spawn.y, gym.drone_spawn.z};
            const float upright_q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            fpv_drone_reset(drone, spawn_pos, upright_q);
            for (int i = 0; i < 6; ++i) {
                fpv_step(1.0f / 60.0f);
                fpv_drone_tick(drone, 1.0f / 60.0f, &frame);
            }

            const float pos_err = std::sqrt(
                (frame.position[0] - gym.drone_spawn.x) * (frame.position[0] - gym.drone_spawn.x) +
                (frame.position[1] - gym.drone_spawn.y) * (frame.position[1] - gym.drone_spawn.y) +
                (frame.position[2] - gym.drone_spawn.z) * (frame.position[2] - gym.drone_spawn.z));
            const float lin_spd = std::sqrt(
                frame.velocity[0] * frame.velocity[0] +
                frame.velocity[1] * frame.velocity[1] +
                frame.velocity[2] * frame.velocity[2]);
            const float ang_spd = std::sqrt(
                frame.angular_velocity[0] * frame.angular_velocity[0] +
                frame.angular_velocity[1] * frame.angular_velocity[1] +
                frame.angular_velocity[2] * frame.angular_velocity[2]);
            const bool upright = std::fabs(frame.rotation[3]) > 0.99f &&
                                 std::fabs(frame.rotation[0]) < 0.08f &&
                                 std::fabs(frame.rotation[1]) < 0.08f;
            CHECK(pos_err < 0.25f && upright && lin_spd < 0.5f && ang_spd < 0.5f &&
                  (frame.flags & FPV_DRONE_ARMED) == 0 && frame.channels[2] <= -0.95f,
                  "FPV drone reset restores upright launch-pad pose, zero velocity, and disarmed idle state");
            fpv_input_clear_channels();
        }

        fpv_drone_delete(drone);
    }

    fpv_static_surfaces_clear();
    fpv_global_terminate();
}

static void test_weapon_fire_and_audio_synth() {
    // 1. Continuous auto-fire cadence at 600 RPM = 10 shots/sec
    sb::WeaponFireController wf{};
    CHECK(wf.auto_fire && std::fabs(wf.fire_rpm - 600.0f) < 0.01f,
          "WeaponFireController defaults to Continuous Full-Auto at 600 RPM");

    const float dt = 1.0f / 60.0f;
    int shots_600 = 0;
    for (int f = 0; f < 60; ++f) {
        shots_600 += wf.update(dt, true, true, f == 0);
    }
    CHECK(shots_600 == 10, "Continuous fire at 600 RPM fires exactly 10 shots over 1.0 second");

    // 2. Continuous auto-fire cadence at 1200 RPM = 20 shots/sec
    wf.reset();
    wf.fire_rpm = 1200.0f;
    int shots_1200 = 0;
    for (int f = 0; f < 60; ++f) {
        shots_1200 += wf.update(dt, true, true, f == 0);
    }
    CHECK(shots_1200 == 20, "Continuous fire at 1200 RPM fires exactly 20 shots over 1.0 second");

    // 3. Releasing & immediately re-pressing respects cooldown (no double-fire burst exploit)
    wf.reset();
    wf.fire_rpm = 600.0f; // 0.10s interval
    const int first = wf.update(dt, true, true, true);
    const int release_f = wf.update(dt, true, false, false); // cooldown ~0.083s
    const int repress_early = wf.update(dt, true, true, true); // cooldown ~0.067s -> must NOT double-fire
    CHECK(first == 1 && release_f == 0 && repress_early == 0,
          "Quick release/re-press respects remaining RPM cooldown (no burst exploit)");

    // Waiting past cooldown fires immediately on next press (no dropped first shot)
    for (int f = 0; f < 8; ++f) wf.update(dt, true, false, false);
    const int fresh_press = wf.update(dt, true, true, true);
    CHECK(fresh_press == 1, "Fresh press after cooldown expires fires immediately with zero dropped shot");

    // 4. Semi-auto fires exactly 1 shot per rising-edge press even when trigger stays held
    wf.reset();
    wf.auto_fire = false;
    int semi_shots = 0;
    for (int f = 0; f < 30; ++f) {
        semi_shots += wf.update(dt, true, true, f == 0);
    }
    CHECK(semi_shots == 1, "Semi-auto fires exactly 1 shot per press even while trigger is held");

    // 5. Procedural WeaponAudioSynth: gunshot produces finite, non-zero samples and decays to silence
    sb::WeaponAudioSynth synth{};
    synth.set_volume(0.8f);
    synth.set_muted(false);
    synth.trigger_gunshot();

    std::vector<float> buf(4800 * 2, 0.0f); // 100 ms
    synth.render_stereo(buf.data(), 4800);
    bool all_finite = true;
    float peak_abs = 0.0f;
    for (float s : buf) {
        if (!std::isfinite(s) || std::fabs(s) > 1.0001f) all_finite = false;
        peak_abs = std::max(peak_abs, std::fabs(s));
    }
    CHECK(all_finite && peak_abs > 0.1f,
          "Procedural gunshot synth generates finite, non-zero stereo PCM within [-1, +1]");

    // Advance past 120 ms total -> must decay cleanly to exact silence
    std::vector<float> tail(2048 * 2, 0.0f);
    synth.render_stereo(tail.data(), 2048); // covers 100..142 ms
    std::vector<float> after(512 * 2, 1.0f);
    synth.render_stereo(after.data(), 512);
    float after_peak = 0.0f;
    for (float s : after) after_peak = std::max(after_peak, std::fabs(s));
    CHECK(after_peak == 0.0f && synth.active_voice_count() == 0,
          "Procedural gunshot voice decays cleanly to exact silence");

    // 6. Hit-confirm audio produces finite, non-zero ping and decays cleanly to silence
    synth.set_hit_confirm_enabled(true);
    synth.trigger_hit_confirm();
    std::vector<float> hit_buf(2048 * 2, 0.0f); // ~42.7 ms (> 36 ms voice)
    synth.render_stereo(hit_buf.data(), 2048);
    bool hit_finite = true;
    float hit_peak = 0.0f;
    for (float s : hit_buf) {
        if (!std::isfinite(s) || std::fabs(s) > 1.0001f) hit_finite = false;
        hit_peak = std::max(hit_peak, std::fabs(s));
    }
    std::vector<float> hit_after(256 * 2, 1.0f);
    synth.render_stereo(hit_after.data(), 256);
    float hit_after_peak = 0.0f;
    for (float s : hit_after) hit_after_peak = std::max(hit_after_peak, std::fabs(s));
    CHECK(hit_finite && hit_peak > 0.05f && hit_after_peak == 0.0f,
          "Procedural hit-confirm synth produces finite non-zero ping and decays to silence");

    // 7. Overlapping rapid fire stress test remains soft-limited & finite
    for (int i = 0; i < 8; ++i) synth.trigger_gunshot();
    synth.trigger_hit_confirm();
    std::vector<float> stress(1024 * 2, 0.0f);
    synth.render_stereo(stress.data(), 1024);
    bool stress_ok = true;
    for (float s : stress) {
        if (!std::isfinite(s) || std::fabs(s) > 1.0f) { stress_ok = false; break; }
    }
    CHECK(stress_ok, "Rapid overlapping gunshots stay finite and soft-limited within [-1, +1]");
}

static void test_sandbox_wa_defaults_menu_bypass_and_settings() {
    // 1. Sandbox weighty-aim right-stick defaults: 0.03 inner, 0.98 outer, Balanced curve
    wa_stick_cfg_t sc{};
    wa_stick_state_t st{};
    sb::apply_sandbox_wa_defaults(sc);
    wa_stick_state_init(&st);
    CHECK(std::fabs(sc.inner_deadzone - 0.03f) < 1e-5f &&
          std::fabs(sc.outer_deadzone - 0.98f) < 1e-5f &&
          sc.curve == WA_CURVE_BALANCED,
          "Sandbox weighty-aim defaults enforce 0.03 inner and 0.98 outer right-stick thresholds");

    float out_in[2] = {}, out_above[2] = {}, out_98[2] = {}, out_100[2] = {};
    wa_process_stick(&sc, &st, 0.025f, 0.0f, 1.0f / 60.0f, 0, out_in);
    wa_process_stick(&sc, &st, 0.06f,  0.0f, 1.0f / 60.0f, 0, out_above);
    wa_stick_state_init(&st);
    wa_process_stick(&sc, &st, 0.98f,  0.0f, 1.0f / 60.0f, 0, out_98);
    wa_stick_state_init(&st);
    wa_process_stick(&sc, &st, 1.00f,  0.0f, 1.0f / 60.0f, 0, out_100);
    CHECK(std::fabs(out_in[0]) < 1e-6f && out_above[0] > 0.0f &&
          std::fabs(out_98[0] - out_100[0]) < 1e-5f && out_98[0] >= 0.99f,
          "Sandbox right-stick deadzones suppress <=0.03 and saturate cleanly at 0.98 radius");

    // Reset helper restores 0.03 / 0.98 after user edits
    sc.inner_deadzone = 0.15f;
    sc.outer_deadzone = 0.85f;
    sb::reset_sandbox_wa_stick_response(sc);
    CHECK(std::fabs(sc.inner_deadzone - 0.03f) < 1e-5f && std::fabs(sc.outer_deadzone - 0.98f) < 1e-5f,
          "Weighty Aim Reset Defaults restores 0.03 inner and 0.98 outer deadzones");

    // 2. Controller bypasses menu (ui_open == true) + Start button rising-edge toggles ui_open
    sb::MenuInputRouter router{};
    bool ui_open = true;

    // With bypass ON and ui_open == true: KB+Mouse blocked, Gamepad live!
    sb::MenuRoutingResult r1 = router.evaluate(true, ui_open, false, true, false);
    CHECK(!r1.allow_kbm && r1.allow_gamepad && !r1.toggle_ui,
          "With ui_open=true & controller bypass ON, KB+mouse are blocked while gamepad stays live");

    // With bypass OFF and ui_open == true: both KB+Mouse and Gamepad blocked
    sb::MenuRoutingResult r2 = router.evaluate(true, ui_open, false, false, false);
    CHECK(!r2.allow_kbm && !r2.allow_gamepad && !r2.toggle_ui,
          "With ui_open=true & controller bypass OFF, both KB+mouse and gamepad are paused");

    // Start button rising edge toggles ui_open from true -> false (even with bypass OFF)
    sb::MenuRoutingResult r3 = router.evaluate(true, ui_open, false, false, true);
    if (r3.toggle_ui) ui_open = !ui_open;
    CHECK(r3.toggle_ui && !ui_open && r3.allow_kbm && r3.allow_gamepad,
          "Start button rising edge closes Tuning Panel and restores KB+mouse & gamepad");

    // Holding Start across next frame must NOT re-toggle
    sb::MenuRoutingResult r4 = router.evaluate(true, ui_open, false, true, true);
    CHECK(!r4.toggle_ui && !ui_open,
          "Holding Start button continuously does not re-toggle Tuning Panel");

    // Release Start, then press Start again -> opens Tuning Panel while keeping gamepad live
    router.evaluate(true, ui_open, false, true, false);
    sb::MenuRoutingResult r5 = router.evaluate(true, ui_open, false, true, true);
    if (r5.toggle_ui) ui_open = !ui_open;
    CHECK(r5.toggle_ui && ui_open && !r5.allow_kbm && r5.allow_gamepad,
          "Second Start button press opens Tuning Panel while gamepad bypass keeps controller live");

    // 3. AimStats tracking & reset
    sb::AimStats stats{};
    stats.record_shot(true);
    stats.record_shot(true);
    stats.record_shot(false);
    stats.record_shot(true);
    CHECK(stats.shots_fired == 4 && stats.target_hits == 3 &&
          stats.current_streak == 1 && stats.best_streak == 2 &&
          std::fabs(stats.accuracy_pct() - 75.0f) < 0.01f,
          "AimStats accurately tracks shots, hits, accuracy %, and streaks");
    stats.reset();
    CHECK(stats.shots_fired == 0 && stats.target_hits == 0 && stats.best_streak == 0,
          "AimStats::reset clears all counters cleanly");

    // 4. Local settings INI save/load round-trip
    sb::SandboxSettings src{};
    src.auto_fire                = false;
    src.fire_rpm                 = 925.0f;
    src.gun_audio_volume         = 1.15f;
    src.gun_audio_muted          = true;
    src.hit_confirm_sound        = false;
    src.fullscreen               = true;
    src.base_world_fovy_deg      = 92.5f;
    src.viewmodel_fovy_deg       = 62.0f;
    src.mouse_sens_deg_per_px    = 0.18f;
    src.show_hud                 = false;
    src.ads_toggle_mode          = true;
    src.controller_bypasses_menu = false;
    src.wa_curve                 = static_cast<int>(WA_CURVE_CUSTOM2);
    src.wa_inner_deadzone        = 0.05f;
    src.wa_outer_deadzone        = 0.96f;
    src.wa_sensitivity           = 1.45f;
    src.wa_vertical_sens         = 0.95f;
    src.wa_full_stick_deg_per_sec = 275.0f;
    src.wa_gyro_mode             = static_cast<int>(WA_GYRO_AIMING);
    src.wa_gyro_sensitivity      = 4.25f;
    src.qm_preset                = static_cast<int>(QM_PRESET_QUAKE_CPMA);
    src.physics_tick_hz          = 240;
    src.fa_preset                = static_cast<int>(FA_PRESET_ARCADE);
    src.fpv_audio_volume         = 0.42f;
    src.fpv_audio_muted          = true;
    src.reticle_style             = static_cast<int>(sb::ReticleStyle::CircleDot);
    src.reticle_arm_size          = 14.5f;
    src.reticle_gap               = 6.5f;
    src.reticle_thickness         = 3.2f;
    src.reticle_dot_size          = 3.5f;
    src.reticle_outline           = false;
    src.reticle_outline_thickness = 2.4f;
    src.reticle_color[0]          = 0.08f;
    src.reticle_color[1]          = 0.96f;
    src.reticle_color[2]          = 1.00f;
    src.reticle_opacity           = 0.88f;

    const std::string ini_path = (std::filesystem::temp_directory_path() / "portable_mods_sandbox_settings_test.ini").string();
    const bool saved = sb::save_sandbox_settings_file(ini_path, src);
    sb::SandboxSettings dst{};
    const bool loaded = sb::load_sandbox_settings_file(ini_path, dst);
    std::error_code ec;
    std::filesystem::remove(ini_path, ec);

    CHECK(saved && loaded &&
          dst.auto_fire == src.auto_fire &&
          std::fabs(dst.fire_rpm - src.fire_rpm) < 0.01f &&
          std::fabs(dst.gun_audio_volume - src.gun_audio_volume) < 0.01f &&
          dst.gun_audio_muted == src.gun_audio_muted &&
          dst.hit_confirm_sound == src.hit_confirm_sound &&
          dst.fullscreen == src.fullscreen &&
          std::fabs(dst.base_world_fovy_deg - src.base_world_fovy_deg) < 0.01f &&
          std::fabs(dst.viewmodel_fovy_deg - src.viewmodel_fovy_deg) < 0.01f &&
          std::fabs(dst.mouse_sens_deg_per_px - src.mouse_sens_deg_per_px) < 1e-4f &&
          dst.show_hud == src.show_hud &&
          dst.ads_toggle_mode == src.ads_toggle_mode &&
          dst.controller_bypasses_menu == src.controller_bypasses_menu &&
          dst.wa_curve == src.wa_curve &&
          std::fabs(dst.wa_inner_deadzone - src.wa_inner_deadzone) < 1e-4f &&
          std::fabs(dst.wa_outer_deadzone - src.wa_outer_deadzone) < 1e-4f &&
          std::fabs(dst.wa_sensitivity - src.wa_sensitivity) < 1e-4f &&
          std::fabs(dst.wa_vertical_sens - src.wa_vertical_sens) < 1e-4f &&
          std::fabs(dst.wa_full_stick_deg_per_sec - src.wa_full_stick_deg_per_sec) < 0.01f &&
          dst.wa_gyro_mode == src.wa_gyro_mode &&
          std::fabs(dst.wa_gyro_sensitivity - src.wa_gyro_sensitivity) < 1e-4f &&
          dst.qm_preset == src.qm_preset &&
          dst.physics_tick_hz == src.physics_tick_hz &&
          dst.fa_preset == src.fa_preset &&
          std::fabs(dst.fpv_audio_volume - src.fpv_audio_volume) < 1e-4f &&
          dst.fpv_audio_muted == src.fpv_audio_muted &&
          dst.reticle_style == src.reticle_style &&
          std::fabs(dst.reticle_arm_size - src.reticle_arm_size) < 0.01f &&
          std::fabs(dst.reticle_gap - src.reticle_gap) < 0.01f &&
          std::fabs(dst.reticle_thickness - src.reticle_thickness) < 0.01f &&
          std::fabs(dst.reticle_dot_size - src.reticle_dot_size) < 0.01f &&
          dst.reticle_outline == src.reticle_outline &&
          std::fabs(dst.reticle_outline_thickness - src.reticle_outline_thickness) < 0.01f &&
          std::fabs(dst.reticle_color[0] - src.reticle_color[0]) < 1e-3f &&
          std::fabs(dst.reticle_color[1] - src.reticle_color[1]) < 1e-3f &&
          std::fabs(dst.reticle_color[2] - src.reticle_color[2]) < 1e-3f &&
          std::fabs(dst.reticle_opacity - src.reticle_opacity) < 1e-3f,
          "SandboxSettings INI save/load round-trip preserves all fields including reticle settings");

    // 5. Default ReticleConfig provides high-contrast neon-green outlined crosshair+dot
    sb::ReticleConfig rdef{};
    CHECK(rdef.style == sb::ReticleStyle::CrosshairDot && rdef.outline &&
          rdef.thickness >= 2.0f && rdef.opacity >= 0.90f && rdef.dot_size >= 2.0f,
          "Default ReticleConfig is high-contrast outlined Crosshair+Dot");
}

int main() {
    std::printf("=== Portable Mods Testing Sandbox — Headless Smoke Suite ===\n");
    const sb::GymMap gym = sb::GymMap::build();
    sb::PhysicsWorld physics{};
    const bool phys_ok = physics.init(gym.static_mesh, sb::GymMap::get_teleport(sb::TeleportId::HubSpawn).position);
    CHECK(phys_ok, "Jolt PhysicsWorld initializes with Testing Gym static MeshShape");

    if (phys_ok) {
        test_gym_and_jolt_physics(gym, physics);
        test_quake_movement_integration(gym, physics);
    }
    test_input_latch_and_fpv_gamepad_mapper();
    test_weapon_fire_and_audio_synth();
    test_sandbox_wa_defaults_menu_bypass_and_settings();
    test_weighty_aim_and_free_aim_composition();
    test_libfpv_integration(gym);

    physics.shutdown();
    std::printf("=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
