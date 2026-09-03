# Optional local deployment: when PAINFUL_DEPLOY_DIR is set (a local cache
# variable - never commit a machine path), every build copies the executable
# there, e.g. the game's Bin folder next to Painkiller.exe.
#
# The shaders used to be copied alongside, and had to hang off the Shaders
# target to avoid shipping them one build stale. They are compiled into the
# executable now, so there is one file to copy and nothing to keep in step.

set(PAINFUL_DEPLOY_DIR "" CACHE PATH "Copy PainfulEngine.exe here after each build")
if(PAINFUL_DEPLOY_DIR)
  add_custom_command(TARGET PainfulEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:PainfulEngine>
            ${PAINFUL_DEPLOY_DIR}/PainfulEngine.exe
    COMMENT "deploy -> ${PAINFUL_DEPLOY_DIR}"
  )
endif()
