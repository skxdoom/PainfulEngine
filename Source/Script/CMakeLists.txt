# Script - the Lua 5.0.2 host and the native API surface. Sits beside Assets:
# it needs only Core (the VFS) plus the interpreter. Natives that touch world or
# render state are wired in from Game, above.
add_library(Script STATIC
  LuaHost.cpp  LuaHost.h
  Natives.cpp  NativeList.inc
)
target_link_libraries(Script PUBLIC Core lua)
painful_layer(Script)
