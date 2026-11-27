#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sb {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float  operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3  cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}
inline float length_sq(Vec3 a) { return dot(a, a); }
inline float length(Vec3 a) { return std::sqrt(length_sq(a)); }
inline Vec3  normalize(Vec3 a) {
    const float l = length(a);
    return l > 1e-6f ? (a / l) : Vec3{0.0f, 0.0f, 1.0f};
}

// Column-major 4x4 matrix matching OpenGL conventions
struct Mat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
};

inline Mat4 mat4_identity() { return Mat4{}; }

inline Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

inline Mat4 mat4_translate(Vec3 t) {
    Mat4 r = mat4_identity();
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

inline Mat4 mat4_scale(Vec3 s) {
    Mat4 r{};
    r.m[0] = s.x;
    r.m[5] = s.y;
    r.m[10] = s.z;
    r.m[15] = 1.0f;
    return r;
}

inline Mat4 mat4_rot_x(float rad) {
    Mat4 r = mat4_identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[5] = c;  r.m[9]  = -s;
    r.m[6] = s;  r.m[10] =  c;
    return r;
}

inline Mat4 mat4_rot_y(float rad) {
    Mat4 r = mat4_identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[0] =  c; r.m[8]  = s;
    r.m[2] = -s; r.m[10] = c;
    return r;
}

inline Mat4 mat4_rot_z(float rad) {
    Mat4 r = mat4_identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[0] = c; r.m[4] = -s;
    r.m[1] = s; r.m[5] =  c;
    return r;
}

// Quaternion {x, y, z, w} -> 4x4 rotation matrix
inline Mat4 mat4_from_quat(const float q[4]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 r = mat4_identity();
    r.m[0] = 1.0f - 2.0f * (y * y + z * z);
    r.m[1] = 2.0f * (x * y + z * w);
    r.m[2] = 2.0f * (x * z - y * w);

    r.m[4] = 2.0f * (x * y - z * w);
    r.m[5] = 1.0f - 2.0f * (x * x + z * z);
    r.m[6] = 2.0f * (y * z + x * w);

    r.m[8]  = 2.0f * (x * z + y * w);
    r.m[9]  = 2.0f * (y * z - x * w);
    r.m[10] = 1.0f - 2.0f * (x * x + y * y);
    return r;
}

inline Mat4 mat4_perspective(float fovy_deg, float aspect, float near_z, float far_z) {
    const float f = 1.0f / std::tan(fovy_deg * 0.5f * kDegToRad);
    Mat4 r{};
    r.m[0]  = f / std::max(aspect, 0.01f);
    r.m[5]  = f;
    r.m[10] = (far_z + near_z) / (near_z - far_z);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * far_z * near_z) / (near_z - far_z);
    r.m[15] = 0.0f;
    return r;
}

// Build OpenGL view matrix from orthonormal right-handed world basis (right, up, forward)
inline Mat4 mat4_view_from_basis(Vec3 eye, Vec3 right, Vec3 up, Vec3 forward) {
    Mat4 v = mat4_identity();
    v.m[0] =  right.x;    v.m[4] =  right.y;    v.m[8]  =  right.z;
    v.m[1] =  up.x;       v.m[5] =  up.y;       v.m[9]  =  up.z;
    v.m[2] = -forward.x;  v.m[6] = -forward.y;  v.m[10] = -forward.z;
    v.m[12] = -dot(right, eye);
    v.m[13] = -dot(up, eye);
    v.m[14] =  dot(forward, eye);
    return v;
}

// Canonical Z-up camera basis matching qm_physics.h:
// yaw = 0 faces +Y; +yaw rotates CCW around +Z (left); +pitch looks up.
struct CameraBasis {
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

inline CameraBasis camera_basis_from_yaw_pitch(float yaw_rad, float pitch_rad) {
    const float cy = std::cos(yaw_rad);
    const float sy = std::sin(yaw_rad);
    const float cp = std::cos(pitch_rad);
    const float sp = std::sin(pitch_rad);

    CameraBasis b{};
    b.forward = {-sy * cp, cy * cp, sp};
    b.right   = {cy, sy, 0.0f};
    b.up      = normalize(cross(b.right, b.forward));
    return b;
}

// Determinant +1 right-handed boundary conversion between canonical Z-up world and Jolt Y-up space:
//   world (+X right, +Y forward, +Z up) <-> Jolt (+X right, +Y up, -Z forward)
inline Vec3 world_to_jolt(Vec3 w) { return {w.x, w.z, -w.y}; }
inline Vec3 jolt_to_world(Vec3 j) { return {j.x, -j.z, j.y}; }

} // namespace sb
