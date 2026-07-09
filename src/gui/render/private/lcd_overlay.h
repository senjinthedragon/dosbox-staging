// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LCD_OVERLAY_H
#define DOSBOX_LCD_OVERLAY_H

#include "dosbox_config.h"

#if C_OPENGL

#include <cstdint>

#include "shader.h"

#include "utils/rect.h"

#include "glad/gl.h"

// Renders a synth's LCD status panel as a small, fixed, non-interactive
// overlay composited into the top-right corner of the game canvas, right
// after the shader pipeline's final pass. Content-agnostic -- used for both
// the Roland Sound Canvas LCD (see `soundcanvas_lcd_overlay`) and the
// Roland MT-32/CM-32L LCD (see `mt32_lcd_overlay`); aspect ratio is derived
// from whatever `width`/`height` is passed to `Render()`, not hardcoded.
class LcdOverlay {
public:
	// `use_nearest_filtering` should be true for low-resolution/pixel-art
	// sources (e.g. the rasterized MT-32 text display) to keep a crisp,
	// blocky look instead of blurring on upscale; false (bilinear) suits
	// the higher-resolution Sound Canvas pixel buffer.
	explicit LcdOverlay(bool use_nearest_filtering = false);
	~LcdOverlay();

	// prevent copying
	LcdOverlay(const LcdOverlay&) = delete;
	// prevent assignment
	LcdOverlay& operator=(const LcdOverlay&) = delete;

	// `pixels` is 0xAABBGGRR (i.e. R in the low byte, not B -- verified
	// against SDL_PIXELFORMAT_BGR888 used by the reference standalone
	// Nuked-SC55 app), one uint32_t per pixel, row 0 = top of the panel.
	// `row_stride_pixels` may exceed `width` -- it's the pixel count to
	// advance per row, not `width`. `canvas_size_px` is the current
	// window/canvas size, used to anchor the overlay to the top-right
	// corner. `opacity` is 0.0 (invisible) to 1.0 (opaque).
	void Render(const uint32_t* pixels, const uint32_t width,
	            const uint32_t height, const uint32_t row_stride_pixels,
	            const DosBox::Rect& canvas_size_px, const float opacity);

private:
	void EnsureInitialised();
	void UpdateTexture(const uint32_t* pixels, const uint32_t width,
	                   const uint32_t height, const uint32_t row_stride_pixels);
	void UpdateVertexData(const DosBox::Rect& canvas_size_px);

	bool is_initialised        = false;
	bool use_nearest_filtering = false;

	Shader shader = {};

	GLuint vao = 0;
	GLuint vbo = 0;

	GLuint texture          = 0;
	uint32_t texture_width  = 0;
	uint32_t texture_height = 0;
};

#endif // C_OPENGL

#endif // DOSBOX_LCD_OVERLAY_H
