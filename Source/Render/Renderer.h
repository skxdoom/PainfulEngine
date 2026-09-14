#pragma once
#include <bgfx/bgfx.h>
#include <cstdint>
#include <string>

namespace painful {

struct Camera;

class Window;

// bgfx front end. The renderer owns backend setup and the frame loop; nothing
// above it needs to know which graphics API is in use.
class Renderer {
public:
	// The flashlight's depth pass goes first, into its own target, so the
	// world and the models can sample it. Render/ShadowMap.h owns the view.
	static constexpr bgfx::ViewId kShadowView = 0;
	// The models' shadows from the environment directional, likewise.
	static constexpr bgfx::ViewId kModelShadowView = 1;
	// The placed lights' shadow atlas: six faces per light, up to eight
	// lights (Render/LightShadowAtlas.h).
	static constexpr bgfx::ViewId kLightShadowViewBase = 2;
	static constexpr bgfx::ViewId kLightShadowViewCount = 48;
	// The view model's own map (Render/ViewModelShadows.h).
	static constexpr bgfx::ViewId kViewModelShadowView = 50;
	// The water's planar reflection, sky then world, into its own half-size
	// target before the frame proper (Render/WaterReflection.h).
	static constexpr bgfx::ViewId kRefractWorldView = 51;
	static constexpr bgfx::ViewId kReflectSkyView = 52;
	static constexpr bgfx::ViewId kReflectWorldView = 53;
	// The live environment cube map a level with RTCubeMap asks for: six
	// faces, sky then world each (Render/EnvCubeMap.h).
	static constexpr bgfx::ViewId kEnvCubeViewBase = 54;
	static constexpr bgfx::ViewId kEnvCubeViewCount = 12;
	// Sky is drawn next and owns the clear; the world paints over it.
	static constexpr bgfx::ViewId kSkyView = 66;
	static constexpr bgfx::ViewId kWorldView = 67;
	// The scene's half-size copy (Render/SceneTargets.h), for the passes
	// that sample the scene small.
	// The particle_warp sprites: a copy of the scene so far, then the sprites
	// drawn over the scene reading it (Render/ParticleRenderer.h, DrawWarp).
	static constexpr bgfx::ViewId kSceneCopyView = 68;
	static constexpr bgfx::ViewId kParticleWarpView = 69;
	// What goes on after the haze so it is not refracted: the view model, the
	// particles, the coronas. Only used on a frame with haze; otherwise they
	// draw in the world view as before.
	static constexpr bgfx::ViewId kAfterWarpView = 70;
	static constexpr bgfx::ViewId kSceneHalfView = 71;
	// Bloom (Render/Bloom.h): the bright pass, the two blurs, then the
	// composite that lands the scene on the backbuffer.
	static constexpr bgfx::ViewId kBloomBrightView = 72;
	static constexpr bgfx::ViewId kBloomBlurHView = 73;
	static constexpr bgfx::ViewId kBloomBlurVView = 74;
	static constexpr bgfx::ViewId kCompositeView = 75;
	// Demon Morph (Render/DemonFx.h): the scene to black and white, the
	// demonic models over it, the warp with the trail, the copy out.
	static constexpr bgfx::ViewId kDemonGrayView = 76;
	static constexpr bgfx::ViewId kDemonEntityView = 77;
	static constexpr bgfx::ViewId kDemonWarpView = 78;
	static constexpr bgfx::ViewId kDemonCopyView = 79;
	// pfsdfdebugclipmaps (Render/SdfClipmapDebug.h): the distance field
	// raymarched over the finished frame, under the 2D layer.
	static constexpr bgfx::ViewId kSdfDebugView = 80;
	// The sky for the distance field's rays (Render/SkyCapture.h): one face
	// drawn, then blitted for read-back in the view after it.
	static constexpr bgfx::ViewId kSkyCaptureView = 81;
	static constexpr bgfx::ViewId kSkyCaptureBlitView = 82;
	// The 2D layer, drawn over everything: no depth, in submission order.
	static constexpr bgfx::ViewId kHudView = 83;

	// A view's transform and rect from the camera, the way WorldRenderer::Draw
	// sets the world view's - for a view that draws with the same eye later.
	static void SetViewCamera(bgfx::ViewId view, const Camera& camera, int width, int height);

	~Renderer() { Shutdown(); }
	// Owns GPU handles that Shutdown destroys, so it is not copyable: a copy
	// would free them twice.
	Renderer() = default;
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	bool Init(Window& window);
	void Shutdown();

	// Called when the window size changes.
	void Resize(int width, int height);
	// Cfg.Multisample's sample count for the backbuffer (0, 2, 4, 6->8, 8,
	// 16); resets the device when it changes. A scene target rendered
	// elsewhere (Render/Bloom.h) reads it back to match.
	void SetMsaa(int samples);
	int msaaSamples() const { return msaa_; }

	// Background colour behind the sky - the level's fog colour, so the void
	// past the far clip reads as fog exactly like the original.
	void SetClearColor(float r, float g, float b);

	void BeginFrame();
	void EndFrame();
	// What bgfx::frame returned last: when a readTexture's data has arrived.
	uint32_t frameNumber() const { return frame_; }

	// Debug overlay text, one line per call, starting at the given row.
	void DebugText(uint16_t row, const char* fmt, ...);
	// Wireframes the GEOMETRY - every triangle the renderer submits, world and
	// entities alike. This is a different question from the collision overlay:
	// that one shows what physics thinks is there, this one shows what is
	// actually being drawn, and the interesting cases are where they disagree.
	void SetWireframe(bool on);
	// Asks bgfx to save the next frame to disk (written as a TGA).
	void RequestScreenshot(const std::string& path);


	// Human-readable name of the backend bgfx actually selected.
	std::string BackendName() const;

private:
	bool initialised_ = false;
	int width_ = 0, height_ = 0;
	int msaa_ = 0;
	uint32_t frame_ = 0;
};

} // namespace painful
