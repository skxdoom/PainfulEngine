# Tools - PainfulTools.exe, the diagnostics.
#
# A console-subsystem executable, unlike the game: it exists to print, so it
# needs no WinMain shim to reattach to the console it was launched from.
#
# It links Render because five of the reports resolve texture references through
# TextureCache, which lives there. No device is ever created - TextureCache::Init
# takes createWhite=false for exactly this - so nothing here opens a window
# except `run`, which is a viewer on purpose.
add_executable(PainfulTools
  ToolsMain.cpp
  Commands.h
  LevelStats.h
  ViewerApp.cpp
  ReportLevel.cpp
  ReportMesh.cpp
  ReportTexture.cpp
  ReportRender.cpp
  ReportPhysics.cpp
  ReportScript.cpp
)
target_link_libraries(PainfulTools PRIVATE Game)
target_include_directories(PainfulTools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES
  $<TARGET_PROPERTY:PainfulTools,SOURCES>)
