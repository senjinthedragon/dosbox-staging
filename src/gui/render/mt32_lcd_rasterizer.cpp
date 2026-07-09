// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mt32_lcd_rasterizer.h"

#include "private/mt32_lcd_font.h"

#include "utils/checks.h"

CHECK_NARROWING();

namespace {

constexpr int NumLcdChars = 20;

constexpr int MarginPx   = 1;
constexpr int GlyphGapPx = 1;

// Approximates the real MT-32/CM-32L's grey/blue-green calculator-style
// dot-matrix LCD: dark segments on a lighter background (the reverse of
// Sound Canvas's amber-backlit-with-dark-cutouts look).
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

constexpr uint32_t BackgroundPixel = make_pixel(0xa8, 0xc4, 0xb0);
constexpr uint32_t ForegroundPixel = make_pixel(0x20, 0x30, 0x2a);

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

} // namespace

bool RasterizeMt32Lcd(const char* buf, std::vector<uint32_t>& out_pixels,
                      uint32_t& out_width, uint32_t& out_height)
{
	if (!buf || buf[0] == '\0') {
		return false;
	}

	constexpr auto GlyphW = Mt32LcdFont::GlyphWidth;
	constexpr auto GlyphH = Mt32LcdFont::GlyphHeight;

	const int width  = MarginPx * 2 + NumLcdChars * GlyphW +
	                   (NumLcdChars - 1) * GlyphGapPx;
	const int height = MarginPx * 2 + GlyphH;

	out_width  = static_cast<uint32_t>(width);
	out_height = static_cast<uint32_t>(height);

	out_pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height),
	                  BackgroundPixel);

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

		const int glyph_x = MarginPx + char_index * (GlyphW + GlyphGapPx);

		for (int row = 0; row < GlyphH; ++row) {
			const auto row_bits = glyph[static_cast<size_t>(row)];

			for (int col = 0; col < GlyphW; ++col) {
				const bool lit = (row_bits >> (GlyphW - 1 - col)) & 1;
				if (!lit) {
					continue;
				}

				const int px = glyph_x + col;
				const int py = MarginPx + row;

				out_pixels[static_cast<size_t>(py) * static_cast<size_t>(width) +
				           static_cast<size_t>(px)] = ForegroundPixel;
			}
		}
	}

	return true;
}
