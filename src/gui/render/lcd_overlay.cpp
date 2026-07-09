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
uniform bool u_use_source_alpha;

void main()
{
	vec4 c = texture(u_texture, v_tex_coord);

	// Most sources (Sound Canvas) have no real alpha channel -- its
	// upstream pixel buffer's colours are plain 24-bit RGB literals with
	// no alpha bits ever set, and the reference implementation uploads
	// via SDL_PIXELFORMAT_BGR888, which has no alpha component at all and
	// always treats content as fully opaque. Sources that deliberately
	// encode real per-pixel transparency (e.g. the MT-32 rasterizer's
	// rounded-corner cutout) opt in via u_use_source_alpha.
	float src_alpha = u_use_source_alpha ? c.a : 1.0;
	frag_color = vec4(c.rgb, src_alpha * u_opacity);
}

#endif
)";
// clang-format on

} // namespace

LcdOverlay::LcdOverlay(bool _use_nearest_filtering, bool _use_source_alpha)
        : use_nearest_filtering(_use_nearest_filtering),
          use_source_alpha(_use_source_alpha)
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

	float overlay_w_px = 0.0f;
	float overlay_h_px = 0.0f;

	if (use_nearest_filtering) {
		// Pure 1:1 -- no scaling at all. Nearest-filtered content (the
		// rasterized MT-32 text) is expected to already be baked out at
		// its final on-screen size by the caller (dot size and the
		// fixed 1-pixel gap between dots are both real screen-pixel
		// quantities set via config, not something to be scaled here).
		// Any further integer upscale would scale that fixed gap right
		// along with everything else and defeat the point of it being
		// fixed.
		overlay_w_px = static_cast<float>(texture_width);
		overlay_h_px = static_cast<float>(texture_height);
	} else {
		const auto target_w_px = std::min(canvas_w * OverlayWidthFraction,
		                                  OverlayMaxWidthPx);
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
	shader.SetUniform1i("u_use_source_alpha", use_source_alpha ? 1 : 0);

	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);

	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
}

#endif // C_OPENGL
