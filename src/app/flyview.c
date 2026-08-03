#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "igb.h"
#include "igb_mesh.h"
#include "igb_raster.h"

#define W 640
#define H 480

static void mul3(float out[3], const float m[16], const float p[3])
{
    out[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    out[1] = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    out[2] = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
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
            mul3(w, m->world, p);
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

/* FPS-style view matrix from eye pos + yaw/pitch (y is up). */
static void build_view(float eye[3], float yaw, float pitch, float out[16])
{
    float cp = cosf(pitch), sp = sinf(pitch);
    float f[3] = {cp * sinf(yaw), sp, cp * cosf(yaw)};
    float r[3] = {cosf(yaw), 0, -sinf(yaw)};
    float u[3];
    u[0] = r[1] * f[2] - r[2] * f[1];
    u[1] = r[2] * f[0] - r[0] * f[2];
    u[2] = r[0] * f[1] - r[1] * f[0];
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2]; out[3] = -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]);
    out[4] = u[0]; out[5] = u[1]; out[6] = u[2]; out[7] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    out[8] = f[0]; out[9] = f[1]; out[10] = f[2]; out[11] = -(f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    out[12] = 0; out[13] = 0; out[14] = 0; out[15] = 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: flyview <file.igb>\n");
        return 1;
    }
    const char *shot = getenv("X2VIEW_SHOT");

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
    igb_raster *ras = igb_raster_build(&f, &sc);
    if (!ras) {
        fprintf(stderr, "raster build failed\n");
        igb_scene_free(&sc);
        igb_close(&f);
        return 1;
    }
    printf("meshes=%d raster_tris=%d\n", sc.n_meshes, ras->n);

    float center[3], radius;
    bbox_scene(&sc, center, &radius);
    printf("scene bbox center=(%.2f %.2f %.2f) radius=%.2f\n", center[0], center[1], center[2], radius);

    float eye[3];
    float yaw, pitch;
    {
        eye[0] = center[0];
        eye[1] = center[1] + radius * 0.5f;
        eye[2] = center[2] + radius * 1.2f;
        float to[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
        float horiz = sqrtf(to[0] * to[0] + to[2] * to[2]);
        yaw = atan2f(to[0], to[2]);
        pitch = atan2f(to[1], horiz);
    }

    if (shot) {
        float view[16];
        build_view(eye, yaw, pitch, view);
        igb_raster_save_bmp(ras, view, W, H, shot);
        printf("wrote %s\n", shot);
        igb_raster_free(ras);
        igb_scene_free(&sc);
        igb_close(&f);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        igb_raster_free(ras);
        igb_scene_free(&sc);
        igb_close(&f);
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("x2 flyview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        igb_raster_free(ras);
        igb_scene_free(&sc);
        igb_close(&f);
        return 1;
    }
    SDL_SetRelativeMouseMode(SDL_TRUE);

    uint8_t *pixels = calloc((size_t)W * H * 4, 1);
    float *zbuf = malloc((size_t)W * H * sizeof(float));
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(pixels, W, H, 32, W * 4,
                                                 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);

    printf("keys: WASD move  SPACE/LCtrl up/down  Shift x4 speed  1/2 speed\n"
           "      mouse look  ESC quit\n");

    float speed_mult = 1.0f;
    Uint32 last = SDL_GetTicks();
    int frames = 0;
    Uint32 fps_last = last;
    int run = 1;
    while (run) {
        Uint32 now = SDL_GetTicks();
        float dt = (float)(now - last) / 1000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        int mx, my;
        SDL_GetRelativeMouseState(&mx, &my);
        yaw += (float)mx * 0.0022f;
        pitch += (float)my * 0.0022f;
        if (pitch > 1.55f) pitch = 1.55f;
        if (pitch < -1.55f) pitch = -1.55f;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                run = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    run = 0;
                } else if (ev.key.keysym.sym == SDLK_1) {
                    speed_mult *= 0.5f;
                } else if (ev.key.keysym.sym == SDLK_2) {
                    speed_mult *= 2.0f;
                }
            }
        }

        const uint8_t *ks = SDL_GetKeyboardState(NULL);
        float speed = radius * 0.12f * speed_mult;
        if (ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT]) {
            speed *= 4.0f;
        }
        float fx = sinf(yaw), fz = cosf(yaw);
        float rx = cosf(yaw), rz = -sinf(yaw);
        if (ks[SDL_SCANCODE_W]) { eye[0] += fx * speed * dt; eye[2] += fz * speed * dt; }
        if (ks[SDL_SCANCODE_S]) { eye[0] -= fx * speed * dt; eye[2] -= fz * speed * dt; }
        if (ks[SDL_SCANCODE_A]) { eye[0] -= rx * speed * dt; eye[2] -= rz * speed * dt; }
        if (ks[SDL_SCANCODE_D]) { eye[0] += rx * speed * dt; eye[2] += rz * speed * dt; }
        if (ks[SDL_SCANCODE_SPACE]) { eye[1] += speed * dt; }
        if (ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_C]) { eye[1] -= speed * dt; }

        float view[16];
        build_view(eye, yaw, pitch, view);
        igb_raster_frame(ras, view, W, H, pixels, zbuf);

        SDL_Surface *ws = SDL_GetWindowSurface(win);
        SDL_BlitSurface(surf, NULL, ws, NULL);
        SDL_UpdateWindowSurface(win);

        ++frames;
        if (now - fps_last >= 500) {
            char title[128];
            snprintf(title, sizeof(title), "x2 flyview  %d fps  pos=(%.0f %.0f %.0f)  speed x%.1f",
                     (int)(frames * 1000.0f / (now - fps_last)), eye[0], eye[1], eye[2], speed_mult);
            SDL_SetWindowTitle(win, title);
            frames = 0;
            fps_last = now;
        }
    }

    SDL_SetRelativeMouseMode(SDL_FALSE);
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
