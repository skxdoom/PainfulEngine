# Game - the seam where the script natives meet the engine subsystems.
#
# ScriptEngine is one class across many translation units: ScriptEngine.cpp
# holds the lifecycle and the entity registry, and each Script*.cpp owns one
# family of natives - the struct that declares them, their bodies, and the
# table that binds them. ScriptBind only calls the seventeen binders, so adding
# a native touches one file instead of the header and all thirty TUs below it.
# ScriptEngineInternal.h carries what they share.
#
# EngineBoot is the window, the device and the level-independent caches, shared
# with PainfulTools' `run` viewer so the two do not boot separately.
add_library(Game STATIC
		MenuSystem.cpp MenuSystem.h
		Console.cpp Console.h
		PlayerPawn.cpp PlayerPawn.h
		EngineBoot.cpp EngineBoot.h
		Input.cpp Input.h

		ScriptEngine.cpp ScriptEngine.h
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
		ScriptConsole.cpp
		ScriptDeath.cpp
		ScriptLimbs.cpp
		ScriptBind.cpp
		ScriptCollision.cpp
		ScriptExplosion.cpp
		ScriptWater.cpp
		ScriptSave.cpp
		ScriptDecal.cpp
		ScriptLight.cpp
		)
target_link_libraries(Game PUBLIC Script Render Audio)
painful_layer(Game)
