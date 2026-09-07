#pragma once
#include <string>

#include "../Render/DebugLines.h"
#include "../Render/Renderer.h"
#include "../Render/TextureCache.h"
#include "../Render/Window.h"
#include "../Assets/Emitter.h"
#include "../Assets/ShaderScript.h"
#include "../World/PhysicsWorld.h"

// Everything a windowed run needs before it knows which level it is showing.
//
// The game and the `run` viewer brought up the same seven subsystems in the
// same order, separately. They are level-independent - the window, the device,
// the caches keyed by name, and the empty simulation - so one type owns them
// and both executables borrow it.
//
// Declaration order is destruction order reversed: the window and the device
// are declared first so they outlive every GPU resource built against them.
// Anything the caller creates afterwards is therefore torn down first, which
// is what a bgfx handle needs.
namespace painful {

class EngineBoot {
public:
	// Opens the window and brings the device and the caches up. False when the
	// window or the renderer will not start, which is the caller's cue to give
	// up rather than draw into nothing.
	bool Init(const std::string& dataRoot, const char* exePath, const std::string& title,
			int width = 1280, int height = 720);

	const std::string& root() const { return root_; }
	// Where the per-backend compiled shaders live, for a subsystem that loads
	// its own. Empty when the executable carries them embedded.
	const std::string& shaderDir() const { return shaderDir_; }

	Window& window() { return window_; }
	Renderer& renderer() { return renderer_; }
	TextureCache& textures() { return textures_; }
	ShaderLibrary& shaders() { return shaders_; }
	EmitterLibrary& emitters() { return emitters_; }
	PhysicsWorld& physics() { return physics_; }
	DebugLines& debugLines() { return debugLines_; }
	// The overlay is optional: a backend that will not compile its shader
	// leaves the rest of the run working.
	bool debugLinesReady() const { return debugLinesReady_; }

private:
	std::string root_;
	std::string shaderDir_;

	Window window_;
	Renderer renderer_;
	TextureCache textures_;
	ShaderLibrary shaders_;
	EmitterLibrary emitters_;
	PhysicsWorld physics_;
	DebugLines debugLines_;
	bool debugLinesReady_ = false;
};

} // namespace painful
