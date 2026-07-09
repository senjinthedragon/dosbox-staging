// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_MT32_LCD_RASTERIZER_H
#define DOSBOX_MT32_LCD_RASTERIZER_H

#include <cstdint>
#include <vector>

// Rasterizes a 20-character MT-32/CM-32L LCD text buffer (as returned by
// MIDI_GetActiveMt32DisplayState(), null-terminated, up to 21 bytes) into
// an RGBA pixel buffer approximating the real hardware's grey/blue-green
// dot-matrix LCD.
//
// The buffer isn't plain ASCII -- two reserved control bytes have special
// meaning on real hardware, reproduced here:
//   0x00 -- end of string (also treated as trailing space)
//   0x01 -- "part active" indicator (Mode_MAIN's per-channel bargraph) --
//           renders as a fully-lit block, not a printable character
//   0x02 -- duplicate pipe glyph, visually identical to ASCII '|' (0x7C)
//   > 0x7F -- defensively treated as space
// Everything else is passed through as ASCII against the font table in
// mt32_lcd_font.h.
//
// Each dot is rendered as a `dot_size_px`-by-`dot_size_px` block, with a
// fixed 1-pixel gap between every adjacent dot (matching real hardware,
// where individual dots stay visibly separated even within the same lit
// block) -- the gap does NOT scale with `dot_size_px`. The returned buffer
// is already at its final on-screen size; callers should display it at
// (at most) 1:1 scale, not scale it up further, or the fixed 1-pixel gap
// will scale too and stop being 1 pixel.
//
// Output stride equals width (tightly packed) -- unlike the Sound Canvas
// overlay's fixed 1024-pixel stride, callers must NOT assume a fixed
// stride here.
//
// Returns false if `buf` is null or empty (all-null), or if `dot_size_px`
// is 0.
bool RasterizeMt32Lcd(const char* buf, const uint32_t dot_size_px,
                      std::vector<uint32_t>& out_pixels, uint32_t& out_width,
                      uint32_t& out_height);

#endif // DOSBOX_MT32_LCD_RASTERIZER_H
