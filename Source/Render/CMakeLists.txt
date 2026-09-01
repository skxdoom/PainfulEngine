# Render - bgfx and everything drawn through it.
add_library(Render STATIC
  Window.cpp             Window.h
  Renderer.cpp           Renderer.h
  TextureCache.cpp       TextureCache.h
  WorldRenderer.cpp      WorldRenderer.h
  EntityRenderer.cpp     EntityRenderer.h
  MaterialState.cpp      MaterialState.h
  SkyRenderer.cpp        SkyRenderer.h
  ParticleRenderer.cpp   ParticleRenderer.h
  BillboardRenderer.cpp  BillboardRenderer.h
  DebugLines.cpp         DebugLines.h
  FontCache.cpp          FontCache.h
  HudRenderer.cpp        HudRenderer.h
                         Camera.h
                         Frustum.h
                         MeshVertex.h
)

# bimg_decode provides imageParse (DDS/PNG/etc); bimg alone is just the core.
# bimg_decode must come BEFORE bimg: both ship the miniz symbols (bimg in a
# standalone miniz.obj, bimg_decode embedded in image_decode.obj, which is
# always pulled in for imageParse). The .pak reader references mz_uncompress,
# and with bimg searched first the linker loads miniz.obj too and every miniz
# symbol is defined twice. Searched in this order, the reference resolves from
# the already-loaded image_decode.obj and bimg's copy stays out.
target_link_libraries(Render PUBLIC World SDL3::SDL3-static bgfx bimg_decode bimg bx)

# stb_truetype and stb_rect_pack, for the HUD's text. Both already ship inside
# bgfx's third-party tree, so the font rasteriser needs no new dependency -
# only the include path to reach them.
target_include_directories(Render PRIVATE
  ${PAINFUL_ROOT}/External/bgfx/bgfx/3rdparty/stb)

painful_layer(Render)
