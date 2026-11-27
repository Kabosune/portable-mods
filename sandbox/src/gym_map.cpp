#include "gym_map.h"

#include <cmath>

namespace sb {

void MeshData::add_tri(Vec3 a, Vec3 b, Vec3 c, Vec3 color, float grid_scale) {
    const Vec3 n = normalize(cross(b - a, c - a));
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const Vec3 pts[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        GymVertex v{};
        v.pos[0] = pts[i].x;
        v.pos[1] = pts[i].y;
        v.pos[2] = pts[i].z;
        v.normal[0] = n.x;
        v.normal[1] = n.y;
        v.normal[2] = n.z;
        v.color[0] = color.x;
        v.color[1] = color.y;
        v.color[2] = color.z;
        v.grid_scale = grid_scale;
        vertices.push_back(v);
    }
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

void MeshData::add_quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color, float grid_scale) {
    add_tri(a, b, c, color, grid_scale);
    add_tri(a, c, d, color, grid_scale);
}

void MeshData::add_box(Vec3 center, Vec3 h, Vec3 color, float grid_scale, float yaw_rad) {
    const float cs = std::cos(yaw_rad);
    const float sn = std::sin(yaw_rad);
    auto pt = [&](float x, float y, float z) -> Vec3 {
        return {
            center.x + x * cs - y * sn,
            center.y + x * sn + y * cs,
            center.z + z,
        };
    };

    const Vec3 p000 = pt(-h.x, -h.y, -h.z);
    const Vec3 p100 = pt( h.x, -h.y, -h.z);
    const Vec3 p110 = pt( h.x,  h.y, -h.z);
    const Vec3 p010 = pt(-h.x,  h.y, -h.z);
    const Vec3 p001 = pt(-h.x, -h.y,  h.z);
    const Vec3 p101 = pt( h.x, -h.y,  h.z);
    const Vec3 p111 = pt( h.x,  h.y,  h.z);
    const Vec3 p011 = pt(-h.x,  h.y,  h.z);

    // Outward CCW faces in right-handed Z-up space
    add_quad(p001, p101, p111, p011, color, grid_scale); // +Z top
    add_quad(p000, p010, p110, p100, color, grid_scale); // -Z bottom
    add_quad(p100, p110, p111, p101, color, grid_scale); // +X right
    add_quad(p010, p000, p001, p011, color, grid_scale); // -X left
    add_quad(p110, p010, p011, p111, color, grid_scale); // +Y front
    add_quad(p000, p100, p101, p001, color, grid_scale); // -Y back
}

void MeshData::add_ramp(Vec3 base_center, float width, float run_y, float height_z, Vec3 color, float grid_scale) {
    const float hx = width * 0.5f;
    const float y0 = base_center.y;
    const float y1 = base_center.y + run_y;
    const float z0 = base_center.z;
    const float z1 = base_center.z + height_z;

    const Vec3 bl = {base_center.x - hx, y0, z0};
    const Vec3 br = {base_center.x + hx, y0, z0};
    const Vec3 tr_base = {base_center.x + hx, y1, z0};
    const Vec3 tl_base = {base_center.x - hx, y1, z0};
    const Vec3 tr_top  = {base_center.x + hx, y1, z1};
    const Vec3 tl_top  = {base_center.x - hx, y1, z1};

    // Sloped face facing -Y and +Z (players slide downhill toward -Y)
    add_quad(bl, br, tr_top, tl_top, color, grid_scale);
    // Back vertical face (+Y)
    add_quad(tr_base, tl_base, tl_top, tr_top, color * 0.82f, grid_scale);
    // Left vertical triangle (-X)
    add_tri(tl_base, bl, tl_top, color * 0.88f, grid_scale);
    // Right vertical triangle (+X)
    add_tri(br, tr_base, tr_top, color * 0.88f, grid_scale);
    // Bottom face (-Z)
    add_quad(bl, tl_base, tr_base, br, color * 0.7f, 0.0f);
}

static void add_gate(MeshData& m, Vec3 base_center, float inner_w, float inner_h, float yaw_rad, Vec3 color) {
    const float post_t = 0.25f;
    const float half_w = inner_w * 0.5f + post_t;
    const float cs = std::cos(yaw_rad);
    const float sn = std::sin(yaw_rad);
    auto offset = [&](float lx, float ly, float lz) -> Vec3 {
        return {
            base_center.x + lx * cs - ly * sn,
            base_center.y + lx * sn + ly * cs,
            base_center.z + lz,
        };
    };

    // Left post, right post, top bar, low threshold bar
    m.add_box(offset(-half_w, 0.0f, inner_h * 0.5f), {post_t, post_t, inner_h * 0.5f}, color, 0.0f, yaw_rad);
    m.add_box(offset( half_w, 0.0f, inner_h * 0.5f), {post_t, post_t, inner_h * 0.5f}, color, 0.0f, yaw_rad);
    m.add_box(offset(0.0f, 0.0f, inner_h + post_t), {half_w + post_t, post_t, post_t}, color, 0.0f, yaw_rad);
    m.add_box(offset(0.0f, 0.0f, 0.12f), {half_w + post_t, post_t, 0.12f}, color * 0.7f, 0.0f, yaw_rad);
}

GymMap GymMap::build() {
    GymMap gym{};
    MeshData& m = gym.static_mesh;

    // -----------------------------------------------------------------------
    // 1. Unified Main Ground Floor (Z = 0, covers all three wings cleanly)
    //    X: [-90, +120], Y: [-60, +100]
    // -----------------------------------------------------------------------
    const Vec3 floor_col{0.46f, 0.49f, 0.53f};
    m.add_box({15.0f, 20.0f, -0.5f}, {105.0f, 80.0f, 0.5f}, floor_col, 1.0f);

    // Subtle perimeter low curb walls so player/drone have clear arena boundaries
    const Vec3 wall_col{0.34f, 0.37f, 0.42f};
    m.add_box({ 15.0f, 100.0f, 4.0f}, {105.0f, 0.5f, 4.0f}, wall_col, 0.5f);
    m.add_box({ 15.0f, -60.0f, 4.0f}, {105.0f, 0.5f, 4.0f}, wall_col, 0.5f);
    m.add_box({-90.0f,  20.0f, 4.0f}, {  0.5f, 80.0f, 4.0f}, wall_col, 0.5f);
    m.add_box({120.0f,  20.0f, 4.0f}, {  0.5f, 80.0f, 4.0f}, wall_col, 0.5f);

    // Hub Spawn Plaza landmark pad at (0, -6, 0)
    m.add_box({0.0f, -6.0f, 0.02f}, {4.5f, 4.5f, 0.02f}, {0.26f, 0.58f, 0.78f}, 1.0f);

    // Color-coded floor signposts leading to the 3 wings
    m.add_box({-10.0f, -6.0f, 0.015f}, {3.0f, 1.5f, 0.015f}, {0.92f, 0.56f, 0.16f}, 1.0f); // Movement Wing (-X)
    m.add_box({  0.0f,  6.0f, 0.015f}, {2.0f, 3.5f, 0.015f}, {0.82f, 0.25f, 0.25f}, 1.0f); // Aiming Range (+Y)
    m.add_box({ 14.0f, -6.0f, 0.015f}, {3.5f, 1.5f, 0.015f}, {0.20f, 0.76f, 0.42f}, 1.0f); // FPV Yard (+X)

    // -----------------------------------------------------------------------
    // 2. Movement Wing (X: -66 .. -14, Y: -32 .. +44)
    // -----------------------------------------------------------------------
    // 2a. Flat Strafe Runway with high-contrast 1m grid strip (X=-18, Y=-24..+32)
    m.add_box({-18.0f, 4.0f, 0.015f}, {4.0f, 28.0f, 0.015f}, {0.72f, 0.75f, 0.78f}, 1.0f);

    // 2b. Surf Ramps: 20 deg, 25 deg, 30 deg, and 35 deg (side by side along X, facing -Y)
    //     Players can walk/jump up the back stairs or launch from the top deck to downhill surf!
    const float ramp_angles_deg[4] = {20.0f, 25.0f, 30.0f, 35.0f};
    const Vec3  ramp_colors[4] = {
        {0.25f, 0.68f, 0.85f},
        {0.25f, 0.78f, 0.62f},
        {0.92f, 0.64f, 0.20f},
        {0.90f, 0.34f, 0.26f},
    };
    const float ramp_run = 11.0f;
    const float ramp_y0  = 8.0f;
    for (int i = 0; i < 4; ++i) {
        const float rx = -24.0f - i * 6.5f;
        const float h  = ramp_run * std::tan(ramp_angles_deg[i] * kDegToRad);
        m.add_ramp({rx, ramp_y0, 0.0f}, 5.4f, ramp_run, h, ramp_colors[i], 1.0f);
        // Flat top launch deck behind each ramp
        m.add_box({rx, ramp_y0 + ramp_run + 2.5f, h * 0.5f}, {2.7f, 2.5f, h * 0.5f}, ramp_colors[i] * 0.85f, 1.0f);
    }
    // Note: 30 deg ramp is at index 2 -> rx = -24 - 13 = -37.0f, Y in [8, 19].
    gym.ramp30_sample_xy = {-37.0f, 13.5f, 0.0f};

    // Access ramp beside the 20-deg deck so the player can effortlessly climb to all surf decks
    m.add_ramp({-17.0f, ramp_y0 + ramp_run + 5.0f, 0.0f}, 3.5f, 10.0f, 4.0f, {0.55f, 0.60f, 0.66f}, 1.0f);
    // Connecting walkway at top behind all 4 ramps
    m.add_box({-34.0f, ramp_y0 + ramp_run + 5.8f, 4.0f}, {16.5f, 1.2f, 0.25f}, {0.52f, 0.56f, 0.62f}, 1.0f);

    // 2c. Parallel Wall-Bounce Corridor (X = -50 +/- 3.0m, Y = -4 .. +22, height 5.5m)
    const Vec3 wb_wall_col{0.28f, 0.52f, 0.82f};
    m.add_box({-53.25f, 9.0f, 2.75f}, {0.25f, 13.0f, 2.75f}, wb_wall_col, 1.0f);
    m.add_box({-46.75f, 9.0f, 2.75f}, {0.25f, 13.0f, 2.75f}, wb_wall_col, 1.0f);
    gym.wall_lane_center = {-50.0f, 9.0f, 1.5f};

    // 2d. Bhop Stepped Platforms (X = -28 .. -36, Y = -22 .. +2)
    for (int i = 0; i < 6; ++i) {
        const float py = -22.0f + i * 4.2f;
        const float pz = 0.25f + i * 0.22f;
        m.add_box({-31.0f, py, pz * 0.5f}, {1.6f, 1.2f, pz * 0.5f}, {0.40f, 0.72f, 0.52f}, 1.0f);
    }

    // 2e. Low Crouch-Slide Tunnel (clearance = 1.35m; standing=1.80m cannot pass, crouching=1.10m slides right through!)
    const Vec3 tunnel_col{0.78f, 0.42f, 0.18f};
    const float tun_x = -41.0f;
    const float tun_y = -14.0f;
    m.add_box({tun_x - 2.2f, tun_y, 1.2f}, {0.3f, 5.5f, 1.2f}, tunnel_col, 1.0f);
    m.add_box({tun_x + 2.2f, tun_y, 1.2f}, {0.3f, 5.5f, 1.2f}, tunnel_col, 1.0f);
    // Overhead beam bottom at Z = 1.35m (center Z = 1.85m, half_z = 0.50m)
    m.add_box({tun_x, tun_y, 1.85f}, {2.5f, 5.5f, 0.50f}, tunnel_col * 1.1f, 1.0f);

    // 2f. Dash-Jump Gap Platforms (X = -58, elevated 2.0m with increasing gaps: 5m, 7m, 9m)
    const Vec3 dash_col{0.66f, 0.34f, 0.80f};
    m.add_ramp({-58.0f, -28.0f, 0.0f}, 4.0f, 6.0f, 2.0f, dash_col * 0.85f, 1.0f);
    m.add_box({-58.0f, -20.0f, 1.0f}, {2.2f, 2.5f, 1.0f}, dash_col, 1.0f);
    m.add_box({-58.0f, -10.5f, 1.0f}, {2.2f, 2.0f, 1.0f}, dash_col, 1.0f);
    m.add_box({-58.0f,   0.5f, 1.0f}, {2.2f, 2.0f, 1.0f}, dash_col, 1.0f);
    m.add_box({-58.0f,  13.5f, 1.0f}, {2.5f, 2.5f, 1.0f}, dash_col * 1.1f, 1.0f);

    // -----------------------------------------------------------------------
    // 3. Aiming Range (X: -14 .. +14, Y: +12 .. +76)
    // -----------------------------------------------------------------------
    // Side lane dividers & range backstop
    m.add_box({-14.0f, 44.0f, 2.5f}, {0.35f, 32.0f, 2.5f}, {0.38f, 0.40f, 0.45f}, 1.0f);
    m.add_box({ 14.0f, 44.0f, 2.5f}, {0.35f, 32.0f, 2.5f}, {0.38f, 0.40f, 0.45f}, 1.0f);
    m.add_box({  0.0f, 76.0f, 4.0f}, {14.35f, 0.5f, 4.0f}, {0.30f, 0.32f, 0.36f}, 1.0f);

    // Shooting booth low counter at Y = 16.5 (player stands at Y = 14 facing +Y)
    m.add_box({0.0f, 16.5f, 0.45f}, {8.0f, 0.35f, 0.45f}, {0.50f, 0.44f, 0.38f}, 0.5f);

    // Distance stripe markers at 10m (Y=24), 25m (Y=39), 50m (Y=64) from firing line Y=14
    m.add_box({0.0f, 24.0f, 0.015f}, {13.0f, 0.25f, 0.015f}, {0.88f, 0.82f, 0.22f}, 0.0f);
    m.add_box({0.0f, 39.0f, 0.015f}, {13.0f, 0.25f, 0.015f}, {0.92f, 0.54f, 0.18f}, 0.0f);
    m.add_box({0.0f, 64.0f, 0.015f}, {13.0f, 0.25f, 0.015f}, {0.88f, 0.22f, 0.22f}, 0.0f);

    // Static & Moving Range Targets (tested dynamically by hitscan raycast + rendered)
    auto add_target = [&](int id, const char* label, Vec3 pos, Vec3 half_ext, Vec3 col,
                          bool moving = false, Vec3 axis = {1, 0, 0}, float amp = 0.0f, float spd = 0.0f, float ph = 0.0f) {
        RangeTarget t{};
        t.id = id;
        t.label = label;
        t.base_pos = pos;
        t.current_pos = pos;
        t.half_extents = half_ext;
        t.color = col;
        t.moving = moving;
        t.move_axis = axis;
        t.move_amplitude = amp;
        t.move_speed = spd;
        t.move_phase = ph;
        gym.targets.push_back(t);
    };

    // Near static targets (10m away, Y=24)
    add_target(1, "Near Left (10m)",   {-5.0f, 24.0f, 1.5f}, {0.45f, 0.12f, 0.85f}, {0.90f, 0.28f, 0.24f});
    add_target(2, "Near Center (10m)", { 0.0f, 24.0f, 1.5f}, {0.45f, 0.12f, 0.85f}, {0.92f, 0.42f, 0.22f});
    add_target(3, "Near Right (10m)",  { 5.0f, 24.0f, 1.5f}, {0.45f, 0.12f, 0.85f}, {0.90f, 0.28f, 0.24f});

    // Mid static targets (25m away, Y=39)
    add_target(4, "Mid Left (25m)",    {-7.5f, 39.0f, 1.8f}, {0.55f, 0.15f, 0.95f}, {0.95f, 0.65f, 0.18f});
    add_target(5, "Mid Right (25m)",   { 7.5f, 39.0f, 1.8f}, {0.55f, 0.15f, 0.95f}, {0.95f, 0.65f, 0.18f});

    // Far static targets (50m away, Y=64)
    add_target(6, "Far Left (50m)",    {-6.0f, 64.0f, 2.2f}, {0.75f, 0.20f, 1.25f}, {0.85f, 0.22f, 0.55f});
    add_target(7, "Far Center (50m)",  { 0.0f, 64.0f, 2.5f}, {0.75f, 0.20f, 1.25f}, {0.85f, 0.22f, 0.55f});
    add_target(8, "Far Right (50m)",   { 6.0f, 64.0f, 2.2f}, {0.75f, 0.20f, 1.25f}, {0.85f, 0.22f, 0.55f});

    // Moving / Strafing targets (near, mid, far)
    add_target(9,  "Strafe Near (17m)", {0.0f, 31.0f, 1.6f}, {0.48f, 0.14f, 0.85f}, {0.18f, 0.86f, 0.78f},
               true, {1.0f, 0.0f, 0.0f}, 5.5f, 1.4f, 0.0f);
    add_target(10, "Strafe Mid (35m)",  {0.0f, 49.0f, 2.0f}, {0.58f, 0.16f, 0.95f}, {0.22f, 0.78f, 0.95f},
               true, {1.0f, 0.0f, 0.0f}, 7.5f, 1.9f, 1.7f);
    add_target(11, "Aerial Bob (45m)",  {0.0f, 58.0f, 3.8f}, {0.65f, 0.18f, 0.65f}, {0.40f, 0.92f, 0.36f},
               true, {0.8f, 0.0f, 0.45f}, 6.0f, 1.2f, 0.8f);

    // -----------------------------------------------------------------------
    // 4. FPV Yard (X: +20 .. +105, Y: -42 .. +48)
    // -----------------------------------------------------------------------
    // Drone Launch Pad at (38, -6, 0)
    m.add_box({38.0f, -6.0f, 0.06f}, {1.6f, 1.6f, 0.06f}, {0.95f, 0.82f, 0.16f}, 0.5f);
    gym.drone_spawn = {38.0f, -6.0f, 0.14f};

    // 5 Brightly Colored Slalom Gates forming an open racing loop
    add_gate(m, {50.0f,  6.0f, 0.0f}, 3.6f, 3.2f,  0.35f, {1.00f, 0.36f, 0.10f});
    add_gate(m, {68.0f, 18.0f, 0.0f}, 3.8f, 3.4f,  0.85f, {0.14f, 0.84f, 0.46f});
    add_gate(m, {86.0f,  4.0f, 0.0f}, 4.0f, 3.6f,  1.55f, {0.16f, 0.62f, 0.96f});
    add_gate(m, {76.0f, -18.0f, 0.0f}, 3.8f, 3.2f, 2.35f, {0.96f, 0.22f, 0.58f});
    add_gate(m, {54.0f, -24.0f, 0.0f}, 3.6f, 3.2f, 2.90f, {0.98f, 0.76f, 0.14f});

    // Twin Fly-Through Arches (elevated power-loop / split-S practice)
    add_gate(m, {58.0f, 32.0f, 0.0f}, 5.5f, 5.2f, 0.0f, {0.92f, 0.42f, 0.16f});
    add_gate(m, {74.0f, 32.0f, 0.0f}, 5.5f, 5.2f, 0.0f, {0.22f, 0.82f, 0.86f});

    // 18m Tall Dive Tower at (94, 24, 0) with 4 corner pillars + top hoop platform + mid window beams
    const Vec3 tower_col{0.58f, 0.62f, 0.68f};
    const Vec3 accent_col{0.96f, 0.46f, 0.12f};
    const float tx = 94.0f, ty = 24.0f, tw = 4.2f, th = 18.0f;
    m.add_box({tx - tw, ty - tw, th * 0.5f}, {0.45f, 0.45f, th * 0.5f}, tower_col, 1.0f);
    m.add_box({tx + tw, ty - tw, th * 0.5f}, {0.45f, 0.45f, th * 0.5f}, tower_col, 1.0f);
    m.add_box({tx - tw, ty + tw, th * 0.5f}, {0.45f, 0.45f, th * 0.5f}, tower_col, 1.0f);
    m.add_box({tx + tw, ty + tw, th * 0.5f}, {0.45f, 0.45f, th * 0.5f}, tower_col, 1.0f);

    // Mid-level ring beams (at Z=9m) & top ring beams (at Z=18m) leaving the center shaft open to dive through!
    for (float bz : {9.0f, 18.0f}) {
        m.add_box({tx, ty - tw, bz}, {tw + 0.45f, 0.45f, 0.45f}, accent_col, 0.5f);
        m.add_box({tx, ty + tw, bz}, {tw + 0.45f, 0.45f, 0.45f}, accent_col, 0.5f);
        m.add_box({tx - tw, ty, bz}, {0.45f, tw - 0.45f, 0.45f}, accent_col, 0.5f);
        m.add_box({tx + tw, ty, bz}, {0.45f, tw - 0.45f, 0.45f}, accent_col, 0.5f);
    }

    // Elevated Slalom Bridge / Low Canopy for proximity flying
    m.add_box({42.0f, 24.0f, 4.6f}, {5.0f, 3.0f, 0.35f}, {0.48f, 0.52f, 0.58f}, 1.0f);
    m.add_box({37.5f, 21.5f, 2.3f}, {0.4f, 0.4f, 2.3f}, {0.45f, 0.48f, 0.52f}, 0.5f);
    m.add_box({46.5f, 21.5f, 2.3f}, {0.4f, 0.4f, 2.3f}, {0.45f, 0.48f, 0.52f}, 0.5f);
    m.add_box({37.5f, 26.5f, 2.3f}, {0.4f, 0.4f, 2.3f}, {0.45f, 0.48f, 0.52f}, 0.5f);
    m.add_box({46.5f, 26.5f, 2.3f}, {0.4f, 0.4f, 2.3f}, {0.45f, 0.48f, 0.52f}, 0.5f);

    return gym;
}

void GymMap::update_targets(float sim_time_sec, float dt_sec) {
    for (auto& t : targets) {
        if (t.moving) {
            const float s = std::sin(sim_time_sec * t.move_speed + t.move_phase);
            t.current_pos = t.base_pos + t.move_axis * (s * t.move_amplitude);
        } else {
            t.current_pos = t.base_pos;
        }
        if (t.hit_flash_timer > 0.0f) {
            t.hit_flash_timer = std::max(0.0f, t.hit_flash_timer - dt_sec);
        }
    }
}

TeleportPoint GymMap::get_teleport(TeleportId id) {
    switch (id) {
    case TeleportId::MovementWing:
        return {"Movement Wing", {-18.0f, -20.0f, 0.05f}, 0.0f, -0.05f};
    case TeleportId::AimingRange:
        return {"Aiming Range", {0.0f, 14.0f, 0.05f}, 0.0f, -0.02f};
    case TeleportId::FpvYard:
        return {"FPV Yard", {30.0f, -10.0f, 0.05f}, -0.95f, -0.04f};
    case TeleportId::HubSpawn:
    default:
        return {"Hub Spawn", {0.0f, -6.0f, 0.05f}, 0.0f, -0.04f};
    }
}

} // namespace sb
