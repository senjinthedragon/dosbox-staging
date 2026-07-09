// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mt32_lcd_rasterizer.h"

#include <algorithm>

#include "private/mt32_lcd_font.h"

#include "utils/checks.h"

CHECK_NARROWING();

namespace {

constexpr int NumLcdChars = 20;

// Fixed regardless of dot size -- real MT-32/CM-32L dots stay visibly
// separated even within the same lit block, and that gap doesn't get
// proportionally wider on a bigger LCD, so it shouldn't scale with
// dot_size_px here either.
constexpr int GapPx = 1;

// Real character LCD modules (this display is built on a controller in the
// same family as the ubiquitous HD44780) reserve an extra row below each
// character cell for a hardware cursor underline. The MT-32 firmware never
// uses it, so it's always background immediately below the glyph, then a
// row of dots that stay permanently unlit -- still visibly part of the dot
// grid, not skipped/blank.
constexpr auto GlyphW       = Mt32LcdFont::GlyphWidth;
constexpr auto GlyphH       = Mt32LcdFont::GlyphHeight;
constexpr auto CursorGapRow = GlyphH;
constexpr auto CellRows     = GlyphH + 2;

// Approximates the real MT-32/CM-32L's dot-matrix LCD, which shows three
// distinct shades: a dark panel background (visible only in the gaps
// between dots and the margin), unlit dots (a dark green close to but
// distinguishable from the background -- the full dot grid stays faintly
// visible even when off), and lit dots (bright green).
constexpr uint32_t make_pixel(uint8_t r, uint8_t g, uint8_t b)
{
	constexpr uint8_t FullyOpaque = 0xFF;
	// 0xAABBGGRR -- see lcd_overlay.h for why (matches Sound Canvas's
	// buffer convention that LcdOverlay already uploads via
	// GL_RGBA + GL_UNSIGNED_INT_8_8_8_8_REV).
	return (static_cast<uint32_t>(FullyOpaque) << 24) |
	       (static_cast<uint32_t>(b) << 16) |
	       (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
}

constexpr uint32_t BackgroundPixel  = make_pixel(0x42, 0x94, 0x00);
constexpr uint32_t InactiveDotPixel = make_pixel(0xa0, 0xc6, 0x1a);
constexpr uint32_t ActiveDotPixel   = make_pixel(0xe2, 0xfb, 0x05);

// Fully transparent (including alpha) -- used to cut the panel's rounded
// corners. Only has any visible effect when the host LcdOverlay instance
// was constructed with use_source_alpha=true.
constexpr uint32_t TransparentPixel = 0x00000000;

int glyph_index_for_byte(const uint8_t byte)
{
	if (byte == 0x01) {
		return Mt32LcdFont::BlockGlyphIndex;
	}
	if (byte == 0x02) {
		return static_cast<int>('|') - Mt32LcdFont::FirstAsciiCodePoint;
	}
	if (byte < Mt32LcdFont::FirstAsciiCodePoint ||
	    byte > Mt32LcdFont::LastAsciiCodePoint) {
		return static_cast<int>(' ') - Mt32LcdFont::FirstAsciiCodePoint;
	}
	return static_cast<int>(byte) - Mt32LcdFont::FirstAsciiCodePoint;
}

void fill_dot(std::vector<uint32_t>& pixels, const int width, const int x,
              const int y, const int dot_size_px, const uint32_t color)
{
	for (int dy = 0; dy < dot_size_px; ++dy) {
		for (int dx = 0; dx < dot_size_px; ++dx) {
			const auto px                   = x + dx;
			const auto py                   = y + dy;
			pixels[static_cast<size_t>(py) * static_cast<size_t>(width) +
			       static_cast<size_t>(px)] = color;
		}
	}
}

// Cuts the four corners of the panel into a rounded shape by making
// whatever falls outside the inscribed quarter-circle at each corner fully
// transparent -- gives the whole overlay a rounded-rectangle silhouette
// instead of square corners.
void round_corners(std::vector<uint32_t>& pixels, const int width,
                   const int height, const int radius)
{
	if (radius <= 0) {
		return;
	}

	const auto is_outside_circle = [&](const int dx, const int dy) {
		// dx/dy are the offsets from the corner's inscribed circle
		// centre; a corner pixel is cut if it's further than `radius`
		// from that centre.
		return (dx * dx + dy * dy) > (radius * radius);
	};

	for (int y = 0; y < radius; ++y) {
		for (int x = 0; x < radius; ++x) {
			const int dx = radius - 1 - x;
			const int dy = radius - 1 - y;
			if (!is_outside_circle(dx, dy)) {
				continue;
			}

			// Top-left
			pixels[static_cast<size_t>(y) * static_cast<size_t>(width) +
			       static_cast<size_t>(x)] = TransparentPixel;

			// Top-right
			pixels[static_cast<size_t>(y) * static_cast<size_t>(width) +
			       static_cast<size_t>(width - 1 - x)] = TransparentPixel;

			// Bottom-left
			pixels[static_cast<size_t>(height - 1 - y) * static_cast<size_t>(width) +
			       static_cast<size_t>(x)] = TransparentPixel;

			// Bottom-right
			pixels[static_cast<size_t>(height - 1 - y) * static_cast<size_t>(width) +
			       static_cast<size_t>(width - 1 - x)] = TransparentPixel;
		}
	}
}

// Fixed layout derived purely from dot_size_px -- the same for every frame
// as long as the dot size setting doesn't change.
struct Layout {
	int dot_px           = 0;
	int glyph_w_px       = 0;
	int glyph_gap_px     = 0;
	int margin_x_px      = 0;
	int margin_top_px    = 0;
	int margin_bottom_px = 0;
	int width            = 0;
	int height           = 0;
};

Layout compute_layout(const int dot_px)
{
	Layout layout;
	layout.dot_px = dot_px;

	// A glyph is GlyphW dots wide and CellRows dots tall (including the
	// cursor gap/dot rows), each dot_px wide/tall, separated by GapPx
	// between adjacent dots (but not trailing past the last dot).
	layout.glyph_w_px = GlyphW * dot_px + (GlyphW - 1) * GapPx;

	// Adjacent glyphs (and their underline rows) are separated by a full
	// dot-width of plain background -- distinct from GapPx, which is the
	// much narrower gap between dots within the same glyph.
	layout.glyph_gap_px = dot_px;

	const int glyph_only_h_px = GlyphH * dot_px + (GlyphH - 1) * GapPx;
	const int glyph_h_px      = CellRows * dot_px + (CellRows - 1) * GapPx;

	// Border around the whole display: one character width on the left
	// and right, half a character's height (the glyph only, not counting
	// the cursor gap/dot rows) on the top -- the bottom border is
	// intentionally one dot shorter than the top, to match the reference
	// display's slightly asymmetric look.
	layout.margin_x_px      = layout.glyph_w_px;
	layout.margin_top_px    = glyph_only_h_px / 2;
	layout.margin_bottom_px = std::max(0, layout.margin_top_px - dot_px);

	layout.width = layout.margin_x_px * 2 + NumLcdChars * layout.glyph_w_px +
	               (NumLcdChars - 1) * layout.glyph_gap_px;
	layout.height = layout.margin_top_px + layout.margin_bottom_px + glyph_h_px;

	return layout;
}

// Renders the background, the full dot grid at its unlit colour (identical
// for every character position regardless of what's actually displayed --
// only which dots get upgraded to "active" varies per frame), the cursor
// gap/dot rows, and the rounded corners. This is the same for every frame
// as long as dot_size_px doesn't change, so it's cached and only rebuilt
// when the layout changes.
std::vector<uint32_t> build_base_image(const Layout& layout)
{
	std::vector<uint32_t> pixels(static_cast<size_t>(layout.width) *
	                                     static_cast<size_t>(layout.height),
	                             BackgroundPixel);

	for (int char_index = 0; char_index < NumLcdChars; ++char_index) {
		const int glyph_x = layout.margin_x_px +
		                    char_index * (layout.glyph_w_px +
		                                  layout.glyph_gap_px);

		for (int row = 0; row < CellRows; ++row) {
			const int dot_y = layout.margin_top_px +
			                  row * (layout.dot_px + GapPx);

			const auto color = (row == CursorGapRow)
			                         ? BackgroundPixel
			                         : InactiveDotPixel;

			for (int col = 0; col < GlyphW; ++col) {
				const int dot_x = glyph_x +
				                  col * (layout.dot_px + GapPx);
				fill_dot(pixels,
				         layout.width,
				         dot_x,
				         dot_y,
				         layout.dot_px,
				         color);
			}
		}
	}

	// Tighter than the full margin -- a full-margin radius looked too
	// loose/round; two thirds of it gives a crisper rounded-rectangle
	// silhouette.
	const int corner_radius = std::min(layout.margin_x_px,
	                                   layout.margin_bottom_px) *
	                          2 / 3;
	round_corners(pixels, layout.width, layout.height, corner_radius);

	return pixels;
}

} // namespace

bool RasterizeMt32Lcd(const char* buf, const uint32_t dot_size_px,
                      std::vector<uint32_t>& out_pixels, uint32_t& out_width,
                      uint32_t& out_height)
{
	if (!buf || buf[0] == '\0' || dot_size_px == 0) {
		return false;
	}

	const int dot_px = static_cast<int>(dot_size_px);

	static Layout cached_layout                    = {};
	static std::vector<uint32_t> cached_base_image = {};

	if (cached_layout.dot_px != dot_px) {
		cached_layout     = compute_layout(dot_px);
		cached_base_image = build_base_image(cached_layout);
	}

	const auto& layout = cached_layout;

	out_width  = static_cast<uint32_t>(layout.width);
	out_height = static_cast<uint32_t>(layout.height);

	out_pixels = cached_base_image;

	bool end_of_string = false;

	for (int char_index = 0; char_index < NumLcdChars; ++char_index) {
		const auto raw_byte = static_cast<uint8_t>(buf[char_index]);

		if (raw_byte == 0x00) {
			end_of_string = true;
		}

		const auto glyph_index = end_of_string
		                               ? static_cast<int>(' ') -
		                                         Mt32LcdFont::FirstAsciiCodePoint
		                               : glyph_index_for_byte(raw_byte);

		const auto& glyph = Mt32LcdFont::Glyphs[static_cast<size_t>(glyph_index)];

		const int glyph_x = layout.margin_x_px +
		                    char_index * (layout.glyph_w_px +
		                                  layout.glyph_gap_px);

		for (int row = 0; row < GlyphH; ++row) {
			const auto row_bits = glyph[static_cast<size_t>(row)];
			const int dot_y     = layout.margin_top_px +
			                      row * (dot_px + GapPx);

			for (int col = 0; col < GlyphW; ++col) {
				const bool lit = (row_bits >> (GlyphW - 1 - col)) & 1;
				if (!lit) {
					// Base image already has this dot at
					// its unlit colour.
					continue;
				}

				const int dot_x = glyph_x + col * (dot_px + GapPx);

				fill_dot(out_pixels,
				         layout.width,
				         dot_x,
				         dot_y,
				         dot_px,
				         ActiveDotPixel);
			}
		}
	}

	return true;
}
