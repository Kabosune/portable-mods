#define SB_GL_LOADER_IMPLEMENTATION
#include "gl_loader.h"

#include "sandbox_app.h"
#include "ui_hud.h"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

struct CliOptions {
    int         width         = 1280;
    int         height        = 720;
    int         smoke_frames  = 0;
    std::string screenshot_path;
    std::string mode          = "fps";      // fps | fpv
    std::string fpv_view      = "fpv";      // fpv | chase | los
    std::string teleport      = "hub";      // hub | movement | aim | fpv
    std::string fpv_data_dir;
    bool        ui_open       = false;
    bool        debug_ellipse = false;
};

static CliOptions parse_cli(int argc, char** argv) {
    CliOptions o{};
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--width")              o.width = std::max(320, std::atoi(next().c_str()));
        else if (a == "--height")        o.height = std::max(240, std::atoi(next().c_str()));
        else if (a == "--smoke-frames")  o.smoke_frames = std::max(0, std::atoi(next().c_str()));
        else if (a == "--screenshot")    o.screenshot_path = next();
        else if (a == "--mode")          o.mode = next();
        else if (a == "--fpv-view")      o.fpv_view = next();
        else if (a == "--teleport")      o.teleport = next();
        else if (a == "--fpv-data-dir")  o.fpv_data_dir = next();
        else if (a == "--ui-open")       o.ui_open = true;
        else if (a == "--debug-ellipse") o.debug_ellipse = true;
    }
    return o;
}

static void apply_imgui_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 6.0f;
    s.FrameRounding     = 4.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 4.0f;
    s.WindowBorderSize  = 1.0f;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.10f, 0.14f, 0.93f);
    s.Colors[ImGuiCol_Header]   = ImVec4(0.18f, 0.34f, 0.52f, 0.80f);
    s.Colors[ImGuiCol_Button]   = ImVec4(0.20f, 0.38f, 0.58f, 0.85f);
}

int main(int argc, char** argv) {
    const CliOptions cli = parse_cli(argc, argv);

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_SENSOR)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "portable-mods Testing Sandbox",
        cli.width, cli.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(cli.smoke_frames > 0 ? 0 : 1);

    if (!sb_gl_load(reinterpret_cast<void* (*)(const char*)>(SDL_GL_GetProcAddress))) {
        std::fprintf(stderr, "Failed to load OpenGL 3.3 Core functions\n");
        SDL_GL_DestroyContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const char* gl_ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gl_ren = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    SDL_Log("[sandbox] OpenGL: %s | Renderer: %s", gl_ver ? gl_ver : "?", gl_ren ? gl_ren : "?");

    // Dear ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    apply_imgui_style();
    ImGui_ImplSDL3_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    sb::SandboxApp app{};
    if (!app.init(cli.fpv_data_dir)) {
        std::fprintf(stderr, "[sandbox] SandboxApp::init failed\n");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DestroyContext(gl_ctx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    app.init_gl();

    // Apply CLI launch configuration
    if (cli.teleport == "movement")      app.teleport(sb::TeleportId::MovementWing);
    else if (cli.teleport == "aim")      app.teleport(sb::TeleportId::AimingRange);
    else if (cli.teleport == "fpv")      app.teleport(sb::TeleportId::FpvYard);
    else                                 app.teleport(sb::TeleportId::HubSpawn);

    if (cli.fpv_view == "chase")         app.fpv_cam_view = sb::FpvCamView::Chase;
    else if (cli.fpv_view == "los")      app.fpv_cam_view = sb::FpvCamView::Los;
    else                                 app.fpv_cam_view = sb::FpvCamView::Fpv;

    if (cli.mode == "fpv" && app.fpv.is_ready()) {
        app.fpv_mode = true;
        app.fpv.set_active_mode(true, app.fpv_cam_view);
    }
    app.ui_open          = cli.ui_open;
    app.fa_debug_ellipse = cli.debug_ellipse;

    auto sync_mouse_capture = [&]() {
        const bool capture = (cli.smoke_frames == 0) && !app.ui_open && !app.fpv.overlay_wants_input() && !app.fpv_mode;
        SDL_SetWindowRelativeMouseMode(window, capture);
    };
    sync_mouse_capture();
    SDL_StartTextInput(window);

    Uint64 prev_ticks = SDL_GetPerformanceCounter();
    const double tick_freq = static_cast<double>(SDL_GetPerformanceFrequency());
    bool running = true;
    int rendered_frames = 0;
    bool gl_error_seen = false;

    while (running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(static_cast<double>(now - prev_ticks) / tick_freq);
        prev_ticks = now;
        if (cli.smoke_frames > 0) {
            dt = 1.0f / 60.0f;
        }
        dt = std::clamp(dt, 0.0001f, 0.1f);

        app.input.begin_frame();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // Always forward release & window events to Dear ImGui so keys/buttons never stick across panel toggles
            const bool is_release_or_win =
                e.type == SDL_EVENT_KEY_UP ||
                e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST);
            if (app.ui_open || is_release_or_win) {
                ImGui_ImplSDL3_ProcessEvent(&e);
            }

            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }

            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                const SDL_Keycode key = e.key.key;

                // F8 toggles libfpv's built-in overlay; close Tuning Panel so overlay input is never trapped
                if (key == SDLK_F8) {
                    const bool open_now = !app.fpv.overlay_is_open();
                    app.fpv.set_overlay_open(open_now);
                    if (open_now) app.ui_open = false;
                    sync_mouse_capture();
                    continue;
                }

                // Tab or Esc toggles our Tuning Panel (unless libfpv's own overlay is open)
                if (key == SDLK_TAB || key == SDLK_ESCAPE) {
                    if (app.fpv.overlay_is_open()) {
                        app.fpv.set_overlay_open(false);
                    } else {
                        app.ui_open = !app.ui_open;
                        if (app.ui_open) app.fpv.release_all_keys();
                    }
                    sync_mouse_capture();
                    continue;
                }

                if (!app.ui_open && !app.fpv.overlay_wants_input()) {
                    if (key == SDLK_F1) {
                        app.show_hud = !app.show_hud;
                        continue;
                    }
                    if (key == SDLK_F) {
                        app.toggle_fpv_mode();
                        sync_mouse_capture();
                        continue;
                    }
                    if (key == SDLK_R) {
                        if (app.fpv_mode) app.fpv.reset_drone();
                        else app.reset_player();
                        continue;
                    }
                    if (key == SDLK_V && app.fpv_mode) {
                        const int next_v = (static_cast<int>(app.fpv_cam_view) + 1) % 3;
                        app.fpv_cam_view = static_cast<sb::FpvCamView>(next_v);
                        app.fpv.set_active_mode(true, app.fpv_cam_view);
                        continue;
                    }
                }
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
                if (!app.ui_open && !app.fpv_mode && app.ads_toggle_mode) {
                    app.ads_toggled_on = !app.ads_toggled_on;
                }
            }

            // Always invoke handle_sdl_event so KEY_UP releases always clear libfpv held-key state
            app.fpv.handle_sdl_event(e, app.fpv_mode, !app.ui_open);

            const bool capture_now = !app.ui_open && !app.fpv.overlay_wants_input();
            app.input.process_event(e, capture_now);
        }

        sync_mouse_capture();

        const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0 || (cli.smoke_frames > 0);
        app.step_frame(dt, focused);

        int fb_w = 0, fb_h = 0, win_w = 0, win_h = 0;
        SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);
        SDL_GetWindowSize(window, &win_w, &win_h);
        const float disp_scale = SDL_GetWindowDisplayScale(window);
        const float px_dens    = std::max(1.0f, SDL_GetWindowPixelDensity(window));
        const float ui_scale   = disp_scale / px_dens;

        // Render 3D world + viewmodel / FPV video + libfpv OSD/overlay
        app.render(fb_w, fb_h, win_w, win_h, ui_scale, dt);

        // Render Dear ImGui HUD + Tuning Panel on top
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        sb::draw_sandbox_ui(app, win_w, win_h);
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fb_w, fb_h);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
            std::fprintf(stderr, "[sandbox] OpenGL error: 0x%X\n", err);
            gl_error_seen = true;
        }

        rendered_frames++;
        if (cli.smoke_frames > 0 && rendered_frames >= cli.smoke_frames) {
            if (!cli.screenshot_path.empty()) {
                if (sb::Renderer::save_png_screenshot(cli.screenshot_path, fb_w, fb_h)) {
                    SDL_Log("[sandbox] Saved verification screenshot: %s (%dx%d)", cli.screenshot_path.c_str(), fb_w, fb_h);
                } else {
                    std::fprintf(stderr, "[sandbox] Failed to save screenshot: %s\n", cli.screenshot_path.c_str());
                    gl_error_seen = true;
                }
            }
            running = false;
        }

        SDL_GL_SwapWindow(window);
    }

    SDL_StopTextInput(window);
    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return gl_error_seen ? 1 : 0;
}
