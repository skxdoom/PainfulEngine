cmake_minimum_required(VERSION 3.20)
project(PainfulEngine LANGUAGES CXX)

# PainfulEngine - open reimplementation of PainEngine (Painkiller, 2004).
# Requires the user's own copy of the game data. No original assets are shipped.

# bx (a bgfx dependency) uses designated initializers, so C++20 is required.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- dependencies
# Third-party sources live in External/ and are built from source so there is
# nothing to install system-wide.
set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL  OFF CACHE BOOL "" FORCE)
add_subdirectory(External/SDL EXCLUDE_FROM_ALL)

# shaderc (part of TOOLS) is needed to compile our shaders, so it stays on.
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL        OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS    ON  CACHE BOOL "" FORCE)
set(BGFX_AMALGAMATED    ON  CACHE BOOL "" FORCE)
set(BX_AMALGAMATED      ON  CACHE BOOL "" FORCE)
# Not EXCLUDE_FROM_ALL: we need the shaderc target addressable for shader builds.
add_subdirectory(External/bgfx)

# ---------------------------------------------------------------- engine
# Layered on purpose, and the layering is one-directional:
#   core <- assets <- world <- render
# Each layer only knows about the ones to its left.
add_library(painful_core STATIC Source/Core/Common.cpp)
target_include_directories(painful_core PUBLIC Source)

add_library(painful_assets STATIC
  Source/Assets/Mpk.cpp
  Source/Assets/Dat.cpp
  Source/Assets/ShaderScript.cpp
  Source/Assets/Pkmdl.cpp
  Source/Assets/Ani.cpp
  Source/Assets/Skeleton.cpp
  Source/Assets/Properties.cpp
  Source/Assets/Emitter.cpp
)
target_link_libraries(painful_assets PUBLIC painful_core)

add_library(painful_world STATIC
  Source/World/Level.cpp
  Source/World/Templates.cpp
  Source/World/Zones.cpp
  Source/World/CollisionMesh.cpp
)
target_link_libraries(painful_world PUBLIC painful_assets)

add_library(painful_render STATIC
  Source/Render/Window.cpp
  Source/Render/Renderer.cpp
  Source/Render/TextureCache.cpp
  Source/Render/WorldRenderer.cpp
  Source/Render/EntityRenderer.cpp
  Source/Render/MaterialState.cpp
  Source/Render/SkyRenderer.cpp
  Source/Render/ParticleRenderer.cpp
  Source/Render/BillboardRenderer.cpp
)
# bimg_decode provides imageParse (DDS/PNG/etc); bimg alone is just the core.
target_link_libraries(painful_render PUBLIC painful_world SDL3::SDL3-static bgfx bimg bimg_decode bx)

# WIN32: a GUI-subsystem executable, so double-clicking it opens no console.
# main() is kept as the entry point via the WinMain shim in main.cpp.
add_executable(PainfulEngine WIN32 Source/main.cpp)
target_link_libraries(PainfulEngine PRIVATE painful_render)

# Compile the shaders whenever their sources change, into the shaders folder
# next to the executable. Depends on the shaderc tool built from bgfx.
set(SHADER_OUT_DIR $<TARGET_FILE_DIR:PainfulEngine>/Shaders)
set(SHADER_INC ${CMAKE_CURRENT_SOURCE_DIR}/External/bgfx/bgfx/src)
set(SHADER_DEF ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/varying.def.sc)

function(add_shader NAME TYPE PROFILE)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Shaders/${NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
    COMMAND $<TARGET_FILE:shaderc>
            -f ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${NAME}.sc
            -o ${SHADER_OUT_DIR}/${NAME}.bin
            --type ${TYPE} --platform windows -p ${PROFILE}
            -i ${SHADER_INC} --varyingdef ${SHADER_DEF}
    COMMAND ${CMAKE_COMMAND} -E copy ${SHADER_OUT_DIR}/${NAME}.bin
            ${CMAKE_CURRENT_BINARY_DIR}/Shaders/${NAME}.bin
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${NAME}.sc ${SHADER_DEF} shaderc
    COMMENT "shaderc ${NAME}.sc"
  )
  list(APPEND SHADER_BINS ${CMAKE_CURRENT_BINARY_DIR}/Shaders/${NAME}.bin)
  set(SHADER_BINS ${SHADER_BINS} PARENT_SCOPE)
endfunction()

add_shader(vs_world v s_5_0)
add_shader(fs_world f s_5_0)
add_shader(fs_sky   f s_5_0)
add_shader(vs_particle v s_5_0)
add_shader(fs_particle f s_5_0)
add_shader(vs_water    v s_5_0)
add_shader(fs_water    f s_5_0)

add_custom_target(shaders ALL DEPENDS ${SHADER_BINS})
add_dependencies(shaders PainfulEngine)

# Optional local deployment: when PAINFUL_DEPLOY_DIR is set (a local cache
# variable - never commit a machine path), every build copies the executable
# and shaders there, e.g. the game's Bin folder next to Painkiller.exe.
set(PAINFUL_DEPLOY_DIR "" CACHE PATH "Copy PainfulEngine.exe and Shaders/ here after each build")
if(PAINFUL_DEPLOY_DIR)
  # Hangs off "shaders", NOT off PainfulEngine. "shaders" depends on the
  # executable, so a POST_BUILD on PainfulEngine runs BEFORE shaderc has
  # regenerated anything - which shipped the previous build's shader binaries
  # alongside the current executable, every single time. A stale shader paired
  # with a current executable is close to undebuggable: a uniform the shader
  # still references but nothing sets any more reads as zero, and multiplying
  # UVs by it collapses every surface to a single texel.
  add_custom_command(TARGET shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:PainfulEngine>
            ${PAINFUL_DEPLOY_DIR}/PainfulEngine.exe
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_OUT_DIR}
            ${PAINFUL_DEPLOY_DIR}/Shaders
    COMMENT "deploy -> ${PAINFUL_DEPLOY_DIR}"
  )
endif()
