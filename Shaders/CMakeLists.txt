# The bgfx shader sources, compiled by shaderc into Shaders/ beside the
# executable. One project, sibling to PainfulEngine: shaders are a build
# product of their own, not a layer of the engine.

set(SHADER_OUT_DIR $<TARGET_FILE_DIR:PainfulEngine>/Shaders)
set(SHADER_INC ${PAINFUL_ROOT}/External/bgfx/bgfx/src)
set(SHADER_DEF ${CMAKE_CURRENT_SOURCE_DIR}/varying.def.sc)

# Each shader compiles straight into the output folder, then a copy lands in
# stamps/ - that copy is what the custom command declares as its OUTPUT, so the
# build only re-runs shaderc when a .sc actually changes. (The real output path
# uses a generator expression, which OUTPUT cannot take.)
function(add_shader NAME TYPE PROFILE)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stamps/${NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
    COMMAND $<TARGET_FILE:shaderc>
            -f ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.sc
            -o ${SHADER_OUT_DIR}/${NAME}.bin
            --type ${TYPE} --platform windows -p ${PROFILE}
            -i ${SHADER_INC} --varyingdef ${SHADER_DEF}
    COMMAND ${CMAKE_COMMAND} -E copy ${SHADER_OUT_DIR}/${NAME}.bin
            ${CMAKE_CURRENT_BINARY_DIR}/stamps/${NAME}.bin
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.sc ${SHADER_DEF} shaderc
    COMMENT "shaderc ${NAME}.sc"
  )
  list(APPEND SHADER_STAMPS ${CMAKE_CURRENT_BINARY_DIR}/stamps/${NAME}.bin)
  set(SHADER_STAMPS ${SHADER_STAMPS} PARENT_SCOPE)
  list(APPEND SHADER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.sc)
  set(SHADER_SOURCES ${SHADER_SOURCES} PARENT_SCOPE)
endfunction()

add_shader(vs_world    v s_5_0)
add_shader(fs_world    f s_5_0)
add_shader(vs_entity   v s_5_0)
add_shader(fs_entity   f s_5_0)
add_shader(fs_sky      f s_5_0)
add_shader(vs_particle v s_5_0)
add_shader(fs_particle f s_5_0)
add_shader(vs_debug    v s_5_0)
add_shader(fs_debug    f s_5_0)
add_shader(vs_hud      v s_5_0)
add_shader(fs_hud      f s_5_0)
add_shader(vs_water    v s_5_0)
add_shader(fs_water    f s_5_0)

# SOURCES carries the .sc files so they are browsable and editable in the IDE;
# HEADER_FILE_ONLY stops anything trying to compile them as C++.
add_custom_target(Shaders ALL
  DEPENDS ${SHADER_STAMPS}
  SOURCES ${SHADER_SOURCES} ${SHADER_DEF})
set_source_files_properties(${SHADER_SOURCES} ${SHADER_DEF} PROPERTIES HEADER_FILE_ONLY TRUE)
add_dependencies(Shaders PainfulEngine)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "" FILES ${SHADER_SOURCES} ${SHADER_DEF})

# The deploy hangs off this target (and so must be declared in this directory:
# add_custom_command(TARGET) requires it). Why it hangs off Shaders and not off
# the executable is in CMake/Deploy.cmake.
include(Deploy)
