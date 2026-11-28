#pragma once

#include "gl_loader.h"
#include "gym_map.h"
#include "math_types.h"

#include <string>
#include <vector>

namespace sb {

struct TracerBeam {
    Vec3  start;
    Vec3  end;
    bool  hit_target = false;
    float time_left  = 0.0f;
    float max_time   = 0.14f;
};

struct ViewmodelRenderParams {
    Vec3  pos_offset_cam{}; // Camera-space offset (+X right, +Y up, -Z forward) from fa_compute_viewmodel_offset
    float rot_yaw_deg        = 0.0f;
    float rot_pitch_deg      = 0.0f;
    float ads_amount         = 0.0f;
    float recoil_kick        = 0.0f;
    float recoil_roll_deg    = 0.0f;
    float muzzle_flash       = 0.0f; // 0..1 brief flash intensity
    float muzzle_flash_angle = 0.0f; // radians around barrel axis
    float viewmodel_fovy_deg = 54.0f;
};

struct DroneRenderParams {
    bool  visible = false;
    Vec3  position{};
    float rotation_quat[4] = {0, 0, 0, 1};
    Vec3  motor_pos[4]{};
    float prop_angle_rad[4] = {0, 0, 0, 0};
    bool  prop_broken[4]    = {false, false, false, false};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool init(const MeshData& gym_mesh);
    void shutdown();

    void ensure_fbo(int width, int height);
    GLuint fbo_handle() const { return fbo_; }
    GLuint fbo_color_tex() const { return fbo_tex_; }
    int    fbo_width() const { return fbo_w_; }
    int    fbo_height() const { return fbo_h_; }

    // Render 3D gym, dynamic targets, tracers, and optional drone mesh to currently bound framebuffer
    void render_world(
        const Mat4& view,
        const Mat4& proj,
        Vec3 eye_pos,
        const std::vector<RangeTarget>& targets,
        const std::vector<TracerBeam>& tracers,
        const DroneRenderParams& drone,
        int vp_w,
        int vp_h);

    // Separate depth-cleared first-person weapon viewmodel pass with independent viewmodel FOV
    void render_viewmodel(const ViewmodelRenderParams& vm, int vp_w, int vp_h);

    // Blit offscreen FBO to default framebuffer (used when FPV video shader is not active)
    void blit_fbo_to_screen(int fb_w, int fb_h);

    // Save current front/back framebuffer as an uncompressed PNG file (zero external deps)
    static bool save_png_screenshot(const std::string& path, int width, int height);

private:
    struct GpuMesh {
        GLuint   vao = 0;
        GLuint   vbo = 0;
        GLuint   ebo = 0;
        uint32_t index_count = 0;

        void upload(const MeshData& data);
        void draw() const;
        void destroy();
    };

    GLuint scene_prog_ = 0;
    GLuint line_prog_  = 0;
    GLuint blit_prog_  = 0;
    GLuint empty_vao_  = 0;

    GLuint line_vao_   = 0;
    GLuint line_vbo_   = 0;

    GLuint fbo_        = 0;
    GLuint fbo_tex_    = 0;
    GLuint fbo_depth_  = 0;
    int    fbo_w_      = 0;
    int    fbo_h_      = 0;

    GpuMesh gym_mesh_;
    GpuMesh unit_box_mesh_;
    GpuMesh weapon_mesh_;
    GpuMesh muzzle_flash_mesh_;
    GpuMesh drone_body_mesh_;
    GpuMesh drone_prop_mesh_;
};

} // namespace sb
