#pragma once

#include "math_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sb {

struct GymVertex {
    float pos[3];
    float normal[3];
    float color[3];
    float grid_scale; // > 0 enables world-space meter checkerboard/grid shading
};

struct MeshData {
    std::vector<GymVertex> vertices;
    std::vector<uint32_t>  indices;

    void add_tri(Vec3 a, Vec3 b, Vec3 c, Vec3 color, float grid_scale = 0.0f);
    void add_quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color, float grid_scale = 0.0f);
    void add_box(Vec3 center, Vec3 half_extents, Vec3 color, float grid_scale = 0.0f, float yaw_rad = 0.0f);
    // Axis-aligned ramp rising along +Y (or rotated by yaw_rad) from z0 to z0+height over run length.
    void add_ramp(Vec3 base_center, float width, float run_y, float height_z, Vec3 color, float grid_scale = 1.0f);
};

enum class TeleportId {
    HubSpawn = 0,
    MovementWing,
    AimingRange,
    FpvYard,
    Count
};

struct TeleportPoint {
    const char* name;
    Vec3        position;   // Feet position in Z-up meters
    float       yaw_rad;    // qm_physics convention (0 = +Y, +CCW)
    float       pitch_rad;
};

struct RangeTarget {
    int         id = 0;
    std::string label;
    Vec3        base_pos;
    Vec3        current_pos;
    Vec3        half_extents;
    Vec3        color;
    bool        moving = false;
    Vec3        move_axis{1.0f, 0.0f, 0.0f};
    float       move_amplitude = 0.0f;
    float       move_speed = 0.0f;
    float       move_phase = 0.0f;
    float       hit_flash_timer = 0.0f;
    int         hit_count = 0;
};

struct GymMap {
    MeshData                   static_mesh;
    std::vector<RangeTarget>   targets;
    Vec3                       drone_spawn{38.0f, -6.0f, 0.15f};
    Vec3                       ramp30_sample_xy{-24.0f, 14.0f, 0.0f}; // Known 30-deg surf ramp test point
    Vec3                       wall_lane_center{-50.0f, 8.0f, 1.5f};  // Known wall-bounce corridor test point

    static GymMap build();

    void update_targets(float sim_time_sec, float dt_sec);
    static TeleportPoint get_teleport(TeleportId id);
};

} // namespace sb
