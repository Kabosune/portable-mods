#pragma once

#include "gym_map.h"
#include "math_types.h"
#include "qm_physics.h"

#include <memory>
#include <vector>

namespace sb {

struct RaycastHit {
    bool  hit = false;
    float distance = 0.0f;
    Vec3  point{};
    Vec3  normal{0.0f, 0.0f, 1.0f};
    int   target_id = -1; // >= 0 if an Aiming Range target was hit, -1 for static world
};

struct CharacterMoveResult {
    Vec3  feet_pos{};
    Vec3  velocity{};
    bool  on_ground = false;
    Vec3  slope_normal{0.0f, 0.0f, 1.0f};
    bool  crouching = false;
    float eye_height = 1.62f;
};

class PhysicsWorld {
public:
    static constexpr float kPlayerRadius       = 0.35f;
    static constexpr float kStandTotalHeight   = 1.80f;
    static constexpr float kCrouchTotalHeight  = 1.10f;
    static constexpr float kStandEyeHeight     = 1.62f;
    static constexpr float kCrouchEyeHeight    = 0.94f;

    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    bool init(const MeshData& gym_mesh, Vec3 spawn_feet_pos);
    void shutdown();

    void teleport_player(Vec3 feet_pos);
    CharacterMoveResult query_character_state() const;

    // Step Jolt CharacterVirtual with velocity computed by qm_step.
    // `jumping_up` disables stick-to-floor so bunnyhops / jumps take off cleanly.
    CharacterMoveResult step_character(Vec3 desired_vel_zup, float dt_sec, bool want_crouch, bool jumping_up);

    // Static world raycast (Z-up meters)
    RaycastHit raycast_world(Vec3 origin, Vec3 dir_unit, float max_dist) const;

    // Combined hitscan raycast against static gym + dynamic aiming targets
    RaycastHit raycast_hitscan(Vec3 origin, Vec3 dir_unit, float max_dist, const std::vector<RangeTarget>& targets) const;

    // Wall-bounce probe matching qm_wall_probe_fn_t signature
    bool probe_wall(Vec3 player_feet_pos, Vec3 probe_dir, float max_dist, qm_wall_probe_result_t* out_hit) const;
    static int qm_wall_probe_callback(
        const float pos[3],
        const float probe_dir[3],
        float max_dist_m,
        qm_wall_probe_result_t* out_hit,
        void* user_ctx);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sb
