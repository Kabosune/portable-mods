#include "physics_world.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace sb {

namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING     = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
} // namespace Layers

namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr JPH::uint            NUM_LAYERS = 2;
} // namespace BroadPhaseLayers

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        map_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        map_[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return map_[layer]; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
    }
#endif
private:
    JPH::BroadPhaseLayer map_[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override {
        if (layer1 == Layers::NON_MOVING) return layer2 == BroadPhaseLayers::MOVING;
        return true;
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l1, JPH::ObjectLayer l2) const override {
        if (l1 == Layers::NON_MOVING) return l2 == Layers::MOVING;
        return true;
    }
};

static std::once_flag g_jolt_once;
static void ensure_jolt_initialized() {
    std::call_once(g_jolt_once, []() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

static inline JPH::Vec3 to_jph(Vec3 w) {
    const Vec3 j = world_to_jolt(w);
    return JPH::Vec3(j.x, j.y, j.z);
}

static inline JPH::RVec3 to_jph_r(Vec3 w) {
    const Vec3 j = world_to_jolt(w);
    return JPH::RVec3(j.x, j.y, j.z);
}

static inline Vec3 from_jph(JPH::Vec3Arg v) {
    return jolt_to_world({v.GetX(), v.GetY(), v.GetZ()});
}

static inline Vec3 from_jph_r(JPH::RVec3Arg v) {
    return jolt_to_world({static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())});
}

struct PhysicsWorld::Impl {
    BPLayerInterfaceImpl              bp_interface;
    ObjectVsBroadPhaseLayerFilterImpl obj_vs_bp_filter;
    ObjectLayerPairFilterImpl         obj_pair_filter;

    std::unique_ptr<JPH::TempAllocatorImpl>     temp_alloc;
    std::unique_ptr<JPH::JobSystemThreadPool>   job_system;
    std::unique_ptr<JPH::PhysicsSystem>         physics_system;
    JPH::BodyID                                 gym_body_id;

    JPH::RefConst<JPH::Shape>                   stand_shape;
    JPH::RefConst<JPH::Shape>                   crouch_shape;
    JPH::Ref<JPH::CharacterVirtual>             character;
    bool                                        is_crouching = false;
    bool                                        initialized  = false;
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() { shutdown(); }

bool PhysicsWorld::init(const MeshData& gym_mesh, Vec3 spawn_feet_pos) {
    shutdown();
    ensure_jolt_initialized();

    impl_->temp_alloc = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    impl_->job_system = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2);
    impl_->physics_system = std::make_unique<JPH::PhysicsSystem>();
    impl_->physics_system->Init(
        1024, 0, 1024, 1024,
        impl_->bp_interface,
        impl_->obj_vs_bp_filter,
        impl_->obj_pair_filter);
    impl_->physics_system->SetGravity(JPH::Vec3(0.0f, -20.0f, 0.0f));

    // Build Jolt static MeshShape from exact Testing Gym triangles (Z-up -> Jolt Y-up, det = +1)
    JPH::VertexList jolt_verts;
    jolt_verts.reserve(gym_mesh.vertices.size());
    for (const auto& v : gym_mesh.vertices) {
        const Vec3 j = world_to_jolt({v.pos[0], v.pos[1], v.pos[2]});
        jolt_verts.emplace_back(j.x, j.y, j.z);
    }

    JPH::IndexedTriangleList jolt_tris;
    jolt_tris.reserve(gym_mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < gym_mesh.indices.size(); i += 3) {
        jolt_tris.emplace_back(gym_mesh.indices[i], gym_mesh.indices[i + 1], gym_mesh.indices[i + 2]);
    }

    JPH::MeshShapeSettings mesh_settings(jolt_verts, jolt_tris);
    JPH::ShapeSettings::ShapeResult mesh_result = mesh_settings.Create();
    if (mesh_result.HasError()) {
        std::fprintf(stderr, "[physics] MeshShape creation failed: %s\n", mesh_result.GetError().c_str());
        return false;
    }

    JPH::BodyInterface& body_if = impl_->physics_system->GetBodyInterface();
    JPH::BodyCreationSettings gym_body_settings(
        mesh_result.Get(),
        JPH::RVec3::sZero(),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::NON_MOVING);
    impl_->gym_body_id = body_if.CreateAndAddBody(gym_body_settings, JPH::EActivation::DontActivate);
    impl_->physics_system->OptimizeBroadPhase();

    // Standing and crouching capsules whose local origin is at the player's feet (y = 0)
    const float stand_cyl_half  = std::max(0.05f, (kStandTotalHeight  - 2.0f * kPlayerRadius) * 0.5f);
    const float crouch_cyl_half = std::max(0.05f, (kCrouchTotalHeight - 2.0f * kPlayerRadius) * 0.5f);

    impl_->stand_shape = JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0.0f, kStandTotalHeight * 0.5f, 0.0f),
        JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(stand_cyl_half, kPlayerRadius)).Create().Get();

    impl_->crouch_shape = JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0.0f, kCrouchTotalHeight * 0.5f, 0.0f),
        JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(crouch_cyl_half, kPlayerRadius)).Create().Get();

    JPH::CharacterVirtualSettings char_settings{};
    char_settings.mMaxSlopeAngle     = 48.0f * kDegToRad; // Supports 20/25/30/35 deg surf ramps cleanly
    char_settings.mMaxStrength       = 100.0f;
    char_settings.mShape             = impl_->stand_shape;
    char_settings.mBackFaceMode      = JPH::EBackFaceMode::CollideWithBackFaces;
    char_settings.mCharacterPadding  = 0.02f;
    char_settings.mPenetrationRecoverySpeed = 1.0f;
    char_settings.mPredictiveContactDistance = 0.1f;
    char_settings.mSupportingVolume  = JPH::Plane(JPH::Vec3::sAxisY(), -kPlayerRadius);

    impl_->character = new JPH::CharacterVirtual(
        &char_settings,
        to_jph_r(spawn_feet_pos),
        JPH::Quat::sIdentity(),
        0,
        impl_->physics_system.get());
    impl_->is_crouching = false;
    impl_->initialized  = true;

    // Initial ground query so spawn state is immediately valid
    JPH::DefaultBroadPhaseLayerFilter bp_filter(impl_->obj_vs_bp_filter, Layers::MOVING);
    JPH::DefaultObjectLayerFilter     obj_filter(impl_->obj_pair_filter, Layers::MOVING);
    JPH::BodyFilter                   body_filter;
    JPH::ShapeFilter                  shape_filter;
    impl_->character->UpdateGroundVelocity();
    impl_->character->RefreshContacts(bp_filter, obj_filter, body_filter, shape_filter, *impl_->temp_alloc);
    return true;
}

void PhysicsWorld::shutdown() {
    if (!impl_) return;
    if (impl_->initialized) {
        impl_->character = nullptr;
        impl_->stand_shape = nullptr;
        impl_->crouch_shape = nullptr;
        if (impl_->physics_system && !impl_->gym_body_id.IsInvalid()) {
            JPH::BodyInterface& body_if = impl_->physics_system->GetBodyInterface();
            body_if.RemoveBody(impl_->gym_body_id);
            body_if.DestroyBody(impl_->gym_body_id);
            impl_->gym_body_id = JPH::BodyID();
        }
        impl_->physics_system.reset();
        impl_->job_system.reset();
        impl_->temp_alloc.reset();
        impl_->initialized = false;
    }
}

void PhysicsWorld::teleport_player(Vec3 feet_pos) {
    if (!impl_->initialized) return;
    impl_->character->SetPosition(to_jph_r(feet_pos));
    impl_->character->SetLinearVelocity(JPH::Vec3::sZero());

    JPH::DefaultBroadPhaseLayerFilter bp_filter(impl_->obj_vs_bp_filter, Layers::MOVING);
    JPH::DefaultObjectLayerFilter     obj_filter(impl_->obj_pair_filter, Layers::MOVING);
    JPH::BodyFilter                   body_filter;
    JPH::ShapeFilter                  shape_filter;
    if (impl_->is_crouching) {
        if (impl_->character->SetShape(impl_->stand_shape, 0.05f, bp_filter, obj_filter, body_filter, shape_filter, *impl_->temp_alloc)) {
            impl_->is_crouching = false;
        }
    }
    impl_->character->RefreshContacts(bp_filter, obj_filter, body_filter, shape_filter, *impl_->temp_alloc);
}

CharacterMoveResult PhysicsWorld::query_character_state() const {
    CharacterMoveResult r{};
    if (!impl_->initialized) return r;

    r.feet_pos   = from_jph_r(impl_->character->GetPosition());
    r.velocity   = from_jph(impl_->character->GetLinearVelocity());
    const auto gs = impl_->character->GetGroundState();
    r.on_ground  = (gs == JPH::CharacterVirtual::EGroundState::OnGround);
    if (gs == JPH::CharacterVirtual::EGroundState::OnGround || gs == JPH::CharacterVirtual::EGroundState::OnSteepGround) {
        const Vec3 n = from_jph(impl_->character->GetGroundNormal());
        r.slope_normal = (length_sq(n) > 1e-6f) ? normalize(n) : Vec3{0.0f, 0.0f, 1.0f};
    } else {
        r.slope_normal = {0.0f, 0.0f, 1.0f};
    }
    r.crouching  = impl_->is_crouching;
    r.eye_height = impl_->is_crouching ? kCrouchEyeHeight : kStandEyeHeight;
    return r;
}

CharacterMoveResult PhysicsWorld::step_character(Vec3 desired_vel_zup, float dt_sec, bool want_crouch, bool jumping_up) {
    if (!impl_->initialized || dt_sec <= 0.0f) {
        return query_character_state();
    }

    JPH::DefaultBroadPhaseLayerFilter bp_filter(impl_->obj_vs_bp_filter, Layers::MOVING);
    JPH::DefaultObjectLayerFilter     obj_filter(impl_->obj_pair_filter, Layers::MOVING);
    JPH::BodyFilter                   body_filter;
    JPH::ShapeFilter                  shape_filter;

    // Crouch / uncrouch transition with overhead clearance verification
    if (want_crouch && !impl_->is_crouching) {
        if (impl_->character->SetShape(impl_->crouch_shape, 0.05f, bp_filter, obj_filter, body_filter, shape_filter, *impl_->temp_alloc)) {
            impl_->is_crouching = true;
        }
    } else if (!want_crouch && impl_->is_crouching) {
        if (impl_->character->SetShape(impl_->stand_shape, 0.05f, bp_filter, obj_filter, body_filter, shape_filter, *impl_->temp_alloc)) {
            impl_->is_crouching = false;
        }
    }

    impl_->character->SetLinearVelocity(to_jph(desired_vel_zup));

    JPH::CharacterVirtual::ExtendedUpdateSettings upd{};
    upd.mWalkStairsStepUp          = JPH::Vec3(0.0f, 0.35f, 0.0f);
    upd.mWalkStairsMinStepForward  = 0.02f;
    upd.mWalkStairsStepForwardTest = 0.15f;
    upd.mStickToFloorStepDown      = (jumping_up || desired_vel_zup.z > 0.25f)
        ? JPH::Vec3::sZero()
        : JPH::Vec3(0.0f, -0.35f, 0.0f);

    // Gravity is already integrated by qm_step; pass zero gravity here to avoid double-gravity.
    impl_->character->ExtendedUpdate(
        dt_sec,
        JPH::Vec3::sZero(),
        upd,
        bp_filter,
        obj_filter,
        body_filter,
        shape_filter,
        *impl_->temp_alloc);

    // Jolt CharacterVirtual::MoveShape does not mutate mLinearVelocity on wall/ceiling contacts.
    // Explicitly clip velocity against collided walls and ceilings (2 passes for corners) so
    // running into walls kills phantom speedometer speed and jumping into low ceilings never glues.
    Vec3 clipped_vel = desired_vel_zup;
    const auto& contacts = impl_->character->GetActiveContacts();
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& c : contacts) {
            if (!c.mHadCollision) continue;
            const Vec3 n = normalize(from_jph(c.mContactNormal));
            const bool is_wall    = std::fabs(n.z) < 0.65f;
            const bool is_ceiling = n.z < -0.5f;
            if (is_wall || is_ceiling) {
                const float vn = dot(clipped_vel, n);
                if (vn < 0.0f) {
                    clipped_vel = clipped_vel - n * vn;
                }
            }
        }
    }

    const auto gs = impl_->character->GetGroundState();
    if (gs == JPH::CharacterVirtual::EGroundState::OnGround && !jumping_up && clipped_vel.z < 0.0f) {
        clipped_vel.z = 0.0f;
    }
    impl_->character->SetLinearVelocity(to_jph(clipped_vel));

    return query_character_state();
}

RaycastHit PhysicsWorld::raycast_world(Vec3 origin, Vec3 dir_unit, float max_dist) const {
    RaycastHit out{};
    if (!impl_->initialized || max_dist <= 0.0f) return out;

    const JPH::RRayCast ray(to_jph_r(origin), to_jph(dir_unit * max_dist));
    JPH::RayCastResult hit{};
    if (impl_->physics_system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
        out.hit = true;
        out.distance = hit.mFraction * max_dist;
        out.point = origin + dir_unit * out.distance;
        out.target_id = -1;

        JPH::BodyLockRead lock(impl_->physics_system->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& body = lock.GetBody();
            const JPH::RVec3 hp = ray.GetPointOnRay(hit.mFraction);
            const JPH::Vec3 n = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hp);
            out.normal = normalize(from_jph(n));
        }
    }
    return out;
}

static bool ray_aabb_intersect(Vec3 orig, Vec3 dir, float max_dist, Vec3 center, Vec3 half_ext,
                               float& out_t, Vec3& out_normal) {
    float tmin = 0.0f;
    float tmax = max_dist;
    int hit_axis = -1;
    float hit_sign = 0.0f;

    for (int i = 0; i < 3; ++i) {
        const float bmin = center[i] - half_ext[i];
        const float bmax = center[i] + half_ext[i];
        if (std::fabs(dir[i]) < 1e-6f) {
            if (orig[i] < bmin || orig[i] > bmax) return false;
        } else {
            const float inv_d = 1.0f / dir[i];
            float t1 = (bmin - orig[i]) * inv_d;
            float t2 = (bmax - orig[i]) * inv_d;
            float sign = -1.0f;
            if (t1 > t2) {
                std::swap(t1, t2);
                sign = 1.0f;
            }
            if (t1 > tmin) {
                tmin = t1;
                hit_axis = i;
                hit_sign = sign;
            }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }

    if (tmin < 0.0f || tmin > max_dist) return false;
    out_t = tmin;
    out_normal = {0.0f, 0.0f, 0.0f};
    if (hit_axis >= 0) out_normal[hit_axis] = hit_sign;
    else out_normal = -dir;
    return true;
}

RaycastHit PhysicsWorld::raycast_hitscan(Vec3 origin, Vec3 dir_unit, float max_dist,
                                         const std::vector<RangeTarget>& targets) const {
    RaycastHit best = raycast_world(origin, dir_unit, max_dist);
    float best_dist = best.hit ? best.distance : max_dist;

    for (const auto& t : targets) {
        float hit_t = 0.0f;
        Vec3 hit_n{};
        if (ray_aabb_intersect(origin, dir_unit, best_dist, t.current_pos, t.half_extents, hit_t, hit_n)) {
            best_dist = hit_t;
            best.hit = true;
            best.distance = hit_t;
            best.point = origin + dir_unit * hit_t;
            best.normal = hit_n;
            best.target_id = t.id;
        }
    }
    return best;
}

bool PhysicsWorld::probe_wall(Vec3 player_feet_pos, Vec3 probe_dir, float max_dist, qm_wall_probe_result_t* out_hit) const {
    if (!impl_->initialized || !out_hit) return false;
    out_hit->hit = 0;

    const float dir_len = length(probe_dir);
    if (dir_len < 1e-5f) return false;
    const Vec3 dir = probe_dir / dir_len;

    // Probe from torso height so walls at chest level trigger wall-bounce cleanly
    const Vec3 origin = {player_feet_pos.x, player_feet_pos.y, player_feet_pos.z + 0.95f};
    const RaycastHit rh = raycast_world(origin, dir, max_dist + kPlayerRadius);
    if (!rh.hit) return false;

    // Reject floors and ceilings; require a mostly vertical wall surface
    if (std::fabs(rh.normal.z) > 0.35f) return false;

    out_hit->hit = 1;
    out_hit->normal[0] = rh.normal.x;
    out_hit->normal[1] = rh.normal.y;
    out_hit->normal[2] = rh.normal.z;
    out_hit->distance_m = std::max(0.0f, rh.distance - kPlayerRadius);
    return true;
}

int PhysicsWorld::qm_wall_probe_callback(
    const float pos[3],
    const float probe_dir[3],
    float max_dist_m,
    qm_wall_probe_result_t* out_hit,
    void* user_ctx) {
    auto* self = static_cast<const PhysicsWorld*>(user_ctx);
    if (!self || !pos || !probe_dir || !out_hit) return 0;
    return self->probe_wall(
        {pos[0], pos[1], pos[2]},
        {probe_dir[0], probe_dir[1], probe_dir[2]},
        max_dist_m,
        out_hit) ? 1 : 0;
}

} // namespace sb
