cmake_minimum_required(VERSION 3.24)
project(portable_mods_sandbox LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
include(CTest)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(MODS_DIR "${REPO_ROOT}/mods")
set(LIBFPV_DIR "${MODS_DIR}/fpv-drone/vendor/libfpv")
set(SDL3_SRC_DIR "${LIBFPV_DIR}/third_party/SDL")
set(IMGUI_SRC_DIR "${LIBFPV_DIR}/third_party/imgui")

if(NOT EXISTS "${LIBFPV_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "libfpv submodule not initialized. Run: git submodule update --init && git -C mods/fpv-drone/vendor/libfpv submodule update --init")
endif()

# ---------------------------------------------------------------------------
# 1. Single SDL3 static build (shared by sandbox; libfpv built with WITH_SDL=OFF)
# ---------------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
set(SDL_SENSOR ON CACHE BOOL "" FORCE)
add_subdirectory("${SDL3_SRC_DIR}" "${CMAKE_BINARY_DIR}/deps/sdl3" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# 2. libfpv (direct from ../mods/fpv-drone/vendor/libfpv, no duplicate SDL3)
# ---------------------------------------------------------------------------
set(LIBFPV_WITH_SDL OFF CACHE BOOL "" FORCE)
set(LIBFPV_WITH_OVERLAY ON CACHE BOOL "" FORCE)
set(LIBFPV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBFPV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${LIBFPV_DIR}" "${CMAKE_BINARY_DIR}/deps/libfpv" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# 3. Jolt Physics (FetchContent)
# ---------------------------------------------------------------------------
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.2.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(JoltPhysics)

# ---------------------------------------------------------------------------
# 4. Dear ImGui (from libfpv's third_party/imgui + SDL3 & OpenGL3 backends)
# ---------------------------------------------------------------------------
add_library(sandbox_imgui STATIC
  "${IMGUI_SRC_DIR}/imgui.cpp"
  "${IMGUI_SRC_DIR}/imgui_draw.cpp"
  "${IMGUI_SRC_DIR}/imgui_tables.cpp"
  "${IMGUI_SRC_DIR}/imgui_widgets.cpp"
  "${IMGUI_SRC_DIR}/backends/imgui_impl_sdl3.cpp"
  "${IMGUI_SRC_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(sandbox_imgui PUBLIC
  "${IMGUI_SRC_DIR}"
  "${IMGUI_SRC_DIR}/backends"
)
target_link_libraries(sandbox_imgui PUBLIC SDL3::SDL3-static)
if(WIN32)
  target_link_libraries(sandbox_imgui PUBLIC opengl32)
endif()

# ---------------------------------------------------------------------------
# 5. Direct-linked pure-C11 mod cores (zero copied code)
# ---------------------------------------------------------------------------
add_library(portable_mod_cores STATIC
  "${MODS_DIR}/quake-movement/core/qm_physics.c"
  "${MODS_DIR}/weighty-aim/core/weighty_aim.c"
  "${MODS_DIR}/free-aim/core/free_aim.c"
)
target_include_directories(portable_mod_cores PUBLIC
  "${MODS_DIR}/quake-movement/core"
  "${MODS_DIR}/weighty-aim/core"
  "${MODS_DIR}/free-aim/core"
)
if(NOT MSVC)
  target_compile_options(portable_mod_cores PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 6. Headless simulation core (Gym + Jolt Physics + Mods)
# ---------------------------------------------------------------------------
add_library(sandbox_sim STATIC
  src/gym_map.cpp
  src/physics_world.cpp
  src/weapon_audio.cpp
  src/sandbox_settings.cpp
)
target_include_directories(sandbox_sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(sandbox_sim PUBLIC
  portable_mod_cores
  Jolt
)
if(NOT MSVC)
  target_compile_options(sandbox_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# 7. Main Testing Sandbox Executable
# ---------------------------------------------------------------------------
add_executable(portable_mods_sandbox
  src/main.cpp
  src/input_manager.cpp
  src/renderer.cpp
  src/fpv_bridge.cpp
  src/sandbox_app.cpp
  src/ui_hud.cpp
)
target_include_directories(portable_mods_sandbox PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${LIBFPV_DIR}/docs/renderers"
)
target_link_libraries(portable_mods_sandbox PRIVATE
  sandbox_sim
  sandbox_imgui
  SDL3::SDL3-static
  libfpv::fpv
)
if(WIN32)
  target_link_libraries(portable_mods_sandbox PRIVATE opengl32)
  if(MINGW)
    target_link_options(portable_mods_sandbox PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  set_target_properties(portable_mods_sandbox PROPERTIES WIN32_EXECUTABLE OFF)
  add_custom_command(TARGET portable_mods_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:fpv> $<TARGET_FILE_DIR:portable_mods_sandbox>
    VERBATIM
  )
endif()
if(NOT MSVC)
  target_compile_options(portable_mods_sandbox PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# 8. Headless Automated Smoke Test Suite (CTest)
# ---------------------------------------------------------------------------
add_executable(sandbox_smoke_tests
  tests/smoke_tests.cpp
)
target_include_directories(sandbox_smoke_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(sandbox_smoke_tests PRIVATE
  sandbox_sim
  libfpv::fpv
)
if(WIN32)
  if(MINGW)
    target_link_options(sandbox_smoke_tests PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
  add_custom_command(TARGET sandbox_smoke_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:fpv> $<TARGET_FILE_DIR:sandbox_smoke_tests>
    VERBATIM
  )
endif()
if(NOT MSVC)
  target_compile_options(sandbox_smoke_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_test(NAME sandbox_smoke_tests COMMAND sandbox_smoke_tests)
