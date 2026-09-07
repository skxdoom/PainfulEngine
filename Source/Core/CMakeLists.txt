# Core - the foundation: no dependency on anything else in the engine.
#   Vectors     Vec3 (and Quat), the (w,x,y,z) rotation convention, kPi
#   Matrix      Mat4 and the row-vector 3x3 <-> quaternion conversions
#   Reader      little-endian reads over a buffer, bounds-checked
#   PakArchive  one .pak: directory parse, name de-obfuscation, inflate; PakWriter
#   FileSystem  the mounted view: archives shadow loose files, and the
#               ReadFile/WriteFile every loader goes through
#   AppPaths    finding the game data and the executable's own resources
#   CrashReport a symbolised stack on a fault, to stderr and a file
#   Check       PAINFUL_CHECK/ASSERT: a failed invariant that is logged, not silent
#   Debug       the PAINFUL_* switch table, and the `traces` listing
#   Frustum     six view planes and AABB tests - pure maths, so World may use it
#               without reaching up into Render
add_library(Core STATIC
  Vectors.cpp     Vectors.h
  Matrix.cpp      Matrix.h
                  Reader.h
  PakArchive.cpp  PakArchive.h
  FileSystem.cpp  FileSystem.h
  AppPaths.cpp    AppPaths.h
  CrashReport.cpp CrashReport.h
  Log.cpp         Log.h
                  Frustum.h
  Check.cpp       Check.h
  Debug.cpp       Debug.h
  Config.cpp      Config.h
)
# Source/ is the include root: headers are reached as "../Core/Vectors.h".
target_include_directories(Core PUBLIC ${PAINFUL_SOURCE})
# dbghelp is the symbol lookup and the stack walk behind CrashReport.
target_link_libraries(Core PRIVATE miniz PUBLIC EngineWarnings)
if(WIN32)
  target_link_libraries(Core PRIVATE dbghelp)
endif()
painful_layer(Core)
