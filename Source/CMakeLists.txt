# The engine, layered on purpose, and the layering is one-directional:
#
#   Core <- Assets <- World <- Render
#
# Each layer only knows about the ones to its left. Script sits beside Assets;
# Audio hangs off Core; Game is the seam above all of them; App is the
# executable on top. One directory, one CMakeLists, one target, one project in
# the IDE - so the solution reads like this folder does.
#
# Headers are listed with their sources so they are browsable in the IDE, and
# so the header-only ones (Log, Camera, Frustum, MeshVertex) appear at all.

set(PAINFUL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(Core)
add_subdirectory(Assets)
add_subdirectory(Script)
add_subdirectory(World)
add_subdirectory(Render)
add_subdirectory(Audio)
add_subdirectory(Game)
add_subdirectory(App)
add_subdirectory(Tools)
