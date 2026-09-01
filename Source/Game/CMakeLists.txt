# Game - the seam where the script natives meet the engine subsystems.
add_library(Game STATIC
  MenuSystem.cpp    MenuSystem.h
  ScriptEngine.cpp  ScriptEngine.h
  PlayerPawn.cpp    PlayerPawn.h
  Input.cpp         Input.h
)
target_link_libraries(Game PUBLIC Script Render Audio)
painful_layer(Game)
