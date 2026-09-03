# App - the executable: the entry point and the game it starts.
#
# The only layer above Game. Main.cpp handles the launch (bare, or `game` with
# its flags) and GameApp runs the script-driven world. The diagnostics that used
# to share this file are their own executable now, in Tools/.
#
# WIN32: a GUI-subsystem executable, so double-clicking it opens no console.
# main() is kept as the entry point via the WinMain shim in Main.cpp.
#
# PainfulEngine.rc carries the icon - ../PainfulEngine.ico, shared with Tools.
add_executable(PainfulEngine WIN32
  Main.cpp
  GameApp.cpp  GameApp.h
  PainfulEngine.rc
)
target_link_libraries(PainfulEngine PRIVATE Game)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameApp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameApp.h
  ${CMAKE_CURRENT_SOURCE_DIR}/PainfulEngine.rc)

# The .ico is not a source. MSBuild's tracker finds it anyway; Ninja does not
# scan RC statements, and would keep linking the previous icon.
set_source_files_properties(PainfulEngine.rc PROPERTIES
  OBJECT_DEPENDS ${PAINFUL_SOURCE}/PainfulEngine.ico)

# The optional copy into a game folder. It lives here because
# add_custom_command(TARGET) has to be in the directory that defines the target.
include(Deploy)
