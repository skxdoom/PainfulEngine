# Audio - one mixed device stream behind SOUND / SOUND2D / SOUND3D.
add_library(Audio STATIC
		AudioEngine.cpp AudioEngine.h
		)
target_link_libraries(Audio PUBLIC Core SDL3::SDL3-static)
painful_layer(Audio)

# minimp3 (CC0), header-only: the music streams.
target_include_directories(Audio PRIVATE ${PAINFUL_ROOT}/External/minimp3)
