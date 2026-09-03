# Game - the seam where the script natives meet the engine subsystems.
#
# ScriptEngine is one class across many translation units: ScriptEngine.cpp
# holds the lifecycle and the entity registry, each Script*.cpp implements one
# family of natives, and ScriptBind maps them to the module and name the
# shipped Lua calls them by. ScriptEngineInternal.h carries what they share.
add_library(Game STATIC
  MenuSystem.cpp    MenuSystem.h
  PlayerPawn.cpp    PlayerPawn.h
  Input.cpp         Input.h

  ScriptEngine.cpp  ScriptEngine.h
                    ScriptEngineInternal.h
  ScriptEntity.cpp
  ScriptMonster.cpp
  ScriptSound.cpp
  ScriptPlayer.cpp
  ScriptInput.cpp
  ScriptTrace.cpp
  ScriptAnim.cpp
  ScriptWorld.cpp
  ScriptHud.cpp
  ScriptMenu.cpp
  ScriptDeath.cpp
  ScriptLimbs.cpp
  ScriptBind.cpp
  ScriptCollision.cpp
  ScriptExplosion.cpp
  ScriptWater.cpp
  ScriptSave.cpp
)
target_link_libraries(Game PUBLIC Script Render Audio)
painful_layer(Game)
