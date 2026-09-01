# Optional local deployment: when PAINFUL_DEPLOY_DIR is set (a local cache
# variable - never commit a machine path), every build copies the executable and
# the compiled shaders there, e.g. the game's Bin folder next to Painkiller.exe.

set(PAINFUL_DEPLOY_DIR "" CACHE PATH "Copy PainfulEngine.exe and Shaders/ here after each build")
if(PAINFUL_DEPLOY_DIR)
  # Hangs off "Shaders", NOT off PainfulEngine. "Shaders" depends on the
  # executable, so a POST_BUILD on PainfulEngine runs BEFORE shaderc has
  # regenerated anything - which shipped the previous build's shader binaries
  # alongside the current executable, every single time. A stale shader paired
  # with a current executable is close to undebuggable: a uniform the shader
  # still references but nothing sets any more reads as zero, and multiplying
  # UVs by it collapses every surface to a single texel.
  add_custom_command(TARGET Shaders POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:PainfulEngine>
            ${PAINFUL_DEPLOY_DIR}/PainfulEngine.exe
    COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_FILE_DIR:PainfulEngine>/Shaders
            ${PAINFUL_DEPLOY_DIR}/Shaders
    COMMENT "deploy -> ${PAINFUL_DEPLOY_DIR}"
  )
endif()
