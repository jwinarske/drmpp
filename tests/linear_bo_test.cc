/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

extern "C" {
#include "bs_drm.h"
}

static constexpr useconds_t test_case_display_usec = 2000000;

struct test_case {
	uint32_t format; /* format for allocating buffer object from GBM*/
	uint32_t fb_format; /* format used to create DRM framebuffer, 0 indicates same as format */
	gbm_bo_flags usage;
};

const char *format_to_string(const uint32_t format) {
	switch (format) {
		case GBM_FORMAT_XRGB8888:
			return "GBM_FORMAT_XRGB8888";
		case GBM_FORMAT_ARGB8888:
			return "GBM_FORMAT_ARGB8888";
		default:
			return "GBM_FORMAT_????????";
	}
}

static void test_case_print(FILE *out, const test_case *tcase) {
	fprintf(out, "format=%s usage=", format_to_string(tcase->format));
	bool first = true;
	if (tcase->usage & GBM_BO_USE_SCANOUT) {
		fprintf(out, "GBM_BO_USE_SCANOUT");
		first = false;
	}
	if (tcase->usage & GBM_BO_USE_RENDERING) {
		fprintf(out, "%sGBM_BO_USE_RENDERING", first ? "" : " | ");
		first = false;
	}
	if (tcase->usage & GBM_BO_USE_LINEAR) {
		fprintf(out, "%sGBM_BO_USE_LINEAR", first ? "" : " | ");
	}
	if (tcase->fb_format) {
		fprintf(out, " fb_format=%s", format_to_string(tcase->fb_format));
	}
}

static void test_case_colors(const test_case *tcase,
                             float *colors /* sizeof(colors) / sizeof(colors[0]) == 9 */) {
	colors[0] = 0.0f;
	colors[1] = 1.0f;
	colors[2] = 0.0f;

	colors[3] = tcase->usage & GBM_BO_USE_SCANOUT ? 1.0f : 0.0f;
	colors[4] = tcase->usage & GBM_BO_USE_RENDERING ? 0.66f : 0.0f;
	colors[5] = tcase->usage & GBM_BO_USE_LINEAR ? 1.0f : 0.0f;

	switch (tcase->format) {
		case GBM_FORMAT_XRGB8888:
			colors[6] = 1.0f;
			colors[7] = 1.0f;
			break;
		case GBM_FORMAT_ARGB8888:
			colors[7] = 1.0f;
			colors[8] = 1.0f;
			break;
		default:
			colors[6] = 0.33f;
			colors[7] = 0.33f;
			colors[8] = 0.33f;
			break;
	}
}

static void bo_lines(const uint32_t height, float *lines /* sizeof(lines) / sizeof(lines[0]) == 9 */) {
	/*
	The screen is divided into sections using 3 lines as shown.
	*----------*
	|\ | /     |
	| \|/      |
	|  X       |
	| /|\      |
	|/ | \     |
	*----------*

	Lines are evaluated as positive or negative in the linear equation:
	  Ax + By - C

	Where the coffecicents A, B, and C appear in the array in the following order:
	[ A,   B,   C ]
	*/
	// negative left of the following lines' intersection
	lines[0] = 1;
	lines[1] = 0;
	lines[2] = static_cast<float>(height) / 2;

	// negative on lower-right triangle section
	lines[3] = 1;
	lines[4] = -1;
	lines[5] = 0;

	// negative on upper-left triangle section
	lines[6] = 1;
	lines[7] = 1;
	lines[8] = static_cast<float>(height);
}

static bool test_case_draw_gl(bs_egl *egl, const test_case *tcase, gbm_bo *bo) {
	bool success = true;
	const uint32_t width = gbm_bo_get_width(bo);
	const uint32_t height = gbm_bo_get_height(bo);

	EGLImageKHR image = bs_egl_image_create_gbm(egl, bo);
	if (image == EGL_NO_IMAGE_KHR) {
		success = false;
		bs_debug_error("failed to make image from buffer object");
		return success;
	}

	bs_egl_fb *fb = bs_egl_fb_new(egl, image);
	if (!fb) {
		success = false;
		bs_debug_error("failed to make rednering framebuffer for buffer object");
		bs_egl_image_destroy(egl, &image);
		return success;
	}

	const auto vert =
			"attribute vec2 vPosition;\n"
			"void main() {\n"
			"  gl_Position = vec4(vPosition, 0, 1);\n"
			"}\n";

	const auto frag =
			"precision mediump float;\n"
			"uniform vec3 uColors[3];\n"
			"uniform vec3 uLines[3];\n"
			"void main() {\n"
			"  bool left = dot(uLines[0].xy, gl_FragCoord.xy) < uLines[0].z;\n"
			"  bool lower_right = dot(uLines[1].xy, gl_FragCoord.xy) < uLines[1].z;\n"
			"  bool upper_left = dot(uLines[2].xy, gl_FragCoord.xy) < uLines[2].z;\n"
			"  if (left && upper_left)\n"
			"    gl_FragColor = vec4(uColors[0], 1.0);\n"
			"  else if ((left && !upper_left) || (!left && lower_right))\n"
			"    gl_FragColor = vec4(uColors[1], 1.0);\n"
			"  else\n"
			"    gl_FragColor = vec4(uColors[2], 1.0);\n"
			"}\n";

	bs_gl_program_create_binding bindings[] = {
		{0, "vPosition"}, {0, nullptr},
	};

	const GLuint program = bs_gl_program_create_vert_frag_bind(vert, frag, bindings);
	if (!program) {
		success = false;
		bs_debug_error("failed to compile test case shader program");
		bs_egl_fb_destroy(&fb);
		bs_egl_image_destroy(egl, &image);
		return success;
	}
	const GLint colors_location = glGetUniformLocation(program, "uColors");
	const GLint lines_location = glGetUniformLocation(program, "uLines");
	if (colors_location == -1 || lines_location == -1) {
		success = false;
		bs_debug_error("failed to retrieve uniform location");
		glDeleteProgram(program);
		bs_egl_fb_destroy(&fb);
		bs_egl_image_destroy(egl, &image);
		return success;
	}

	GLfloat colors[9];
	test_case_colors(tcase, colors);

	float lines[9];
	bo_lines(height, lines);

	constexpr GLfloat verts[] = {
		// clang-format off
		-1.0f, -1.0f,
		2.0f, -1.0f,
		-1.0f, 2.0f,
		2.0f, 2.0f,
		// clang-format on
	};

	glBindFramebuffer(GL_FRAMEBUFFER, bs_egl_fb_name(fb));
	glViewport(0, 0, static_cast<GLint>(width), static_cast<GLint>(height));

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(program);
	glUniform3fv(colors_location, 3, colors);
	glUniform3fv(lines_location, 3, lines);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glFinish();

	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDeleteProgram(program);
	bs_egl_fb_destroy(&fb);
	bs_egl_image_destroy(egl, &image);
	return success;
}

static bool test_case_draw_dma_buf(const test_case *tcase, bs_mapper *mapper,
                                   gbm_bo *bo) {
	if (const int bo_fd = gbm_bo_get_fd(bo); bo_fd < 0) {
		bs_debug_error("failed to get fd of bo");
		return false;
	}
	const uint32_t width = gbm_bo_get_width(bo);
	const uint32_t height = gbm_bo_get_height(bo);
	uint32_t stride;
	void *map_data;
	auto *ptr = static_cast<uint8_t *>(bs_mapper_map(mapper, bo, 0, &map_data, &stride));
	if (ptr == MAP_FAILED) {
		bs_debug_error("failed to mmap gbm bo");
		return false;
	}

	float colors_float[9];
	test_case_colors(tcase, colors_float);
	uint8_t colors[9];
	for (size_t i = 0; i < 9; i++)
		colors[i] = static_cast<uint8_t>(colors_float[i] * 255.0f);

	float lines[9];
	bo_lines(height, lines);

	for (uint32_t y = 0; y < height; y++) {
		uint8_t *row_ptr = &ptr[y * stride];
		for (uint32_t x = 0; x < width; x++) {
			const bool left = lines[0] * static_cast<float>(x) + lines[1] * static_cast<float>(y) < lines[2];
			const bool lower_right = lines[3] * static_cast<float>(x) + lines[4] * static_cast<float>(y) < lines[5];
			const bool upper_left = lines[6] * static_cast<float>(x) + lines[7] * static_cast<float>(y) < lines[8];

			int color_index = 0;
			if (left && upper_left)
				color_index = 0;
			else if ((left && !upper_left) || (!left && lower_right))
				color_index = 1;
			else
				color_index = 2;

			row_ptr[x * 4 + 0] = colors[color_index * 3 + 2];
			row_ptr[x * 4 + 1] = colors[color_index * 3 + 1];
			row_ptr[x * 4 + 2] = colors[color_index * 3 + 0];
			row_ptr[x * 4 + 3] = 0;
		}
	}

	bs_mapper_unmap(mapper, bo, map_data);

	return true;
}

int main(int argc, char **argv) {
	constexpr test_case tcases[] = {
		{GBM_FORMAT_XRGB8888, 0, static_cast<enum gbm_bo_flags>(GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)},
		{GBM_FORMAT_XRGB8888, 0, static_cast<enum gbm_bo_flags>(GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR)},
		{
			GBM_FORMAT_ARGB8888, GBM_FORMAT_XRGB8888,
			static_cast<enum gbm_bo_flags>(GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING)
		},
		{
			GBM_FORMAT_ARGB8888, GBM_FORMAT_XRGB8888,
			static_cast<enum gbm_bo_flags>(GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR)
		},
	};
	constexpr size_t tcase_count = BS_ARRAY_LEN(tcases);

	const int display_fd = bs_drm_open_main_display();
	if (display_fd < 0) {
		bs_debug_error("failed to open card for display");
		return 1;
	}

	gbm_device *gbm = gbm_create_device(display_fd);
	if (!gbm) {
		bs_debug_error("failed to create gbm device");
		return 1;
	}

	bs_drm_pipe pipe{};
	if (!bs_drm_pipe_make(display_fd, &pipe)) {
		bs_debug_error("failed to make pipe");
		return 1;
	}

	const drmModeConnector *connector = drmModeGetConnector(display_fd, pipe.connector_id);
	drmModeModeInfo *mode = &connector->modes[0];
	const uint32_t width = mode->hdisplay;
	const uint32_t height = mode->vdisplay;

	bs_egl *egl = bs_egl_new();
	if (!bs_egl_setup(egl, nullptr)) {
		bs_debug_error("failed to setup egl context");
		return 1;
	}

	uint32_t fbs[BS_ARRAY_LEN(tcases)]{};
	bs_mapper *mapper = bs_mapper_dma_buf_new();
	if (mapper == nullptr) {
		bs_debug_error("failed to create mapper object");
		return 1;
	}

	bool all_pass = true;
	for (size_t tcase_index = 0; tcase_index < tcase_count; tcase_index++) {
		const test_case *tcase = &tcases[tcase_index];

		gbm_bo *bo = gbm_bo_create(gbm, width, height, tcase->format, tcase->usage);

		if (!bo) {
			all_pass = false;
			printf("failed test case: ");
			test_case_print(stdout, tcase);
			printf("\n");
			continue;
		}

		bs_drm_fb_builder *fb_builder = bs_drm_fb_builder_new();
		bs_drm_fb_builder_gbm_bo(fb_builder, bo);
		if (tcase->fb_format)
			bs_drm_fb_builder_format(fb_builder, tcase->fb_format);
		fbs[tcase_index] = bs_drm_fb_builder_create_fb(fb_builder);
		bs_drm_fb_builder_destroy(&fb_builder);
		if (!fbs[tcase_index]) {
			bs_debug_error("failed to create framebuffer from buffer object");
			return 1;
		}

		if (tcase->usage & GBM_BO_USE_LINEAR) {
			if (!test_case_draw_dma_buf(tcase, mapper, bo)) {
				bs_debug_error("failed to draw to buffer using vgem");
				return 1;
			}
		} else if (tcase->usage & GBM_BO_USE_RENDERING) {
			if (!test_case_draw_gl(egl, tcase, bo)) {
				bs_debug_error("failed to draw to buffer using GL");
				return 1;
			}
		}

		// Reference held in kernel by the frame buffer.
		gbm_bo_destroy(bo);
	}

	bs_mapper_destroy(mapper);

	for (size_t tcase_index = 0; tcase_index < tcase_count; tcase_index++) {
		const test_case *tcase = &tcases[tcase_index];
		const uint32_t fb_id = fbs[tcase_index];

		if (fb_id == 0)
			continue;

		printf("displaying test case: ");
		test_case_print(stdout, tcase);
		printf("\n");

		const int ret = drmModeSetCrtc(display_fd, pipe.crtc_id, fb_id, 0 /* x */, 0 /* y */,
		                               &pipe.connector_id, 1 /* connector count */, mode);
		if (ret) {
			bs_debug_error("failed to set crtc: %d", ret);
			return 1;
		}
		usleep(test_case_display_usec);
	}

	for (const unsigned int fb: fbs)
		if (fb != 0)
			drmModeRmFB(display_fd, fb);

	bs_egl_destroy(&egl);

	return all_pass ? 0 : 2;
}
