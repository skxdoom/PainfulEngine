# The layering, checked rather than asserted.
#
#   Core <- Assets <- World <- Render;  Script beside Assets;  Audio off Core
#   Game is the seam; App and Tools sit on top
#
# The CMake targets enforce this at LINK time only, and that is weaker than it
# looks: an upward `#include "../Render/Foo.h"` resolves relative to the FILE,
# never touching the target's include directories, so a header-only or inline
# use compiles and links clean. Zones.cpp reached into Render/Frustum.h that way
# for as long as Frustum was there.
#
# So the rule is checked directly: every #include of a sibling layer is matched
# against what that layer may see, at configure time.

set(PAINFUL_LAYERS Core Assets Script World Render Audio Game App Tools)

# What each layer may include from. Anything else is a violation.
set(PAINFUL_SEES_Core    "")
set(PAINFUL_SEES_Assets  Core)
set(PAINFUL_SEES_Script  Core)
set(PAINFUL_SEES_Audio   Core)
set(PAINFUL_SEES_World   Core Assets)
set(PAINFUL_SEES_Render  Core Assets World)
set(PAINFUL_SEES_Game    Core Assets Script World Render Audio)
set(PAINFUL_SEES_App     Core Assets Script World Render Audio Game)
set(PAINFUL_SEES_Tools   Core Assets Script World Render Audio Game App)

function(painful_check_layering PAINFUL_SOURCE)
  set(violations "")
  foreach(layer IN LISTS PAINFUL_LAYERS)
    file(GLOB sources "${PAINFUL_SOURCE}/${layer}/*.cpp" "${PAINFUL_SOURCE}/${layer}/*.h")
    foreach(source IN LISTS sources)
      file(STRINGS ${source} lines REGEX "^[ \t]*#include[ \t]*\"")
      get_filename_component(name ${source} NAME)
      foreach(line IN LISTS lines)
        # "../Other/X.h" and "Other/X.h" both name a layer; "X.h" is own-layer.
        if(line MATCHES "#include[ \t]*\"(\.\./)?([A-Za-z]+)/")
          set(other ${CMAKE_MATCH_2})
          if(other IN_LIST PAINFUL_LAYERS AND NOT other STREQUAL layer
             AND NOT other IN_LIST PAINFUL_SEES_${layer})
            list(APPEND violations "  ${layer}/${name} includes ${other}/")
          endif()
        endif()
      endforeach()
    endforeach()
  endforeach()
  if(violations)
    list(JOIN violations "\n" report)
    message(FATAL_ERROR
      "Layering violation - a layer may only include the ones below it:\n${report}\n"
      "Either move the header down to a layer both sides may see, or the "
      "dependency is real and the layering in CMake/Layering.cmake is wrong.")
  endif()
endfunction()
