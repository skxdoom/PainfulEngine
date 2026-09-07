#pragma once
#include "FontCache.h"
#include "TextureCache.h"

#include <bgfx/bgfx.h>
#include <string>
#include <vector>

namespace painful {

// The 2D layer: everything the scripts draw over the world.
//
// PainEngine's interface is drawn entirely from Lua - health, ammo, the tarot
// board, the menus, the console - through `HUD.DrawQuad` and `HUD.PrintXY`,
// with images coming from `MATERIAL.Create`. Coordinates are in PIXELS: the
// scripts do their own layout against `R3D.ScreenSize()`, scaling from the
// 1024x768 the interface was authored at.
//
// Everything batches into one vertex buffer per frame and is submitted in
// order, because 2D is order-dependent: a panel drawn after the text it sits
// behind would cover it.
class HudRenderer {
public:
	bool Init(const std::string& shaderDir, const std::string& fontsRoot);
	void Shutdown();
	// Every other renderer frees its GPU handles from its destructor; this one
	// relied on the process exiting.
	~HudRenderer() { Shutdown(); }
	HudRenderer() = default;
	HudRenderer(const HudRenderer&) = delete;
	HudRenderer& operator=(const HudRenderer&) = delete;
	bool ready() const { return bgfx::isValid(program_); }

	// A material is a texture the scripts hold by handle. Handle 0 is "no
	// texture", which the scripts pass to draw a solid colour.
	using Material = int;
	Material CreateMaterial(const std::string& name, TextureCache& textures,
			const std::string& texturesRoot);
	void ReleaseMaterial(Material m);
	bool MaterialSize(Material m, int& w, int& h) const;

	// abgr is the packed colour the scripts build with R3D.RGB / R3D.RGBA -
	// our own native, so the byte order is ours on both ends.
	void Quad(Material m, float x, float y, float w, float h, uint32_t abgr,
			float u1 = 0.f, float v1 = 0.f, float u2 = 1.f, float v2 = 1.f);
	// Rotated about an arbitrary screen point - not the quad's own centre. The
	// compass needle is drawn this way: the quad sits at one place and turns
	// about the dial's hub a few pixels away.
	void QuadRotated(Material m, float x, float y, float w, float h, float radians,
			float pivotX, float pivotY, uint32_t abgr);
	// Four corners already placed on screen: top-left, top-right,
	// bottom-right, bottom-left, as x,y pairs.
	void QuadCorners(Material m, const float xy[8], uint32_t abgr);
	// Repeats a texture at its NATIVE size across a rectangle rather than
	// stretching it - HUD::DrawTiles in the original, and how every piece of
	// the menu frame is drawn. A width or height of 0 means "one texture
	// wide/tall", which is what lets an edge tile along a single axis.
	void Tiles(Material m, float x, float y, float w, float h, uint32_t abgr = 0xffffffffu);

	// Returns the width drawn, so the caller can advance a cursor. An x of -1
	// centres the string on the screen, which is what the scripts pass when
	// they want a banner.
	// patternMaterial is the font TEXTURE the menu rows carry
	// (PMENU.SetItemFontsTex); 0 leaves the glyphs their plain colour.
	float Text(const std::string& fontName, int size, float x, float y,
			const std::string& text, uint32_t abgr, Material patternMaterial = 0);
	float TextWidth(const std::string& fontName, int size, const std::string& text);
	float TextHeight(const std::string& fontName, int size);

	// Starts a frame's batch; everything drawn afterwards lands in this view.
	void Begin(bgfx::ViewId view, int screenW, int screenH);
	void End();

	// Widescreen. The interface is authored for 4:3 and the scripts lay it
	// out against R3D.ScreenSize, so on a wider window they are handed a 4:3
	// CANVAS - the window's height, and 4/3 of it wide - and every draw is
	// mapped from canvas to screen here. Docs/Reference/Hud.md, "Widescreen".
	enum class Aspect {
		kStretch, // the original: canvas = window, stretched
		kCentered, // the canvas centred, empty sides
		kAnchored, // left third to the left edge, right third to the right,
				// the middle centred - decided per draw by its centre
	};
	void SetAspect(Aspect a) { aspect_ = a; }
	Aspect aspect() const { return aspect_; }
	// The canvas the scripts should see for the current window.
	int canvasWidth() const { return canvasW_; }
	int canvasHeight() const { return canvasH_; }
	// Whether coordinates are canvas (true) or raw window pixels (false).
	// The console and the debug overlay draw in window pixels.
	void UseCanvas(bool on) { useCanvas_ = on; }
	// Anchoring off: everything centred, for the menus, which are one 4:3
	// composition rather than corner-anchored readouts.
	void UseAnchoring(bool on) { anchoring_ = on; }
	// Full-window artwork: scaled to cover the window, cropped, never
	// stretched - the loading art.
	void Cover(Material m, uint32_t abgr = 0xffffffffu);
	// The window outside the canvas, in a flat colour: the sides of a wide
	// window while a menu is up.
	void FillOutsideCanvas(uint32_t abgr);
	// One anchor for several draws - a frame's four edges, a panel and its
	// border - decided once from the group's centre, so the pieces stay
	// together whichever third each one falls in.
	void BeginGroup(float centreX);
	void EndGroup() { grouped_ = false; }
	// Where canvas x 0 lands in the window when everything is centred: what
	// a window-pixel mouse position needs taken off to become a canvas one.
	float CanvasOffsetX() const;

	size_t quadsThisFrame() const { return quads_; }
	size_t drawCalls() const { return drawCalls_; }
	const FontCache& fonts() const { return fonts_; }

private:
	struct Vertex {
		float x, y, z;
		uint32_t abgr;
		float u, v;
	};
	struct Batch {
		bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
		// The second stage, for textured text. White for everything else, so a
		// run only splits when the PAIR changes.
		bgfx::TextureHandle pattern = BGFX_INVALID_HANDLE;
		float patternW = 1.f, patternH = 1.f;
		uint32_t first = 0, count = 0;
	};

	void Push(bgfx::TextureHandle tex, const Vertex* quad,
			bgfx::TextureHandle pattern = BGFX_INVALID_HANDLE, float patternW = 1.f,
			float patternH = 1.f);
	void Flush();
	// The screen x offset a canvas draw centred at `centreX` gets. Zero when
	// the canvas is the window (stretch mode, or a window no wider than 4:3).
	float OffsetFor(float centreX) const;

	Aspect aspect_ = Aspect::kAnchored;
	int canvasW_ = 0, canvasH_ = 0;
	bool useCanvas_ = true;
	bool anchoring_ = true;
	bool grouped_ = false;
	float groupOffset_ = 0.f;

	bgfx::VertexLayout layout_;
	bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sDiffuse_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sPattern_ = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle uParams_ = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;

	struct Mat {
		bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
		int w = 0, h = 0;
		bool used = false;
	};
	std::vector<Mat> materials_;

	FontCache fonts_;
	std::vector<Vertex> verts_;
	std::vector<Batch> batches_;
	bgfx::ViewId view_ = 0;
	int screenW_ = 0, screenH_ = 0;
	size_t quads_ = 0, drawCalls_ = 0;
	bool active_ = false;
};

} // namespace painful
