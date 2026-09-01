# IDE folder organisation.
#
# The solution is meant to read like Source/ on disk: one project per layer,
# named for its directory, with its files in the tree they actually live in.
# Without this every target - ours and all 25 of the third-party ones - lands at
# the root of the solution, which says nothing about the engine's shape.

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

# Put every target a subdirectory defined (and its own subdirectories defined)
# into one IDE folder. bgfx.cmake alone contributes 19 targets across a nested
# tree, so this recurses rather than listing them.
function(painful_group_directory DIR FOLDER_NAME)
  get_property(targets DIRECTORY ${DIR} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    set_target_properties(${target} PROPERTIES FOLDER ${FOLDER_NAME})
  endforeach()

  get_property(subdirs DIRECTORY ${DIR} PROPERTY SUBDIRECTORIES)
  foreach(subdir IN LISTS subdirs)
    painful_group_directory(${subdir} ${FOLDER_NAME})
  endforeach()
endfunction()

# One engine layer -> one project, named for its directory. Sources are grouped
# relative to that directory, so opening the project in the IDE shows the same
# file list as opening the folder.
function(painful_layer NAME)
  set_target_properties(${NAME} PROPERTIES FOLDER "Engine")
  get_target_property(sources ${NAME} SOURCES)
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${sources})
endfunction()
