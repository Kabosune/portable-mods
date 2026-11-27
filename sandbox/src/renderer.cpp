#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sb {

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[renderer] Shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[renderer] Program link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static const char* kSceneVs = R"(#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_nrm;
layout(location=2) in vec3 a_col;
layout(location=3) in float a_grid;

uniform mat4 u_viewProj;
uniform mat4 u_model;

out vec3 v_world;
out vec3 v_nrm;
out vec3 v_col;
out float v_grid;

void main() {
    vec4 w = u_model * vec4(a_pos, 1.0);
    v_world = w.xyz;
    v_nrm   = mat3(u_model) * a_nrm;
    v_col   = a_col;
    v_grid  = a_grid;
    gl_Position = u_viewProj * w;
}
)";

static const char* kSceneFs = R"(#version 330 core
in vec3 v_world;
in vec3 v_nrm;
in vec3 v_col;
in float v_grid;

uniform vec3  u_eye;
uniform vec3  u_tint;
uniform float u_emissive;
uniform float u_fog_scale;

out vec4 o_color;

void main() {
    vec3 n = normalize(v_nrm);
    vec3 sun_dir = normalize(vec3(0.45, 0.35, 0.85));
    float ndl = max(dot(n, sun_dir), 0.0);
    float hemi = 0.5 + 0.5 * n.z;
    vec3 lit = v_col * (0.32 + 0.24 * hemi + 0.58 * ndl);

    // Tri-planar procedural meter checkerboard & grid line shading (Z-up meters)
    if (v_grid > 0.01) {
        vec3 an = abs(n);
        vec2 uv = (an.z >= an.x && an.z >= an.y) ? v_world.xy :
                  (an.x >= an.y)                 ? v_world.yz : v_world.xz;
        vec2 cell = floor(uv * v_grid);
        float chk = mod(cell.x + cell.y, 2.0);
        vec2 f = fract(uv * v_grid);
        float edge = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
        float line = 1.0 - smoothstep(0.015, 0.035, edge);
        lit *= mix(0.88, 1.04, chk);
        lit = mix(lit, lit * 0.68, line * 0.55);
    }

    vec3 col = mix(lit, v_col, u_emissive) * u_tint;
    float dist = length(v_world - u_eye);
    float fog = clamp(dist * u_fog_scale, 0.0, 1.0);
    vec3 sky = vec3(0.56, 0.70, 0.84);
    o_color = vec4(mix(col, sky, fog * fog), 1.0);
}
)";

static const char* kLineVs = R"(#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec4 a_col;
uniform mat4 u_viewProj;
out vec4 v_col;
void main() {
    v_col = a_col;
    gl_Position = u_viewProj * vec4(a_pos, 1.0);
}
)";

static const char* kLineFs = R"(#version 330 core
in vec4 v_col;
out vec4 o_color;
void main() {
    o_color = v_col;
}
)";

static const char* kBlitVs = R"(#version 330 core
out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const char* kBlitFs = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 o_color;
void main() {
    o_color = texture(u_tex, v_uv);
}
)";

void Renderer::GpuMesh::upload(const MeshData& data) {
    destroy();
    if (data.vertices.empty() || data.indices.empty()) return;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(GymVertex)),
                 data.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                 data.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GymVertex), reinterpret_cast<void*>(offsetof(GymVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GymVertex), reinterpret_cast<void*>(offsetof(GymVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GymVertex), reinterpret_cast<void*>(offsetof(GymVertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GymVertex), reinterpret_cast<void*>(offsetof(GymVertex, grid_scale)));

    glBindVertexArray(0);
    index_count = static_cast<uint32_t>(data.indices.size());
}

void Renderer::GpuMesh::draw() const {
    if (!vao || index_count == 0) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count), GL_UNSIGNED_INT, nullptr);
}

void Renderer::GpuMesh::destroy() {
    if (ebo) glDeleteBuffers(1, &ebo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = ebo = 0;
    index_count = 0;
}

Renderer::~Renderer() {
    shutdown();
}

static MeshData build_weapon_viewmodel_mesh() {
    // Built in camera space (+X right, +Y up, -Z forward)
    MeshData m{};
    const Vec3 gun_dark{0.16f, 0.18f, 0.21f};
    const Vec3 gun_mid{0.28f, 0.31f, 0.36f};
    const Vec3 accent{0.92f, 0.48f, 0.14f};
    const Vec3 sight_glow{0.18f, 0.96f, 0.56f};

    // Receiver body
    m.add_box({0.0f, -0.02f, -0.26f}, {0.028f, 0.038f, 0.14f}, gun_dark);
    // Upper slide / rail
    m.add_box({0.0f,  0.024f, -0.27f}, {0.024f, 0.014f, 0.15f}, gun_mid);
    // Barrel shroud + muzzle tip
    m.add_box({0.0f,  0.012f, -0.48f}, {0.014f, 0.014f, 0.09f}, gun_dark);
    m.add_box({0.0f,  0.012f, -0.58f}, {0.017f, 0.017f, 0.018f}, accent);
    // Magazine / grip
    m.add_box({0.0f, -0.085f, -0.18f}, {0.020f, 0.052f, 0.032f}, gun_mid);
    // Iron sights (rear notch + glowing front post aligned with Y=0.048)
    m.add_box({-0.015f, 0.046f, -0.14f}, {0.005f, 0.011f, 0.010f}, gun_dark);
    m.add_box({ 0.015f, 0.046f, -0.14f}, {0.005f, 0.011f, 0.010f}, gun_dark);
    m.add_box({ 0.000f, 0.049f, -0.54f}, {0.004f, 0.012f, 0.006f}, sight_glow);
    return m;
}

static MeshData build_drone_body_mesh() {
    // Z-up quadcopter frame (+X right, +Y forward, +Z up)
    MeshData m{};
    const Vec3 carbon{0.12f, 0.13f, 0.16f};
    const Vec3 canopy{0.92f, 0.24f, 0.16f};
    const Vec3 arm_col{0.20f, 0.22f, 0.26f};

    m.add_box({0.0f, 0.0f, 0.015f}, {0.045f, 0.075f, 0.018f}, carbon);
    m.add_box({0.0f, 0.055f, 0.038f}, {0.022f, 0.022f, 0.018f}, canopy); // FPV camera front (+Y)
    // X-arms (45 deg diagonals)
    m.add_box({0.0f, 0.0f, 0.008f}, {0.14f, 0.010f, 0.005f}, arm_col, 0.0f,  0.785398f);
    m.add_box({0.0f, 0.0f, 0.008f}, {0.14f, 0.010f, 0.005f}, arm_col, 0.0f, -0.785398f);
    return m;
}

static MeshData build_drone_prop_mesh() {
    // 3-blade prop spinning around local +Z
    MeshData m{};
    const Vec3 col{0.18f, 0.78f, 1.0f};
    for (int b = 0; b < 3; ++b) {
        const float a = static_cast<float>(b) * (2.0f * kPi / 3.0f);
        const Vec3 hub{0.0f, 0.0f, 0.024f};
        const Vec3 tip{std::cos(a) * 0.064f, std::sin(a) * 0.064f, 0.024f};
        const Vec3 side{-std::sin(a) * 0.009f, std::cos(a) * 0.009f, 0.0f};
        m.add_tri(hub, tip + side, tip - side, col);
        m.add_tri(hub, tip - side, tip + side, col);
    }
    return m;
}

bool Renderer::init(const MeshData& gym_mesh) {
    scene_prog_ = link_program(kSceneVs, kSceneFs);
    line_prog_  = link_program(kLineVs, kLineFs);
    blit_prog_  = link_program(kBlitVs, kBlitFs);
    if (!scene_prog_ || !line_prog_ || !blit_prog_) return false;

    glGenVertexArrays(1, &empty_vao_);

    // Dynamic line buffer for hitscan tracers
    glGenVertexArrays(1, &line_vao_);
    glGenBuffers(1, &line_vbo_);
    glBindVertexArray(line_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    gym_mesh_.upload(gym_mesh);

    MeshData box_data{};
    box_data.add_box({0, 0, 0}, {1, 1, 1}, {1, 1, 1}, 0.0f);
    unit_box_mesh_.upload(box_data);

    weapon_mesh_.upload(build_weapon_viewmodel_mesh());
    drone_body_mesh_.upload(build_drone_body_mesh());
    drone_prop_mesh_.upload(build_drone_prop_mesh());
    return true;
}

void Renderer::shutdown() {
    gym_mesh_.destroy();
    unit_box_mesh_.destroy();
    weapon_mesh_.destroy();
    drone_body_mesh_.destroy();
    drone_prop_mesh_.destroy();

    if (fbo_)       glDeleteFramebuffers(1, &fbo_);
    if (fbo_tex_)   glDeleteTextures(1, &fbo_tex_);
    if (fbo_depth_) glDeleteRenderbuffers(1, &fbo_depth_);
    fbo_ = fbo_tex_ = fbo_depth_ = 0;
    fbo_w_ = fbo_h_ = 0;

    if (line_vbo_)  glDeleteBuffers(1, &line_vbo_);
    if (line_vao_)  glDeleteVertexArrays(1, &line_vao_);
    if (empty_vao_) glDeleteVertexArrays(1, &empty_vao_);
    if (scene_prog_) glDeleteProgram(scene_prog_);
    if (line_prog_)  glDeleteProgram(line_prog_);
    if (blit_prog_)  glDeleteProgram(blit_prog_);
    line_vbo_ = line_vao_ = empty_vao_ = scene_prog_ = line_prog_ = blit_prog_ = 0;
}

void Renderer::ensure_fbo(int width, int height) {
    width  = std::max(1, width);
    height = std::max(1, height);
    if (fbo_ && width == fbo_w_ && height == fbo_h_) return;

    if (fbo_)       glDeleteFramebuffers(1, &fbo_);
    if (fbo_tex_)   glDeleteTextures(1, &fbo_tex_);
    if (fbo_depth_) glDeleteRenderbuffers(1, &fbo_depth_);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &fbo_tex_);
    glBindTexture(GL_TEXTURE_2D, fbo_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex_, 0);

    glGenRenderbuffers(1, &fbo_depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo_depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo_depth_);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    fbo_w_ = width;
    fbo_h_ = height;
}

void Renderer::render_world(
    const Mat4& view,
    const Mat4& proj,
    Vec3 eye_pos,
    const std::vector<RangeTarget>& targets,
    const std::vector<TracerBeam>& tracers,
    const DroneRenderParams& drone,
    int vp_w,
    int vp_h) {
    glViewport(0, 0, vp_w, vp_h);
    glClearColor(0.56f, 0.70f, 0.84f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const Mat4 vp = mat4_mul(proj, view);
    glUseProgram(scene_prog_);
    glUniformMatrix4fv(glGetUniformLocation(scene_prog_, "u_viewProj"), 1, GL_FALSE, vp.m);
    glUniform3f(glGetUniformLocation(scene_prog_, "u_eye"), eye_pos.x, eye_pos.y, eye_pos.z);
    glUniform1f(glGetUniformLocation(scene_prog_, "u_fog_scale"), 1.0f / 340.0f);

    const GLint loc_model = glGetUniformLocation(scene_prog_, "u_model");
    const GLint loc_tint  = glGetUniformLocation(scene_prog_, "u_tint");
    const GLint loc_emis  = glGetUniformLocation(scene_prog_, "u_emissive");

    // 1. Static Testing Gym
    const Mat4 ident = mat4_identity();
    glUniformMatrix4fv(loc_model, 1, GL_FALSE, ident.m);
    glUniform3f(loc_tint, 1.0f, 1.0f, 1.0f);
    glUniform1f(loc_emis, 0.0f);
    gym_mesh_.draw();

    // 2. Dynamic Aiming Range Targets (highlight bright gold/white on hit)
    for (const auto& t : targets) {
        const Mat4 m = mat4_mul(mat4_translate(t.current_pos), mat4_scale(t.half_extents));
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, m.m);
        const float flash = std::clamp(t.hit_flash_timer / 0.22f, 0.0f, 1.0f);
        const Vec3 c = t.color * (1.0f - flash) + Vec3{1.0f, 0.96f, 0.45f} * flash;
        glUniform3f(loc_tint, c.x, c.y, c.z);
        glUniform1f(loc_emis, flash * 0.85f);
        unit_box_mesh_.draw();
    }

    // 3. Drone visual mesh (in FPS mode, Chase cam, or LOS cam)
    if (drone.visible) {
        const Mat4 dm = mat4_mul(mat4_translate(drone.position), mat4_from_quat(drone.rotation_quat));
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, dm.m);
        glUniform3f(loc_tint, 1.0f, 1.0f, 1.0f);
        glUniform1f(loc_emis, 0.0f);
        drone_body_mesh_.draw();

        for (int i = 0; i < 4; ++i) {
            if (drone.prop_broken[i]) continue;
            const Mat4 pm = mat4_mul(dm, mat4_mul(mat4_translate(drone.motor_pos[i]), mat4_rot_z(drone.prop_angle_rad[i])));
            glUniformMatrix4fv(loc_model, 1, GL_FALSE, pm.m);
            drone_prop_mesh_.draw();
        }
    }

    // 4. Hitscan Tracer Beams
    if (!tracers.empty()) {
        std::vector<float> line_data;
        line_data.reserve(tracers.size() * 14);
        for (const auto& b : tracers) {
            const float a = std::clamp(b.time_left / std::max(0.01f, b.max_time), 0.0f, 1.0f);
            const float r = b.hit_target ? 1.0f : 0.95f;
            const float g = b.hit_target ? 0.92f : 0.72f;
            const float bl = b.hit_target ? 0.25f : 0.30f;
            const float v0[7] = {b.start.x, b.start.y, b.start.z, r, g, bl, a * 0.45f};
            const float v1[7] = {b.end.x,   b.end.y,   b.end.z,   r, g, bl, a};
            line_data.insert(line_data.end(), v0, v0 + 7);
            line_data.insert(line_data.end(), v1, v1 + 7);
        }
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(line_prog_);
        glUniformMatrix4fv(glGetUniformLocation(line_prog_, "u_viewProj"), 1, GL_FALSE, vp.m);
        glBindVertexArray(line_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(line_data.size() * sizeof(float)),
                     line_data.data(), GL_STREAM_DRAW);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(tracers.size() * 2));
        glDisable(GL_BLEND);
    }
}

void Renderer::render_viewmodel(const ViewmodelRenderParams& vm, int vp_w, int vp_h) {
    glViewport(0, 0, vp_w, vp_h);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    const float aspect = static_cast<float>(vp_w) / static_cast<float>(std::max(1, vp_h));
    const Mat4 proj = mat4_perspective(vm.viewmodel_fovy_deg, aspect, 0.03f, 5.0f);

    // Hip resting position in camera space (+X right, +Y up, -Z forward); ADS centers barrel
    const Vec3 hip_base{0.19f, -0.17f, -0.05f};
    const Vec3 pos = {
        hip_base.x + vm.pos_offset_cam.x,
        hip_base.y + vm.pos_offset_cam.y,
        hip_base.z + vm.pos_offset_cam.z + vm.recoil_kick * 0.045f,
    };

    // Apply free-aim yaw (-Y rotation in OpenGL camera space turns right) and pitch (+X rotation pitches up)
    const Mat4 rot_y = mat4_rot_y(-vm.rot_yaw_deg * kDegToRad);
    const Mat4 rot_x = mat4_rot_x((vm.rot_pitch_deg + vm.recoil_kick * 3.5f) * kDegToRad);
    const Mat4 model = mat4_mul(mat4_translate(pos), mat4_mul(rot_y, rot_x));

    glUseProgram(scene_prog_);
    glUniformMatrix4fv(glGetUniformLocation(scene_prog_, "u_viewProj"), 1, GL_FALSE, proj.m);
    glUniformMatrix4fv(glGetUniformLocation(scene_prog_, "u_model"), 1, GL_FALSE, model.m);
    glUniform3f(glGetUniformLocation(scene_prog_, "u_eye"), 0.0f, 0.0f, 0.0f);
    glUniform3f(glGetUniformLocation(scene_prog_, "u_tint"), 1.0f, 1.0f, 1.0f);
    glUniform1f(glGetUniformLocation(scene_prog_, "u_emissive"), 0.0f);
    glUniform1f(glGetUniformLocation(scene_prog_, "u_fog_scale"), 0.0f);

    weapon_mesh_.draw();
}

void Renderer::blit_fbo_to_screen(int fb_w, int fb_h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fb_w, fb_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(blit_prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fbo_tex_);
    glUniform1i(glGetUniformLocation(blit_prog_, "u_tex"), 0);
    glBindVertexArray(empty_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ---------------------------------------------------------------------------
// Minimal self-contained PNG writer (uncompressed DEFLATE stored blocks)
// ---------------------------------------------------------------------------
static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

static void write_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >>  8) & 0xFFu));
    out.push_back(static_cast<uint8_t>( v        & 0xFFu));
}

static void write_png_chunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t len) {
    write_be32(out, static_cast<uint32_t>(len));
    const size_t type_start = out.size();
    out.insert(out.end(), type, type + 4);
    if (len > 0) out.insert(out.end(), data, data + len);
    const uint32_t crc = crc32_update(0, out.data() + type_start, 4 + len);
    write_be32(out, crc);
}

bool Renderer::save_png_screenshot(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;

    std::vector<uint8_t> rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

    // Build filtered scanlines (top-to-bottom, filter byte 0 = None)
    const size_t row_bytes = static_cast<size_t>(width) * 3;
    std::vector<uint8_t> raw;
    raw.reserve((row_bytes + 1) * static_cast<size_t>(height));
    for (int y = height - 1; y >= 0; --y) {
        raw.push_back(0);
        const uint8_t* row = rgb.data() + static_cast<size_t>(y) * row_bytes;
        raw.insert(raw.end(), row, row + row_bytes);
    }

    // Wrap raw scanlines in zlib stream using DEFLATE stored blocks (max 65535 bytes/block)
    std::vector<uint8_t> zlib_data;
    zlib_data.reserve(raw.size() + (raw.size() / 65535 + 1) * 5 + 6);
    zlib_data.push_back(0x78); // CMF
    zlib_data.push_back(0x01); // FLG (check bits valid, no dict)

    uint32_t s1 = 1, s2 = 0;
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t chunk = std::min<size_t>(65535, raw.size() - offset);
        const bool last = (offset + chunk == raw.size());
        zlib_data.push_back(last ? 0x01 : 0x00);
        const uint16_t len16 = static_cast<uint16_t>(chunk);
        const uint16_t nlen16 = static_cast<uint16_t>(~len16);
        zlib_data.push_back(static_cast<uint8_t>(len16 & 0xFFu));
        zlib_data.push_back(static_cast<uint8_t>((len16 >> 8) & 0xFFu));
        zlib_data.push_back(static_cast<uint8_t>(nlen16 & 0xFFu));
        zlib_data.push_back(static_cast<uint8_t>((nlen16 >> 8) & 0xFFu));
        zlib_data.insert(zlib_data.end(), raw.begin() + static_cast<ptrdiff_t>(offset),
                         raw.begin() + static_cast<ptrdiff_t>(offset + chunk));
        for (size_t i = 0; i < chunk; ++i) {
            s1 = (s1 + raw[offset + i]) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        offset += chunk;
    }
    write_be32(zlib_data, (s2 << 16) | s1);

    std::vector<uint8_t> png;
    static const uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), kSig, kSig + 8);

    std::vector<uint8_t> ihdr;
    write_be32(ihdr, static_cast<uint32_t>(width));
    write_be32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(2); // color type: RGB
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    write_png_chunk(png, "IHDR", ihdr.data(), ihdr.size());
    write_png_chunk(png, "IDAT", zlib_data.data(), zlib_data.size());
    write_png_chunk(png, "IEND", nullptr, 0);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t written = std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    return written == png.size();
}

} // namespace sb
