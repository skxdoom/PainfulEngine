# World - a loaded level: what is in it, what can see what, and what collides.
add_library(World STATIC
  Level.cpp          Level.h
  Lighting.cpp       Lighting.h
  Templates.cpp      Templates.h
  Zones.cpp          Zones.h
  CollisionMesh.cpp  CollisionMesh.h
  PhysicsWorld.cpp   PhysicsWorld.h
)
target_link_libraries(World PUBLIC Assets Jolt)
painful_layer(World)
