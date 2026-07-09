// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LCD_OVERLAY_H
#define DOSBOX_LCD_OVERLAY_H

#include "dosbox_config.h"

#if C_OPENGL

#include <cstdint>
#include <string_view>

#include "shader.h"

#include "utils/rect.h"

#include "glad/gl.h"

// Which corner of the game canvas the overlay is anchored to. Shared by both
// the Sound Canvas and MT-32 overlays (see `soundcanvas_lcd_overlay_position`
// and `mt32_lcd_overlay_position`).
enum class LcdOverlayPosition { TopRight, TopLeft, BottomRight, BottomLeft };

// Parses one of "top-right", "top-left", "bottom-right", "bottom-left"
// (matching the config setting's valid values); falls back to TopRight for
// any other input.
LcdOverlayPosition ParseLcdOverlayPosition(const std::string_view value);

// Runtime-toggleable visibility shared by both the Sound Canvas and MT-32
// overlays -- lets a single hotkey hide/show whichever one is currently
// active, independent of the `*_lcd_overlay` config setting (which only
// controls whether the feature is enabled at all). Defaults to visible.
bool LcdOverlaysAreVisible();
void ToggleLcdOverlaysVisible(bool pressed);

// Renders a synth's LCD status panel as a small, fixed, non-interactive
// overlay composited into a corner of the game canvas, right after the
// shader pipeline's final pass. Content-agnostic -- used for both the Roland
// Sound Canvas LCD (see `soundcanvas_lcd_overlay`) and the Roland MT-32/
// CM-32L LCD (see `mt32_lcd_overlay`); aspect ratio is derived from whatever
// `width`/`height` is passed to `Render()`, not hardcoded.
class LcdOverlay {
public:
	// `use_nearest_filtering` should be true for low-resolution/pixel-art
	// sources (e.g. the rasterized MT-32 text display) to keep a crisp,
	// blocky look instead of blurring on upscale; false (bilinear) suits
	// the higher-resolution Sound Canvas pixel buffer.
	//
	// `use_source_alpha`: when false (default), every pixel renders fully
	// opaque regardless of its alpha byte -- needed for Sound Canvas,
	// whose upstream pixel buffer has no meaningful alpha channel at all
	// (see lcd_overlay.cpp's fragment shader comment). Set true only for
	// sources that deliberately encode real per-pixel transparency (e.g.
	// the MT-32 rasterizer's rounded-corner cutout).
	//
	// `position` selects which corner of the canvas the overlay is
	// anchored to.
	explicit LcdOverlay(bool use_nearest_filtering = false,
	                    bool use_source_alpha      = false,
	                    LcdOverlayPosition position = LcdOverlayPosition::TopRight);
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
	// window/canvas size, used to anchor the overlay to its configured
	// corner. `opacity` is 0.0 (invisible) to 1.0 (opaque).
	void Render(const uint32_t* pixels, const uint32_t width,
	            const uint32_t height, const uint32_t row_stride_pixels,
	            const DosBox::Rect& canvas_size_px, const float opacity);

private:
	void EnsureInitialised();
	void UpdateTexture(const uint32_t* pixels, const uint32_t width,
	                   const uint32_t height, const uint32_t row_stride_pixels);
	void UpdateVertexData(const DosBox::Rect& canvas_size_px);

	bool is_initialised         = false;
	bool use_nearest_filtering  = false;
	bool use_source_alpha       = false;
	LcdOverlayPosition position = LcdOverlayPosition::TopRight;

	Shader shader = {};

	GLuint vao = 0;
	GLuint vbo = 0;

	GLuint texture          = 0;
	uint32_t texture_width  = 0;
	uint32_t texture_height = 0;
};

#endif // C_OPENGL

#endif // DOSBOX_LCD_OVERLAY_H
