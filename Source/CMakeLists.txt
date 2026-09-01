# The engine, layered on purpose, and the layering is one-directional:
#
#   Core <- Assets <- World <- Render
#
# Each layer only knows about the ones to its left. Script sits beside Assets;
# Audio hangs off Core; Game is the seam above all of them. One directory, one
# CMakeLists, one target, one project in the IDE - so the solution reads like
# this folder does.
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

# WIN32: a GUI-subsystem executable, so double-clicking it opens no console.
# main() is kept as the entry point via the WinMain shim in main.cpp.
# It sits at the solution root rather than inside Engine/: it is the product,
# not a layer.
add_executable(PainfulEngine WIN32 main.cpp)
target_link_libraries(PainfulEngine PRIVATE Game)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES main.cpp)
