cmake_minimum_required(VERSION 3.20)
# C is for Lua 5.0.2; everything else is C++.
project(PainfulEngine LANGUAGES C CXX)

# PainfulEngine - open reimplementation of PainEngine (Painkiller, 2004).
# Requires the user's own copy of the game data. No original assets are shipped.
#
#   CMake/Folders.cmake       IDE folder helpers
#   CMake/Dependencies.cmake  External/ - SDL3, bgfx, Jolt, miniz, Lua 5.0.2
#   Source/CMakeLists.txt     the engine layers and the executable
#   Shaders/CMakeLists.txt    shaderc invocations
#   CMake/Deploy.cmake        the optional copy into a game folder (from Shaders/)

# Multi-config generators offer four configurations by default. MinSizeRel
# optimises for binary size at the cost of speed, which no desktop game wants,
# so it is dropped rather than left in the dropdown to be picked by mistake.
#   Debug           unoptimised, full symbols - too slow to play, right for a
#                   crash you need to step through
#   RelWithDebInfo  optimised and keeps a .pdb - the day-to-day config
#   Release         optimised, no symbols - what ships
if(CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo;Release" CACHE STRING "" FORCE)
endif()

set(PAINFUL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH ${PAINFUL_ROOT}/CMake)

# How many dynamic lights one model or one chunk of world is lit by at once.
#
# THE ONLY PLACE THIS NUMBER IS WRITTEN. The C++ side sizes its uniform arrays
# by it and the shaders declare theirs by it, and bgfx matches the two by name
# at runtime rather than at compile time - so a value that drifts between them
# is silently wrong lighting, not a build error. Both sides are fed from here:
# Source/World/CMakeLists.txt as a compile definition, Shaders/CMakeLists.txt
# as a shaderc --define. Docs/Reference/Lighting.md
#
# The original's four was a vertex-constant budget (Entity::MaxLights, filling
# c12..c23), not a rule; the lighting is per pixel from uniforms here.
set(PAINFUL_MAX_DYN_LIGHTS 8 CACHE STRING "Dynamic lights per model / per world chunk")

# Everything buildable lands in two folders rather than beside whichever
# CMakeLists declared it. The generator appends /<Config>, so a Release build
# leaves both executables in Build/Bin/Release.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Lib)

# bx (a bgfx dependency) uses designated initializers, so C++20 is required.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Our warning level, carried by a target rather than set globally: applied
# globally it also lands on SDL, bgfx and Jolt and buries our own diagnostics
# under thousands of theirs. Core links it PUBLIC, so every engine target
# downstream inherits it.
add_library(EngineWarnings INTERFACE)
if(MSVC)
	target_compile_options(EngineWarnings INTERFACE /W4 /permissive-)
else()
	target_compile_options(EngineWarnings INTERFACE -Wall -Wextra)
endif()

# The executable icons (Source/App and Source/Tools .rc files). Visual Studio
# compiles .rc unasked; Ninja and the Makefile generators do not.
if(WIN32)
	enable_language(RC)
endif()

include(Folders)
include(Layering)
# Checked here rather than by the targets: an upward relative include compiles
# and links clean, so only a direct check catches one. CMake/Layering.cmake.
painful_check_layering(${PAINFUL_ROOT}/Source)
include(Dependencies)
# Shaders first: it compiles the .sc sources into C arrays that Render
# includes, so its generated headers have to exist before Source builds.
add_subdirectory(Shaders)
add_subdirectory(Source)

set_property(DIRECTORY ${PAINFUL_ROOT} PROPERTY VS_STARTUP_PROJECT PainfulEngine)
