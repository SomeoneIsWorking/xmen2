#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igb.h"

static SDL_Texture *load_texture(SDL_Renderer *renderer, const char *path, int mip, int *tw, int *th)
{
    igb f;
    if (igb_open(&f, path) != 0) {
        fprintf(stderr, "igb_open failed: %s\n", path);
        return NULL;
    }
    igb_image imgs[128];
    int n = igb_find_images(&f, imgs, 128);
    if (n <= 0) {
        fprintf(stderr, "no igImage objects in %s\n", path);
        igb_close(&f);
        return NULL;
    }
    if (mip < 0) {
        mip = 0;
        for (int i = 1; i < n; ++i) {
            if (imgs[i].width > imgs[mip].width) {
                mip = i;
            }
        }
    }
    if (mip >= n) {
        fprintf(stderr, "mip %d out of range (0..%d)\n", mip, n - 1);
        igb_close(&f);
        return NULL;
    }
    int len = 0;
    uint8_t *rgba = igb_image_to_rgba(&imgs[mip], &len);
    if (!rgba) {
        fprintf(stderr, "decode failed for mip %d (pfmt=%d)\n", mip, imgs[mip].pixel_format);
        igb_close(&f);
        return NULL;
    }
    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                         SDL_TEXTUREACCESS_STATIC,
                                         imgs[mip].width, imgs[mip].height);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, rgba, imgs[mip].width * 4);
    }
    free(rgba);
    *tw = imgs[mip].width;
    *th = imgs[mip].height;
    igb_close(&f);
    return tex;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: x2view <file.igb> [mip]\n");
        return 1;
    }
    int mip = argc >= 3 ? atoi(argv[2]) : -1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("X-Men Legends II — asset viewer",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1280, 720, SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(win, -1, 0);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    int tw = 0, th = 0;
    SDL_Texture *tex = load_texture(renderer, argv[1], mip, &tw, &th);
    if (!tex) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    printf("displaying %dx%d (%s)\n", tw, th, argv[1]);

    const char *shot = getenv("X2VIEW_SHOT");
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
            }
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        float scale = (float)ww / tw;
        if ((float)wh / th < scale) {
            scale = (float)wh / th;
        }
        SDL_Rect dst = {
            (int)((ww - tw * scale) / 2), (int)((wh - th * scale) / 2),
            (int)(tw * scale), (int)(th * scale)
        };
        SDL_RenderCopy(renderer, tex, NULL, &dst);
        SDL_RenderPresent(renderer);
        if (shot) {
            SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, NULL, 0);
            SDL_Surface *surf = SDL_CreateRGBSurface(0, ww, wh, 32, 0, 0, 0, 0);
            if (surf) {
                SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                     surf->pixels, surf->pitch);
                SDL_SaveBMP(surf, shot);
                SDL_FreeSurface(surf);
                printf("saved %s\n", shot);
            }
            running = 0;
        }
        SDL_Delay(16);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
