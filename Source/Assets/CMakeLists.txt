# Assets - the shipped file formats, each one decoded rather than guessed.
add_library(Assets STATIC
  Mpk.cpp             Mpk.h
  Dat.cpp             Dat.h
  ShaderScript.cpp    ShaderScript.h
  Pkmdl.cpp           Pkmdl.h
  Ani.cpp             Ani.h
  Rde.cpp             Rde.h
  Hke.cpp             Hke.h
  AnimationCache.cpp  AnimationCache.h
  SkeletonCache.cpp   SkeletonCache.h
  Waypoints.cpp       Waypoints.h
  Skeleton.cpp        Skeleton.h
  Properties.cpp      Properties.h
  Emitter.cpp         Emitter.h
  Tweaks.cpp          Tweaks.h
)
target_link_libraries(Assets PUBLIC Core)
painful_layer(Assets)
