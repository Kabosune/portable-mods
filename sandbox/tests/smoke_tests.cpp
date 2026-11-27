#include "gym_map.h"
#include "physics_world.h"
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

        fpv_drone_delete(drone);
    }

    fpv_static_surfaces_clear();
    fpv_global_terminate();
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
    test_weighty_aim_and_free_aim_composition();
    test_libfpv_integration(gym);

    physics.shutdown();
    std::printf("=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
