# World - a loaded level: what is in it, what can see what, and what collides.
#
# PhysicsWorld is one class across three translation units, the way ScriptEngine
# is: PhysicsWorld.cpp the lifecycle, the static world, the props, the step and
# the queries; PhysicsScriptBodies.cpp the bodies the scripts create and the
# characters they walk; PhysicsRagdolls.cpp the corpses. PhysicsWorldInternal.h
# carries the Jolt filters, the contact listener and Impl.
add_library(World STATIC
		Level.cpp Level.h
		Lighting.cpp Lighting.h
		Templates.cpp Templates.h
		Zones.cpp Zones.h
		CollisionMesh.cpp CollisionMesh.h
		PhysicsWorld.cpp PhysicsWorld.h
		PhysicsScriptBodies.cpp
		PhysicsRagdolls.cpp
		PhysicsWorldInternal.h
		Decals.cpp Decals.h
		)
target_link_libraries(World PUBLIC Assets Jolt)
# PUBLIC: Lighting.h sizes kMaxDynamicLights from this, and Render and Game
# both build uniform arrays against it. The shaders take the same number
# through shaderc --define. Set in the top-level CMakeLists.
target_compile_definitions(World PUBLIC PAINFUL_MAX_DYN_LIGHTS=${PAINFUL_MAX_DYN_LIGHTS})
painful_layer(World)
