cmake_minimum_required(VERSION 3.20)
# C is for Lua 5.0.2; everything else is C++.
project(PainfulEngine LANGUAGES C CXX)

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

# Jolt Physics, standing in for Havok. Jolt's own CMake is opinionated, so the
# options that would fight the rest of the build are turned off first:
#  - it overrides CMAKE_CXX_FLAGS_RELEASE and links the STATIC MSVC runtime by
#    default, either of which would clash with SDL and bgfx;
#  - it builds with warnings as errors, which is its business and not ours;
#  - its SIMD flags are PUBLIC, so anything linking Jolt is compiled with them.
#    Stopping at SSE4.2 keeps the executable runnable on older machines; the
#    physics load here is a few hundred bodies, not a reason for AVX2.
set(OVERRIDE_CXX_FLAGS              OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS             OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION    OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM            OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL                  OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE       OFF CACHE BOOL "" FORCE)
set(USE_AVX   OFF CACHE BOOL "" FORCE)
set(USE_AVX2  OFF CACHE BOOL "" FORCE)
set(USE_F16C  OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
add_subdirectory(External/JoltPhysics/Build EXCLUDE_FROM_ALL)

# DEFLATE for the .pak reader. bimg_decode already compiles a full miniz (it
# comes with tinyexr), so the engine reuses those symbols at link time rather
# than vendoring the library again - compiling miniz.c a second time
# double-defines every one of its C symbols. This target carries only the
# header; the executable gets the implementation through bimg_decode.
add_library(painful_miniz INTERFACE)
target_include_directories(painful_miniz INTERFACE
  External/bgfx/bimg/3rdparty/tinyexr/deps/miniz)

# Lua 5.0.2, vendored verbatim from lua.org (MIT licence). The version is
# load-bearing: the shipped scripts use 5.0-only forms (generic
# `for k,v in <table> do`, `table.getn`, `math.mod`) that 5.1+ rejects, and
# Engine.dll statically links exactly this interpreter. It is 2003-era ANSI C,
# so its warnings are not ours to fix.
add_library(painful_lua STATIC
  External/lua-5.0.2/src/lapi.c
  External/lua-5.0.2/src/lcode.c
  External/lua-5.0.2/src/ldebug.c
  External/lua-5.0.2/src/ldo.c
  External/lua-5.0.2/src/ldump.c
  External/lua-5.0.2/src/lfunc.c
  External/lua-5.0.2/src/lgc.c
  External/lua-5.0.2/src/llex.c
  External/lua-5.0.2/src/lmem.c
  External/lua-5.0.2/src/lobject.c
  External/lua-5.0.2/src/lopcodes.c
  External/lua-5.0.2/src/lparser.c
  External/lua-5.0.2/src/lstate.c
  External/lua-5.0.2/src/lstring.c
  External/lua-5.0.2/src/ltable.c
  External/lua-5.0.2/src/ltm.c
  External/lua-5.0.2/src/lundump.c
  External/lua-5.0.2/src/lvm.c
  External/lua-5.0.2/src/lzio.c
  External/lua-5.0.2/src/lib/lauxlib.c
  External/lua-5.0.2/src/lib/lbaselib.c
  External/lua-5.0.2/src/lib/ldblib.c
  External/lua-5.0.2/src/lib/liolib.c
  External/lua-5.0.2/src/lib/lmathlib.c
  External/lua-5.0.2/src/lib/loadlib.c
  External/lua-5.0.2/src/lib/lstrlib.c
  External/lua-5.0.2/src/lib/ltablib.c
)
target_include_directories(painful_lua
  PUBLIC External/lua-5.0.2/include
  PRIVATE External/lua-5.0.2/src)
if(MSVC)
  target_compile_options(painful_lua PRIVATE /w)
else()
  target_compile_options(painful_lua PRIVATE -w)
endif()

# ---------------------------------------------------------------- engine
# Layered on purpose, and the layering is one-directional:
#   core <- assets <- world <- render
# Each layer only knows about the ones to its left. Script sits beside
# assets: it needs only core (the VFS) plus the Lua interpreter - native
# implementations that touch world or render state are wired in from above.
add_library(painful_core STATIC
  Source/Core/Common.cpp
  Source/Core/PakArchive.cpp
  Source/Core/FileSystem.cpp
)
target_include_directories(painful_core PUBLIC Source)
target_link_libraries(painful_core PRIVATE painful_miniz)

add_library(painful_assets STATIC
  Source/Assets/Mpk.cpp
  Source/Assets/Dat.cpp
  Source/Assets/ShaderScript.cpp
  Source/Assets/Pkmdl.cpp
  Source/Assets/Ani.cpp
  Source/Assets/AnimationCache.cpp
  Source/Assets/SkeletonCache.cpp
  Source/Assets/Waypoints.cpp
  Source/Assets/Skeleton.cpp
  Source/Assets/Properties.cpp
  Source/Assets/Emitter.cpp
  Source/Assets/Tweaks.cpp
)
target_link_libraries(painful_assets PUBLIC painful_core)

add_library(painful_script STATIC
  Source/Script/LuaHost.cpp
  Source/Script/Natives.cpp
)
target_link_libraries(painful_script PUBLIC painful_core painful_lua)

add_library(painful_world STATIC
  Source/World/Level.cpp
  Source/World/Templates.cpp
  Source/World/Zones.cpp
  Source/World/CollisionMesh.cpp
  Source/World/PhysicsWorld.cpp
)
target_link_libraries(painful_world PUBLIC painful_assets Jolt)

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
  Source/Render/DebugLines.cpp
)
# bimg_decode provides imageParse (DDS/PNG/etc); bimg alone is just the core.
# bimg_decode must come BEFORE bimg: both ship the miniz symbols (bimg in a
# standalone miniz.obj, bimg_decode embedded in image_decode.obj, which is
# always pulled in for imageParse). The .pak reader references mz_uncompress,
# and with bimg searched first the linker loads miniz.obj too and every miniz
# symbol is defined twice. Searched in this order, the reference resolves from
# the already-loaded image_decode.obj and bimg's copy stays out.
target_link_libraries(painful_render PUBLIC painful_world SDL3::SDL3-static bgfx bimg_decode bimg bx)

# The game layer: the seam where script natives meet the engine subsystems.
add_library(painful_game STATIC
  Source/Game/ScriptEngine.cpp
  Source/Game/PlayerPawn.cpp
  Source/Game/Input.cpp
)
target_link_libraries(painful_game PUBLIC painful_script painful_render)

# WIN32: a GUI-subsystem executable, so double-clicking it opens no console.
# main() is kept as the entry point via the WinMain shim in main.cpp.
add_executable(PainfulEngine WIN32 Source/main.cpp)
target_link_libraries(PainfulEngine PRIVATE painful_game)

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
add_shader(vs_debug    v s_5_0)
add_shader(fs_debug    f s_5_0)
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
