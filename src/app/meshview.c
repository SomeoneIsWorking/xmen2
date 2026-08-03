#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "igb.h"
#include "igb_mesh.h"
#include "igb_raster.h"

#define W 1280
#define H 720

static void transform_pt(float out[3], const float m[16], const float p[3])
{
    out[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    out[1] = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    out[2] = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
}

static void look_at(float eye[3], const float target[3], const float up[3], float out[16])
{
    float f[3], s[3], u[3];
    f[0] = target[0] - eye[0]; f[1] = target[1] - eye[1]; f[2] = target[2] - eye[2];
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] /= fl; f[1] /= fl; f[2] /= fl;
    s[0] = f[1] * up[2] - f[2] * up[1];
    s[1] = f[2] * up[0] - f[0] * up[2];
    s[2] = f[0] * up[1] - f[1] * up[0];
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    u[0] = s[1] * f[2] - s[2] * f[1];
    u[1] = s[2] * f[0] - s[0] * f[2];
    u[2] = s[0] * f[1] - s[1] * f[0];
    out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; out[3] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    out[4] = u[0]; out[5] = u[1]; out[6] = u[2]; out[7] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    out[8] = f[0]; out[9] = f[1]; out[10] = f[2]; out[11] = -(f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    out[12] = 0; out[13] = 0; out[14] = 0; out[15] = 1;
}

static void bbox_scene(const igb_scene *sc, float center[3], float *radius)
{
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    int any = 0;
    for (int i = 0; i < sc->n_meshes; ++i) {
        const igb_mesh *m = &sc->meshes[i];
        for (int v = 0; v < m->nverts; ++v) {
            float p[3] = {m->pos[v * 3], m->pos[v * 3 + 1], m->pos[v * 3 + 2]};
            float w[3];
            transform_pt(w, m->world, p);
            for (int k = 0; k < 3; ++k) {
                if (w[k] < lo[k]) lo[k] = w[k];
                if (w[k] > hi[k]) hi[k] = w[k];
            }
            any = 1;
        }
    }
    if (!any) {
        center[0] = center[1] = center[2] = 0;
        *radius = 1;
        return;
    }
    for (int k = 0; k < 3; ++k) {
        center[k] = (lo[k] + hi[k]) * 0.5f;
    }
    *radius = sqrtf(powf(hi[0] - lo[0], 2) + powf(hi[1] - lo[1], 2) + powf(hi[2] - lo[2], 2)) * 0.5f;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: meshview <file.igb> [azimuth_degrees]\n");
        return 1;
    }
    const char *shot = getenv("X2VIEW_SHOT");
    double azim = argc >= 3 ? atof(argv[2]) : 30.0;

    igb f;
    if (igb_open(&f, argv[1]) != 0) {
        fprintf(stderr, "igb_open failed\n");
        return 1;
    }
    igb_scene sc;
    if (igb_scene_load(&f, &sc) != 0 || sc.n_meshes == 0) {
        fprintf(stderr, "no meshes found\n");
        igb_close(&f);
        return 1;
    }
    printf("meshes=%d\n", sc.n_meshes);
    for (int i = 0; i < sc.n_meshes; ++i) {
        igb_mesh *m = &sc.meshes[i];
        printf("  [%d] name=%s nverts=%d nidx=%d prims=%d img=%d pos=%s uv=%s\n",
               i, m->name ? m->name : "-", m->nverts, m->nidx, m->num_prim, m->img_idx,
               m->pos ? "y" : "n", m->uv ? "y" : "n");
    }

    igb_raster *ras = igb_raster_build(&f, &sc);
    if (!ras) {
        fprintf(stderr, "raster build failed\n");
        igb_scene_free(&sc);
        igb_close(&f);
        return 1;
    }

    float center[3], radius;
    bbox_scene(&sc, center, &radius);
    printf("scene bbox center=(%.2f %.2f %.2f) radius=%.2f\n", center[0], center[1], center[2], radius);
    float dist = radius * 1.45f;
    float rad = (float)(azim * 3.14159265 / 180.0);
    float eye[3] = {
        center[0] + dist * cosf(rad),
        center[1] + radius * 0.9f,
        center[2] + dist * sinf(rad)
    };
    float up[3] = {0, 1, 0};
    float view[16];
    look_at(eye, center, up, view);

    uint8_t *pixels = calloc((size_t)W * H * 4, 1);
    float *zbuf = malloc((size_t)W * H * sizeof(float));
    int n = igb_raster_frame(ras, view, W, H, pixels, zbuf);
    printf("triangles drawn=%d\n", n);

    if (shot) {
        igb_raster_save_bmp(ras, view, W, H, shot);
        printf("wrote %s\n", shot);
        free(pixels);
        free(zbuf);
        igb_raster_free(ras);
        igb_scene_free(&sc);
        igb_close(&f);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("x2 meshview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(pixels, W, H, 32, W * 4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    SDL_Event ev;
    int run = 1;
    while (run) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                run = 0;
            }
        }
        SDL_Surface *ws = SDL_GetWindowSurface(win);
        SDL_BlitSurface(surf, NULL, ws, NULL);
        SDL_UpdateWindowSurface(win);
        SDL_Delay(16);
    }
    SDL_FreeSurface(surf);
    SDL_DestroyWindow(win);
    SDL_Quit();

    free(pixels);
    free(zbuf);
    igb_raster_free(ras);
    igb_scene_free(&sc);
    igb_close(&f);
    return 0;
}
