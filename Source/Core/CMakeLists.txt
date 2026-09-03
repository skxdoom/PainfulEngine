# Core - the foundation: no dependency on anything else in the engine.
#   Common      Mat4, Reader, ReadFile
#   PakArchive  one .pak: directory parse, name de-obfuscation, inflate; PakWriter
#   FileSystem  the mounted view: archives shadow loose files
#   AppPaths    finding the game data and the executable's own resources
add_library(Core STATIC
  Common.cpp      Common.h
  PakArchive.cpp  PakArchive.h
  FileSystem.cpp  FileSystem.h
  AppPaths.cpp    AppPaths.h
                  Log.h
)
# Source/ is the include root: headers are reached as "../Core/Common.h".
target_include_directories(Core PUBLIC ${PAINFUL_SOURCE})
target_link_libraries(Core PRIVATE miniz PUBLIC EngineWarnings)
painful_layer(Core)
