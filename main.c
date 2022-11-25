#include <SDL.h>
#include "gfx_gl2.h"
#include "common.h"
#include "vxl.h"

struct globals {
	SDL_Window* window;
	int true_screen_width;
	int true_screen_height;
	int screen_width;
	int screen_height;
	float pixel_ratio;

	u32* im;
	int im_width;
	int im_height;
} g;

static void populate_screen_globals()
{
	int prev_width = g.true_screen_width;
	int prev_height = g.true_screen_height;
	SDL_GL_GetDrawableSize(g.window, &g.true_screen_width, &g.true_screen_height);
	int w, h;
	SDL_GetWindowSize(g.window, &w, &h);
	g.pixel_ratio = (float)g.true_screen_width / (float)w;
	g.screen_width = g.true_screen_width / g.pixel_ratio;
	g.screen_height = g.true_screen_height / g.pixel_ratio;
	if ((g.true_screen_width != prev_width || g.true_screen_height != prev_height)) {
#ifdef DEBUG
		printf("%d×%d -> %d×%d (r=%f)\n", prev_width, prev_height, g.screen_width, g.screen_height, g.pixel_ratio);
#endif
	}
}

static void clearscr()
{
	memset(g.im, 0, g.im_width * g.im_height * sizeof(*g.im));
}

static void vblit(struct vxl* vxl, int src_x0, int src_y0)
{
	clearscr();
	int dst_w = g.im_width;
	int dst_h = g.im_height;
	int src_w = vxl->bitmap_width;
	int src_h = vxl->bitmap_height;

	u32* dst = g.im;
	for (int y = 0; y < dst_h; y++) {
		for (int x = 0; x < dst_w; x++) {
			int src_x = src_x0 + x;
			int src_y = src_y0 + y;
			u32 src;
			if (src_x < 0 || src_x >= src_w || src_y < 0 || src_y >= src_h) {
				src = 0;
			} else {
				src = vxl->bitmap[src_x + src_y*src_w];
			}

			*dst = src;

			dst++;
		}
	}
}

int main(int argc, char** argv)
{
	assert(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) == 0);
	atexit(SDL_Quit);

	SDL_GLContext glctx;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

	g.window = SDL_CreateWindow(
			"song paint",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			1920, 1080,
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);

	if (g.window == NULL) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		abort();
	}

	glctx = SDL_GL_CreateContext(g.window);
	if (!glctx) {
		fprintf(stderr, "SDL_GL_CreateContextfailed: %s\n", SDL_GetError());
		abort();
	}

	{
		printf("                 GL_VERSION:  %s\n", glGetString(GL_VERSION));
		printf("                  GL_VENDOR:  %s\n", glGetString(GL_VENDOR));
		printf("                GL_RENDERER:  %s\n", glGetString(GL_RENDERER));
		printf("GL_SHADING_LANGUAGE_VERSION:  %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	}


	struct gfx gfx;
	gfx_init(&gfx);

	populate_screen_globals();

	{
		g.im_width = 1920/4;
		g.im_height = 1080/4;
		size_t sz = g.im_width * g.im_height * sizeof(*g.im);
		g.im = malloc(sz);
		assert(g.im != NULL);
		clearscr();
	}

	struct vxl vxl;
	const int vxl_dx = 128;
	const int vxl_dy = 128;
	const int vxl_dz = 32;
	{

		vxl_init(&vxl, vxl_dx, vxl_dy, vxl_dz);
		vxl_set_full_update(&vxl);
		vxl_set_rotation(&vxl, 0);

		// XXX debug drawing
		for (int y = 0; y < vxl_dy; y++) {
			for (int x = 0; x < vxl_dx; x++) {
				float f = sinf((float)x * 0.05f) * sinf((float)y * 0.07);
				int h = 5 + (int)((f+1.0f) * 10.0f);
				h = MAX(h, 0);
				h = MIN(h, vxl_dz);
				const int mid = 24;
				const int is_mid = x >= (vxl_dx-mid)/2 && x <= (vxl_dx+mid)/2 && y >= (vxl_dy-mid)/2 && y <= (vxl_dy+mid)/2;
				if (is_mid) h = vxl_dz-1;
				for (int z = 0; z < h; z++) {
					vxl_put(&vxl, x, y, z, 1);
				}
			}
		}
	}

	int exiting = 0;
	int fullscreen = 0;
	int iteration = 0;
	while (!exiting) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				exiting = 1;
			} else if (e.type == SDL_KEYDOWN) {
				if (e.key.keysym.sym == SDLK_ESCAPE) {
					exiting = 1;
				} else if (e.key.keysym.sym == SDLK_f) {
					fullscreen = !fullscreen;
					//SDL_SetWindowFullscreen(g.window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
					SDL_SetWindowFullscreen(g.window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
				}
			} else if (e.type == SDL_WINDOWEVENT) {
				if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
					populate_screen_globals();
				}
			}
		}

		glViewport(0, 0, g.true_screen_width, g.true_screen_height);
		glClearColor(0, 0, 0.2, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);

		//vxl_set_full_update(&vxl);

		for (int y = 0; y < vxl_dy; y++) {
			for (int x = 0; x < vxl_dx; x++) {
				{
					const int mid = 24;
					const int is_mid = x >= (vxl_dx-mid)/2 && x <= (vxl_dx+mid)/2 && y >= (vxl_dy-mid)/2 && y <= (vxl_dy+mid)/2;
					if (is_mid) {
						int h = (iteration >> 2) & (vxl_dz-1);
						for (int z = 0; z < vxl_dz; z++) {
							vxl_put(&vxl, x, y, z, z < h ? 1 : 0);
						}
					}
				}
				{
					const int mid = 12;
					const int is_mid = x >= (vxl_dx-mid)/2 && x <= (vxl_dx+mid)/2 && y >= (vxl_dy-mid)/2 && y <= (vxl_dy+mid)/2;
					if (is_mid) {
						int h = (iteration >> 3) & (vxl_dz-1);
						for (int z = 0; z < vxl_dz; z++) {
							vxl_put(&vxl, x, y, z, z < h ? 1 : 0);
						}
					}
				}
			}
		}


		vxl_flush(&vxl);
		printf("frame %d\n", iteration); // XXX

		vblit(&vxl, 0, 0);

		px_present(&gfx.px, g.true_screen_width, g.true_screen_height, g.im_width, g.im_height, g.im);

		SDL_GL_SwapWindow(g.window);

		iteration++;
	}

	SDL_GL_DeleteContext(glctx);
	SDL_DestroyWindow(g.window);

	return EXIT_SUCCESS;
}
