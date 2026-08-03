#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "igb.h"
#include "igb_mesh.h"

#define W 1280
#define H 720

typedef struct {
    float v[3];
    float u, w_uv[2];
    uint8_t c[4];
    float z;
} RasterVert;

typedef struct {
    RasterVert tv[3];
    float depth;
    uint8_t *tex;
    int tw, th;
} RasterTri;

typedef struct {
    uint8_t *rgba;
    float *z;
    int w, h;
} FrameBuf;

static void draw_tri(FrameBuf *fb, const RasterTri *t)
{
    int x0 = W, x1 = 0, y0 = H, y1 = 0;
    for (int i = 0; i < 3; ++i) {
        int sx = (int)t->tv[i].v[0], sy = (int)t->tv[i].v[1];
        if (sx < x0) x0 = sx;
        if (sx > x1) x1 = sx;
        if (sy < y0) y0 = sy;
        if (sy > y1) y1 = sy;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;

    const RasterVert *a = &t->tv[0], *b = &t->tv[1], *c = &t->tv[2];
    float ax = a->v[0], ay = a->v[1], az = a->z;
    float bx = b->v[0], by = b->v[1], bz = b->z;
    float cx = c->v[0], cy = c->v[1], cz = c->z;
    float inv_area = 1.0f / ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
    float au = a->w_uv[0], av = a->w_uv[1], aw = a->w_uv[0] * 0 + 1.0f;
    (void)aw;

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float w0 = ((bx - fx) * (cy - fy) - (by - fy) * (cx - fx)) * inv_area;
            float w1 = ((cx - fx) * (ay - fy) - (cy - fy) * (ax - fx)) * inv_area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            float zz = w0 * az + w1 * bz + w2 * cz;
            size_t pix = (size_t)y * fb->w + (size_t)x;
            if (zz >= fb->z[pix]) {
                continue;
            }
            fb->z[pix] = zz;
            uint8_t *out = fb->rgba + pix * 4;
            if (t->tex) {
                float u = w0 * au + w1 * b->w_uv[0] + w2 * c->w_uv[0];
                float v = w0 * av + w1 * b->w_uv[1] + w2 * c->w_uv[1];
                u = u - floorf(u);
                v = v - floorf(v);
                int tu = (int)(u * (float)t->tw);
                int tv_ = (int)(v * (float)t->th);
                if (tu >= t->tw) tu = t->tw - 1;
                if (tv_ >= t->th) tv_ = t->th - 1;
                const uint8_t *tx = t->tex + ((size_t)tv_ * t->tw + (size_t)tu) * 4;
                uint8_t cr = (uint8_t)(((int)tx[0] * (int)a->c[0]) >> 8);
                uint8_t cg = (uint8_t)(((int)tx[1] * (int)a->c[1]) >> 8);
                uint8_t cb = (uint8_t)(((int)tx[2] * (int)a->c[2]) >> 8);
                uint8_t ca = (uint8_t)(((int)tx[3] * (int)a->c[3]) >> 8);
                if (ca == 0) {
                    continue;
                }
                out[0] = cr; out[1] = cg; out[2] = cb; out[3] = ca;
            } else {
                out[0] = a->c[0]; out[1] = a->c[1]; out[2] = a->c[2]; out[3] = 255;
            }
        }
    }
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

static void transform_pt(float out[3], const float m[16], const float p[3])
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

static int qsort_tri(const void *a, const void *b)
{
    const RasterTri *ta = (const RasterTri *)a, *tb = (const RasterTri *)b;
    if (ta->depth < tb->depth) return 1;
    if (ta->depth > tb->depth) return -1;
    return 0;
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

    /* Image lookup: igb_find_images returns images in object-index order. */
    igb_image imgs[512];
    int nimg = igb_find_images(&f, imgs, 512);
    int *img_obj_idx = malloc((size_t)nimg * sizeof(int));
    int img_pos = 0;
    for (int i = 0; i < f.n_objects && img_pos < nimg; ++i) {
        if (!f.objects[i].is_mem && f.objects[i].type_name &&
            strcmp(f.objects[i].type_name, "igImage") == 0) {
            img_obj_idx[img_pos++] = i;
        }
    }

    uint8_t *tex_cache[512] = {0};
    int tex_dim[512][2] = {{0}};
    for (int i = 0; i < sc.n_meshes; ++i) {
        int idx = sc.meshes[i].img_idx;
        if (idx < 0) {
            continue;
        }
        for (int k = 0; k < img_pos; ++k) {
            if (img_obj_idx[k] == idx) {
                int len = 0;
                uint8_t *rgba = igb_image_to_rgba(&imgs[k], &len);
                if (rgba) {
                    tex_cache[i] = rgba;
                    tex_dim[i][0] = imgs[k].width;
                    tex_dim[i][1] = imgs[k].height;
                }
                break;
            }
        }
    }

    /* Render one frame (rotate handled by azimuth argument). */
    float center[3], radius;
    bbox_scene(&sc, center, &radius);
    printf("scene bbox center=(%.2f %.2f %.2f) radius=%.2f\n", center[0], center[1], center[2], radius);
    float dist = radius * 1.45f;
    float rad = azim * 3.14159265f / 180.0f;
    float eye[3] = {
        center[0] + dist * cosf(rad),
        center[1] + radius * 0.9f,
        center[2] + dist * sinf(rad)
    };
    float up[3] = {0, 1, 0};
    float view[16];
    look_at(eye, center, up, view);

    float focal = (float)H * 0.9f;
    float cx = W * 0.5f, cy = H * 0.5f;

    FrameBuf fb;
    fb.w = W; fb.h = H;
    fb.rgba = calloc((size_t)W * H * 4, 1);
    fb.z = malloc((size_t)W * H * sizeof(float));
    for (int i = 0; i < W * H; ++i) {
        fb.z[i] = 1e30f;
    }

    RasterTri *tris = NULL;
    int ntris = 0;
    for (int mi = 0; mi < sc.n_meshes; ++mi) {
        igb_mesh *m = &sc.meshes[mi];
        /* Build the per-primitive index runs.  For a single strip without an
         * explicit prim-length array the whole index buffer is one strip. */
        int base = 0;
        for (int p = 0; p < (m->num_prim > 0 ? m->num_prim : 1); ++p) {
            int len = m->num_prim > 0 ? (int)m->prim_len[p] : m->nidx;
            if (m->prim_type == 4) {           /* TRIANGLE_STRIP */
                for (int i = 0; i + 2 < len; ++i) {
                    int v0 = (int)m->idx[base + i];
                    int v1 = (int)m->idx[base + i + 1];
                    int v2 = (int)m->idx[base + i + 2];
                    if (v0 == v1 || v1 == v2 || v0 == v2) {
                        continue;
                    }
                    RasterTri r;
                    memset(&r, 0, sizeof(r));
                    r.tex = tex_cache[mi];
                    r.tw = tex_dim[mi][0];
                    r.th = tex_dim[mi][1];
                    int order[3] = {v0, v1, v2};
                    if (i & 1) {
                        order[1] = v2;
                        order[2] = v1;
                    }
                    float sumz = 0;
                    int skip = 0;
                    for (int k = 0; k < 3; ++k) {
                        int vi = order[k];
                        if (vi < 0 || vi >= m->nverts) {
                            skip = 1;
                            break;
                        }
                        float p_[3] = {m->pos[vi * 3], m->pos[vi * 3 + 1], m->pos[vi * 3 + 2]};
                        float w[3];
                        transform_pt(w, m->world, p_);
                        float vp[3];
                        vp[0] = view[0] * w[0] + view[1] * w[1] + view[2] * w[2] + view[3];
                        vp[1] = view[4] * w[0] + view[5] * w[1] + view[6] * w[2] + view[7];
                        vp[2] = view[8] * w[0] + view[9] * w[1] + view[10] * w[2] + view[11];
                        if (vp[2] < 0.5f) {
                            skip = 1;
                            break;
                        }
                        RasterVert *rv = &r.tv[k];
                        rv->v[0] = cx + focal * vp[0] / vp[2];
                        rv->v[1] = cy - focal * vp[1] / vp[2];
                        rv->z = vp[2];
                        sumz += vp[2];
                        if (m->uv) {
                            rv->w_uv[0] = m->uv[vi * 2];
                            rv->w_uv[1] = m->uv[vi * 2 + 1];
                        }
                        if (m->col) {
                            rv->c[0] = m->col[vi * 4];
                            rv->c[1] = m->col[vi * 4 + 1];
                            rv->c[2] = m->col[vi * 4 + 2];
                            rv->c[3] = m->col[vi * 4 + 3];
                        } else {
                            rv->c[0] = rv->c[1] = rv->c[2] = 255;
                            rv->c[3] = 255;
                        }
                    }
                    if (skip) {
                        continue;
                    }
                    float ax = r.tv[1].v[0] - r.tv[0].v[0], ay = r.tv[1].v[1] - r.tv[0].v[1];
                    float bx = r.tv[2].v[0] - r.tv[0].v[0], by = r.tv[2].v[1] - r.tv[0].v[1];
                    if (ax * by - ay * bx >= 0) {
                        continue;
                    }
                    r.depth = sumz / 3.0f;
                    tris = realloc(tris, (size_t)(ntris + 1) * sizeof(RasterTri));
                    tris[ntris++] = r;
                }
            } else if (m->prim_type == 3) {    /* TRIANGLES list */
                for (int i = 0; i + 2 < len; i += 3) {
                    int v0 = (int)m->idx[base + i];
                    int v1 = (int)m->idx[base + i + 1];
                    int v2 = (int)m->idx[base + i + 2];
                    if (v0 == v1 || v1 == v2 || v0 == v2) {
                        continue;
                    }
                    RasterTri r;
                    memset(&r, 0, sizeof(r));
                    r.tex = tex_cache[mi];
                    r.tw = tex_dim[mi][0];
                    r.th = tex_dim[mi][1];
                    float sumz = 0;
                    int skip = 0;
                    for (int k = 0; k < 3; ++k) {
                        int vi = k == 0 ? v0 : (k == 1 ? v1 : v2);
                        if (vi < 0 || vi >= m->nverts) {
                            skip = 1;
                            break;
                        }
                        float p_[3] = {m->pos[vi * 3], m->pos[vi * 3 + 1], m->pos[vi * 3 + 2]};
                        float w[3];
                        transform_pt(w, m->world, p_);
                        float vp[3];
                        vp[0] = view[0] * w[0] + view[1] * w[1] + view[2] * w[2] + view[3];
                        vp[1] = view[4] * w[0] + view[5] * w[1] + view[6] * w[2] + view[7];
                        vp[2] = view[8] * w[0] + view[9] * w[1] + view[10] * w[2] + view[11];
                        if (vp[2] < 0.5f) {
                            skip = 1;
                            break;
                        }
                        RasterVert *rv = &r.tv[k];
                        rv->v[0] = cx + focal * vp[0] / vp[2];
                        rv->v[1] = cy - focal * vp[1] / vp[2];
                        rv->z = vp[2];
                        sumz += vp[2];
                        if (m->uv) {
                            rv->w_uv[0] = m->uv[vi * 2];
                            rv->w_uv[1] = m->uv[vi * 2 + 1];
                        }
                        if (m->col) {
                            rv->c[0] = m->col[vi * 4];
                            rv->c[1] = m->col[vi * 4 + 1];
                            rv->c[2] = m->col[vi * 4 + 2];
                            rv->c[3] = m->col[vi * 4 + 3];
                        } else {
                            rv->c[0] = rv->c[1] = rv->c[2] = 255;
                            rv->c[3] = 255;
                        }
                    }
                    if (skip) {
                        continue;
                    }
                    float ax = r.tv[1].v[0] - r.tv[0].v[0], ay = r.tv[1].v[1] - r.tv[0].v[1];
                    float bx = r.tv[2].v[0] - r.tv[0].v[0], by = r.tv[2].v[1] - r.tv[0].v[1];
                    if (ax * by - ay * bx >= 0) {
                        continue;
                    }
                    r.depth = sumz / 3.0f;
                    tris = realloc(tris, (size_t)(ntris + 1) * sizeof(RasterTri));
                    tris[ntris++] = r;
                }
            }
            base += len;
        }
    }

    qsort(tris, (size_t)ntris, sizeof(RasterTri), qsort_tri);
    for (int i = 0; i < ntris; ++i) {
        draw_tri(&fb, &tris[i]);
    }
    printf("triangles drawn=%d\n", ntris);

    const char *outshot = shot;
    if (outshot) {
        FILE *fp = fopen(outshot, "wb");
        if (fp) {
            uint8_t hdr[54] = {0};
            hdr[0] = 'B'; hdr[1] = 'M';
            uint32_t filesz = 54u + (uint32_t)W * H * 3u;
            hdr[2] = filesz & 0xff; hdr[3] = (filesz >> 8) & 0xff; hdr[4] = (filesz >> 16) & 0xff; hdr[5] = (filesz >> 24) & 0xff;
            hdr[10] = 54;
            hdr[14] = 40;
            uint32_t w_ = W, h_ = H;
            hdr[18] = w_ & 0xff; hdr[19] = (w_ >> 8) & 0xff; hdr[20] = (w_ >> 16) & 0xff; hdr[21] = (w_ >> 24) & 0xff;
            hdr[22] = h_ & 0xff; hdr[23] = (h_ >> 8) & 0xff; hdr[24] = (h_ >> 16) & 0xff; hdr[25] = (h_ >> 24) & 0xff;
            hdr[26] = 1; hdr[28] = 24;
            fwrite(hdr, 1, 54, fp);
            for (int y = H - 1; y >= 0; --y) {
                for (int x = 0; x < W; ++x) {
                    const uint8_t *px = fb.rgba + ((size_t)y * W + (size_t)x) * 4;
                    uint8_t row[3] = {px[2], px[1], px[0]};
                    fwrite(row, 1, 3, fp);
                }
            }
            fclose(fp);
            printf("wrote %s\n", outshot);
        }
        free(tris);
        for (int i = 0; i < sc.n_meshes; ++i) free(tex_cache[i]);
        free(img_obj_idx);
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
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(fb.rgba, W, H, 32, W * 4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
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

    free(tris);
    for (int i = 0; i < sc.n_meshes; ++i) free(tex_cache[i]);
    free(img_obj_idx);
    free(fb.rgba);
    free(fb.z);
    igb_scene_free(&sc);
    igb_close(&f);
    return 0;
}
