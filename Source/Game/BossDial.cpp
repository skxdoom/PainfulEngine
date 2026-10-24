#include "BossDial.h"

#include <cstdlib>

namespace painful {

namespace {

void Mark(uint8_t* rgba, int w, int h, int x, int y) {
	if (x >= 0 && x < w && y >= 0 && y < h) rgba[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] = 1;
}

// HUD::ClearAlphaLine (0x1008b3b0): Bresenham from (x0,y0) to (x1,y1), both
// ends marked. Texels off the image are skipped; the original's surface is
// padded, and its longest line runs past the image edge.
void ClearLine(uint8_t* rgba, int w, int h, int x0, int y0, int x1, int y1) {
	const int adx = std::abs(x1 - x0), ady = std::abs(y1 - y0);
	const int sx = x1 >= x0 ? 1 : -1, sy = y1 >= y0 ? 1 : -1;
	int x = x0, y = y0;
	Mark(rgba, w, h, x, y);
	if (ady < adx) {
		int err = -adx;
		while (x != x1 || y != y1) {
			err += ady * 2;
			if (err >= 0) { y += sy; err -= adx * 2; }
			x += sx;
			Mark(rgba, w, h, x, y);
		}
	} else {
		int err = -ady;
		while (x != x1 || y != y1) {
			err += adx * 2;
			if (err >= 0) { x += sx; err -= ady * 2; }
			y += sy;
			Mark(rgba, w, h, x, y);
		}
	}
}

// HUD::ClearAlphaQuad (0x1008b490): the rectangle, ends included, alpha
// lowered to 1 where it is higher.
void ClearQuad(uint8_t* rgba, int w, int x0, int y0, int x1, int y1) {
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x) {
			uint8_t& a = rgba[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3];
			if (a > 1) a = 1;
		}
}

// trunc(n * 0.04f * k), as the x87 computes it: 0.04f is a little under 0.04,
// so a whole quarter's reach lands just below its integer and truncates down.
int Reach(int n, int k) { return int(double(n) * double(0.04f) * double(k)); }

} // namespace

void BossDialWipe(uint8_t* rgba, int w, int h, int percent) {
	const int lost = 100 - percent;
	const int cx = w / 2, cy = h / 2;
	auto line = [&](int x, int y) { ClearLine(rgba, w, h, cx, cy, x, y); };
	// Eight segments, two to a quarter. Each finished quarter is cleared as a
	// rectangle; the current one as a fan of lines to its edge.
	if (lost < 25) {
		if (lost <= 13) {
			const int end = cx + Reach(w, lost) - 1;
			for (int x = cx; x <= end; ++x) line(x, 0);
		} else {
			for (int x = cx; x < w; ++x) line(x, 0);
			const int n = Reach(h, lost - 13);
			for (int y = 0; y < n; ++y) line(w - 1, y);
		}
	} else if (lost < 50) {
		ClearQuad(rgba, w, cx, 0, w - 1, cy - 1);
		if (lost <= 38) {
			const int end = cy + Reach(h, lost - 25) - 1;
			for (int y = cy; y <= end; ++y) line(w - 1, y);
		} else {
			for (int y = cy; y < h; ++y) line(w - 1, y);
			const int stop = w - Reach(w, lost - 38);
			for (int x = w - 1; x > stop; --x) line(x, h - 1);
		}
	} else if (lost < 75) {
		ClearQuad(rgba, w, cx, 0, w - 1, h - 1);
		if (lost <= 63) {
			const int stop = cx - Reach(w, lost - 50);
			for (int x = cx; x >= stop; --x) line(x, h - 1);
		} else {
			for (int x = cx; x >= 0; --x) line(x, h - 1);
			const int stop = h - Reach(h, lost - 63);
			for (int y = h - 1; y > stop; --y) line(0, y);
		}
	} else {
		ClearQuad(rgba, w, cx, 0, w - 1, h - 1);
		ClearQuad(rgba, w, 0, cy, cx - 1, h - 1);
		if (lost <= 88) {
			const int stop = cy - Reach(h, lost - 75);
			for (int y = cy; y > stop; --y) line(0, y);
		} else {
			for (int y = cy; y >= 0; --y) line(0, y);
			const int n = Reach(w, lost - 88);
			for (int x = 0; x < n; ++x) line(x, 0);
		}
	}
}

} // namespace painful
