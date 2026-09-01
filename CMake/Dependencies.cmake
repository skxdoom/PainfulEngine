# Third-party sources live in External/ and are built from source, so there is
# nothing to install system-wide. Each block turns off the options that would
# otherwise fight the rest of the build.

# ------------------------------------------------------------------------ SDL3
set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL  OFF CACHE BOOL "" FORCE)
add_subdirectory(${PAINFUL_ROOT}/External/SDL ${CMAKE_BINARY_DIR}/external/SDL EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------ bgfx
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL        OFF CACHE BOOL "" FORCE)
# We need exactly one of bgfx's tools - shaderc. Left at the default, TOOLS also
# builds texturec, texturev, geometryc, geometryv and bin2c; the two viewers are
# full applications, and none of the five is ever invoked here.
set(BGFX_BUILD_TOOLS          ON  CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER   ON  CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE  OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C    OFF CACHE BOOL "" FORCE)
set(BGFX_AMALGAMATED    ON  CACHE BOOL "" FORCE)
set(BX_AMALGAMATED      ON  CACHE BOOL "" FORCE)
# Not EXCLUDE_FROM_ALL: we need the shaderc target addressable for shader builds.
add_subdirectory(${PAINFUL_ROOT}/External/bgfx ${CMAKE_BINARY_DIR}/external/bgfx)

# Two bgfx targets build by default even with the examples and texture tools
# off, and nothing we link reaches either: example-common is the samples'
# imgui/nanovg harness, bimg_encode the ASTC/ETC compressor texturec used.
# The exclusions only drop them from the default build - were a link ever to
# need one, it would still be built as a dependency.
foreach(unused example-common bimg_encode)
  if(TARGET ${unused})
    set_target_properties(${unused} PROPERTIES
      EXCLUDE_FROM_ALL           TRUE   # out of the ALL_BUILD target
      EXCLUDE_FROM_DEFAULT_BUILD TRUE)  # and unticked in the solution itself
  endif()
endforeach()

# ------------------------------------------------------------------------ Jolt
# Standing in for Havok. Jolt's own CMake is opinionated:
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
add_subdirectory(${PAINFUL_ROOT}/External/JoltPhysics/Build ${CMAKE_BINARY_DIR}/external/Jolt EXCLUDE_FROM_ALL)

# ----------------------------------------------------------------------- miniz
# DEFLATE for the .pak reader. bimg_decode already compiles a full miniz (it
# comes with tinyexr), so the engine reuses those symbols at link time rather
# than vendoring the library again - compiling miniz.c a second time
# double-defines every one of its C symbols. This target carries only the
# header; the executable gets the implementation through bimg_decode.
add_library(miniz INTERFACE)
target_include_directories(miniz INTERFACE
  ${PAINFUL_ROOT}/External/bgfx/bimg/3rdparty/tinyexr/deps/miniz)

# ------------------------------------------------------------------- Lua 5.0.2
# Vendored verbatim from lua.org (MIT licence). The version is load-bearing:
# the shipped scripts use 5.0-only forms (generic `for k,v in <table> do`,
# `table.getn`, `math.mod`) that 5.1+ rejects, and Engine.dll statically links
# exactly this interpreter. It is 2003-era ANSI C, so its warnings are not ours.
set(PAINFUL_LUA_DIR ${PAINFUL_ROOT}/External/lua-5.0.2)
add_library(lua STATIC
  ${PAINFUL_LUA_DIR}/src/lapi.c
  ${PAINFUL_LUA_DIR}/src/lcode.c
  ${PAINFUL_LUA_DIR}/src/ldebug.c
  ${PAINFUL_LUA_DIR}/src/ldo.c
  ${PAINFUL_LUA_DIR}/src/ldump.c
  ${PAINFUL_LUA_DIR}/src/lfunc.c
  ${PAINFUL_LUA_DIR}/src/lgc.c
  ${PAINFUL_LUA_DIR}/src/llex.c
  ${PAINFUL_LUA_DIR}/src/lmem.c
  ${PAINFUL_LUA_DIR}/src/lobject.c
  ${PAINFUL_LUA_DIR}/src/lopcodes.c
  ${PAINFUL_LUA_DIR}/src/lparser.c
  ${PAINFUL_LUA_DIR}/src/lstate.c
  ${PAINFUL_LUA_DIR}/src/lstring.c
  ${PAINFUL_LUA_DIR}/src/ltable.c
  ${PAINFUL_LUA_DIR}/src/ltm.c
  ${PAINFUL_LUA_DIR}/src/lundump.c
  ${PAINFUL_LUA_DIR}/src/lvm.c
  ${PAINFUL_LUA_DIR}/src/lzio.c
  ${PAINFUL_LUA_DIR}/src/lib/lauxlib.c
  ${PAINFUL_LUA_DIR}/src/lib/lbaselib.c
  ${PAINFUL_LUA_DIR}/src/lib/ldblib.c
  ${PAINFUL_LUA_DIR}/src/lib/liolib.c
  ${PAINFUL_LUA_DIR}/src/lib/lmathlib.c
  ${PAINFUL_LUA_DIR}/src/lib/loadlib.c
  ${PAINFUL_LUA_DIR}/src/lib/lstrlib.c
  ${PAINFUL_LUA_DIR}/src/lib/ltablib.c
)
target_include_directories(lua
  PUBLIC  ${PAINFUL_LUA_DIR}/include
  PRIVATE ${PAINFUL_LUA_DIR}/src)
if(MSVC)
  target_compile_options(lua PRIVATE /w)
else()
  target_compile_options(lua PRIVATE -w)
endif()

# --------------------------------------------------------------- IDE grouping
painful_group_directory(${PAINFUL_ROOT}/External/SDL                "External/SDL")
painful_group_directory(${PAINFUL_ROOT}/External/bgfx               "External/bgfx")
painful_group_directory(${PAINFUL_ROOT}/External/JoltPhysics/Build  "External/Jolt")
set_target_properties(lua   PROPERTIES FOLDER "External/Lua")
set_target_properties(miniz PROPERTIES FOLDER "External")
