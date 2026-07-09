// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "private/lcd_overlay.h"

#if C_OPENGL

#include <algorithm>
#include <array>
#include <cmath>

#include "utils/checks.h"

CHECK_NARROWING();

namespace {

constexpr float OverlayWidthFraction  = 0.22f;
constexpr float OverlayMaxWidthPx     = 480.0f;
constexpr float OverlayMarginFraction = 0.02f;

// clang-format off
constexpr auto ShaderSource = R"(
#version 330 core

#if defined(VERTEX)

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_tex_coord;

out vec2 v_tex_coord;

void main()
{
	gl_Position = vec4(a_position, 0.0, 1.0);
	v_tex_coord = a_tex_coord;
}

#elif defined(FRAGMENT)

in vec2 v_tex_coord;
out vec4 frag_color;

uniform sampler2D u_texture;
uniform float u_opacity;

void main()
{
	// Source has no real alpha channel (lcd.color1/color2 are plain 24-bit
	// RGB literals with no alpha bits set) -- the reference implementation
	// uploads via SDL_PIXELFORMAT_BGR888, which has no alpha component and
	// always treats content as fully opaque. Do the same here; only
	// u_opacity controls the overlay's overall transparency.
	vec4 c = texture(u_texture, v_tex_coord);
	frag_color = vec4(c.rgb, u_opacity);
}

#endif
)";
// clang-format on

} // namespace

LcdOverlay::LcdOverlay(bool _use_nearest_filtering)
        : use_nearest_filtering(_use_nearest_filtering)
{}

LcdOverlay::~LcdOverlay()
{
	if (!is_initialised) {
		return;
	}
	glDeleteTextures(1, &texture);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
	glDeleteProgram(shader.program_object);
}

void LcdOverlay::EnsureInitialised()
{
	if (is_initialised) {
		return;
	}

	if (!shader.BuildShaderProgram(ShaderSource)) {
		// BuildShaderProgram already logs the error. Mark as
		// initialised anyway so Render() doesn't retry every frame;
		// program_object stays 0, so subsequent draws are silently
		// skipped.
		is_initialised = true;
		return;
	}

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	// 4 vertices * (vec2 position + vec2 tex_coord), updated per-frame via
	// UpdateVertexData().
	constexpr auto NumVertices        = 4;
	constexpr auto NumFloatsPerVertex = 4;
	constexpr auto BufferSizeBytes    = NumVertices * NumFloatsPerVertex *
	                                    sizeof(GLfloat);

	glBufferData(GL_ARRAY_BUFFER, BufferSizeBytes, nullptr, GL_DYNAMIC_DRAW);

	constexpr auto StrideBytes = NumFloatsPerVertex * sizeof(GLfloat);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, StrideBytes, nullptr);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1,
	                      2,
	                      GL_FLOAT,
	                      GL_FALSE,
	                      StrideBytes,
	                      reinterpret_cast<void*>(2 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	const GLint filter_mode = use_nearest_filtering ? GL_NEAREST : GL_LINEAR;

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	is_initialised = true;
}

void LcdOverlay::UpdateTexture(const uint32_t* pixels, const uint32_t width,
                               const uint32_t height, const uint32_t row_stride_pixels)
{
	glBindTexture(GL_TEXTURE_2D, texture);

	if (width != texture_width || height != texture_height) {
		glTexImage2D(GL_TEXTURE_2D,
		             0,
		             GL_RGBA8,
		             static_cast<GLsizei>(width),
		             static_cast<GLsizei>(height),
		             0,
		             GL_RGBA,
		             GL_UNSIGNED_BYTE,
		             nullptr);

		texture_width  = width;
		texture_height = height;
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(row_stride_pixels));

	glTexSubImage2D(GL_TEXTURE_2D,
	                0,
	                0,
	                0,
	                static_cast<GLsizei>(width),
	                static_cast<GLsizei>(height),
	                GL_RGBA,
	                GL_UNSIGNED_INT_8_8_8_8_REV,
	                pixels);

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void LcdOverlay::UpdateVertexData(const DosBox::Rect& canvas_size_px)
{
	const auto canvas_w = canvas_size_px.w;
	const auto canvas_h = canvas_size_px.h;

	const auto target_w_px = std::min(canvas_w * OverlayWidthFraction,
	                                  OverlayMaxWidthPx);

	float overlay_w_px = 0.0f;
	float overlay_h_px = 0.0f;

	if (use_nearest_filtering) {
		// Scale by an integer multiple of the source texture's size so
		// every source pixel maps to a uniform whole-pixel block on
		// screen -- the same principle as DOSBox's own
		// `integer_scaling` setting for its main canvas. A non-integer
		// scale factor causes GL_NEAREST to duplicate some source pixel
		// columns/rows more than others (e.g. 3x here, 4x there), which
		// reads as blur/unevenness even though no texture interpolation
		// is actually happening -- most visible on small, fine-detail
		// sources like the rasterized MT-32 text, where each source
		// pixel is only a few screen pixels wide to begin with.
		//
		// Only applies to nearest-filtered content: bilinear-filtered
		// content (e.g. Sound Canvas's much higher-resolution panel,
		// which gets downscaled to fit rather than upscaled) already
		// looks smooth at any ratio, and forcing an integer scale there
		// would force it to its full native size whenever that's larger
		// than the target width.
		const auto pixel_scale = std::max(
		        1,
		        static_cast<int>(target_w_px /
		                         static_cast<float>(texture_width)));

		overlay_w_px = static_cast<float>(texture_width) *
		               static_cast<float>(pixel_scale);
		overlay_h_px = static_cast<float>(texture_height) *
		               static_cast<float>(pixel_scale);
	} else {
		const auto content_aspect_ratio = static_cast<float>(texture_width) /
		                                  static_cast<float>(texture_height);

		overlay_w_px = target_w_px;
		overlay_h_px = overlay_w_px / content_aspect_ratio;
	}

	// Round the margin and position to whole pixels too, so the overlay's
	// on-screen origin -- not just its size -- lands on an exact pixel
	// boundary.
	const auto margin_px = std::round(canvas_w * OverlayMarginFraction);

	const auto left_px   = std::round(canvas_w - margin_px - overlay_w_px);
	const auto right_px  = left_px + overlay_w_px;
	const auto top_px    = margin_px;
	const auto bottom_px = top_px + overlay_h_px;

	// Pixel space is y-down with origin top-left; NDC is y-up with origin
	// centre.
	const auto to_ndc_x = [&](const float px) {
		return (px / canvas_w) * 2.0f - 1.0f;
	};
	const auto to_ndc_y = [&](const float px) {
		return 1.0f - (px / canvas_h) * 2.0f;
	};

	const auto left_ndc   = to_ndc_x(left_px);
	const auto right_ndc  = to_ndc_x(right_px);
	const auto top_ndc    = to_ndc_y(top_px);
	const auto bottom_ndc = to_ndc_y(bottom_px);

	// Triangle strip: top-left, top-right, bottom-left, bottom-right.
	// Row 0 of the LCD pixel buffer is the top of the panel, and row 0
	// maps to texcoord v=0 per standard glTexImage2D semantics, so the
	// top vertices use v=0 with no flip needed.
	// clang-format off
	const std::array<GLfloat, 16> vertex_data = {
		left_ndc,  top_ndc,    0.0f, 0.0f,
		right_ndc, top_ndc,    1.0f, 0.0f,
		left_ndc,  bottom_ndc, 0.0f, 1.0f,
		right_ndc, bottom_ndc, 1.0f, 1.0f,
	};
	// clang-format on

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertex_data), vertex_data.data());
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void LcdOverlay::Render(const uint32_t* pixels, const uint32_t width,
                        const uint32_t height, const uint32_t row_stride_pixels,
                        const DosBox::Rect& canvas_size_px, const float opacity)
{
	if (!pixels || width == 0 || height == 0 || canvas_size_px.IsEmpty()) {
		return;
	}

	EnsureInitialised();

	UpdateTexture(pixels, width, height, row_stride_pixels);
	UpdateVertexData(canvas_size_px);

	// The shader pipeline's last pass sets the viewport to the (possibly
	// letterboxed/pillarboxed and offset) DOS-content rect, not the full
	// window -- our vertex data is computed in NDC relative to the full
	// canvas, so the viewport must be reset to match before drawing.
	glViewport(0,
	           0,
	           static_cast<GLsizei>(canvas_size_px.w),
	           static_cast<GLsizei>(canvas_size_px.h));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(shader.program_object);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	shader.SetUniform1i("u_texture", 0);
	shader.SetUniform1f("u_opacity", opacity);

	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);

	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
}

#endif // C_OPENGL
