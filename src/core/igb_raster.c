#include "igb_raster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void mul3(float out[3], const float m[16], const float p[3])
{
    out[0] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    out[1] = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    out[2] = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
}

static void push_tri(igb_raster *r, const igb_raster_tri *t)
{
    if (r->n == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 1024;
        r->tris = realloc(r->tris, (size_t)r->cap * sizeof(igb_raster_tri));
    }
    r->tris[r->n++] = *t;
}

static int fill_tri(igb_raster_tri *t, const igb_mesh *m, int a, int b, int c,
                    const uint8_t *tex, int tw, int th)
{
    const int idx[3] = {a, b, c};
    memset(t, 0, sizeof(*t));
    for (int k = 0; k < 3; ++k) {
        int vi = idx[k];
        if (vi < 0 || vi >= m->nverts) {
            return 0;
        }
        float p[3] = {m->pos[vi * 3], m->pos[vi * 3 + 1], m->pos[vi * 3 + 2]};
        mul3(t->p[k], m->world, p);
        if (m->uv) {
            t->uv[k][0] = m->uv[vi * 2];
            t->uv[k][1] = m->uv[vi * 2 + 1];
        }
        if (m->col) {
            memcpy(t->col[k], m->col + vi * 4, 4);
        } else {
            t->col[k][0] = t->col[k][1] = t->col[k][2] = t->col[k][3] = 255;
        }
    }
    t->tex = tex;
    t->tw = tw;
    t->th = th;
    return 1;
}

igb_raster *igb_raster_build(const igb *f, const igb_scene *sc)
{
    igb_raster *r = calloc(1, sizeof(igb_raster));
    if (!r) {
        return NULL;
    }

    igb_image imgs[512];
    int nimg = igb_find_images((igb *)f, imgs, 512);
    int *img_obj = malloc((size_t)(nimg > 0 ? nimg : 1) * sizeof(int));
    int ipos = 0;
    for (int i = 0; i < f->n_objects && ipos < nimg; ++i) {
        if (!f->objects[i].is_mem && f->objects[i].type_name &&
            strcmp(f->objects[i].type_name, "igImage") == 0) {
            img_obj[ipos++] = i;
        }
    }

    const uint8_t *tex[512];
    int tw[512], th[512];
    for (int mi = 0; mi < sc->n_meshes; ++mi) {
        tex[mi] = NULL;
        tw[mi] = th[mi] = 0;
        int idx = sc->meshes[mi].img_idx;
        if (idx < 0) {
            continue;
        }
        for (int k = 0; k < ipos; ++k) {
            if (img_obj[k] == idx) {
                int len = 0;
                uint8_t *rgba = igb_image_to_rgba(&imgs[k], &len);
                if (rgba && r->n_tex < 512) {
                    r->tex_owner[r->n_tex++] = rgba;
                    tex[mi] = rgba;
                    tw[mi] = imgs[k].width;
                    th[mi] = imgs[k].height;
                } else if (rgba) {
                    free(rgba);
                }
                break;
            }
        }
    }
    free(img_obj);

    for (int mi = 0; mi < sc->n_meshes; ++mi) {
        const igb_mesh *m = &sc->meshes[mi];
        if (!m->pos || !m->idx) {
            continue;
        }
        int base = 0;
        int nprim = m->num_prim > 0 ? m->num_prim : 1;
        for (int p = 0; p < nprim; ++p) {
            int len = m->num_prim > 0 ? (int)m->prim_len[p] : m->nidx;
            if (m->prim_type == 4) { /* TRIANGLE_STRIP */
                for (int i = 0; i + 2 < len; ++i) {
                    int v0 = (int)m->idx[base + i];
                    int v1 = (int)m->idx[base + i + 1];
                    int v2 = (int)m->idx[base + i + 2];
                    if (v0 == v1 || v1 == v2 || v0 == v2) {
                        continue;
                    }
                    if (i & 1) {
                        int t_ = v1;
                        v1 = v2;
                        v2 = t_;
                    }
                    igb_raster_tri t;
                    if (!fill_tri(&t, m, v0, v1, v2, tex[mi], tw[mi], th[mi])) {
                        continue;
                    }
                    push_tri(r, &t);
                }
            } else if (m->prim_type == 3) { /* TRIANGLES list */
                for (int i = 0; i + 2 < len; i += 3) {
                    int v0 = (int)m->idx[base + i];
                    int v1 = (int)m->idx[base + i + 1];
                    int v2 = (int)m->idx[base + i + 2];
                    if (v0 == v1 || v1 == v2 || v0 == v2) {
                        continue;
                    }
                    igb_raster_tri t;
                    if (!fill_tri(&t, m, v0, v1, v2, tex[mi], tw[mi], th[mi])) {
                        continue;
                    }
                    push_tri(r, &t);
                }
            }
            base += len;
        }
    }
    return r;
}

typedef struct {
    float sx, sy;
    float z;
    float uv[2];
    uint8_t c[4];
} RasterVert;

typedef struct {
    RasterVert v[3];
    float depth;
    const uint8_t *tex;
    int tw, th;
} RasterTri;

static void draw_tri(const RasterTri *t, int w, int h, uint8_t *rgba, float *zbuf)
{
    int x0 = w, x1 = 0, y0 = h, y1 = 0;
    for (int i = 0; i < 3; ++i) {
        int sx = (int)t->v[i].sx, sy = (int)t->v[i].sy;
        if (sx < x0) x0 = sx;
        if (sx > x1) x1 = sx;
        if (sy < y0) y0 = sy;
        if (sy > y1) y1 = sy;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    const RasterVert *a = &t->v[0], *b = &t->v[1], *c = &t->v[2];
    float ax = a->sx, ay = a->sy, az = a->z;
    float bx = b->sx, by = b->sy, bz = b->z;
    float cx = c->sx, cy = c->sy, cz = c->z;
    float inv_area = 1.0f / ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
    float au = a->uv[0], av = a->uv[1];

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
            size_t pix = (size_t)y * w + (size_t)x;
            if (zz >= zbuf[pix]) {
                continue;
            }
            zbuf[pix] = zz;
            uint8_t *out = rgba + pix * 4;
            if (t->tex) {
                float u = w0 * au + w1 * b->uv[0] + w2 * c->uv[0];
                float v = w0 * av + w1 * b->uv[1] + w2 * c->uv[1];
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
                out[0] = cr;
                out[1] = cg;
                out[2] = cb;
                out[3] = ca;
            } else {
                out[0] = a->c[0];
                out[1] = a->c[1];
                out[2] = a->c[2];
                out[3] = 255;
            }
        }
    }
}

static int qsort_tri(const void *a, const void *b)
{
    const RasterTri *ta = (const RasterTri *)a, *tb = (const RasterTri *)b;
    if (ta->depth < tb->depth) return 1;
    if (ta->depth > tb->depth) return -1;
    return 0;
}

int igb_raster_frame(const igb_raster *r, const float view[16], int w, int h,
                     uint8_t *rgba, float *zbuf)
{
    memset(rgba, 0, (size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        zbuf[i] = 1e30f;
    }

    float focal = (float)h * 0.9f;
    float cx = w * 0.5f, cy = h * 0.5f;

    RasterTri *pts = malloc((size_t)(r->n > 0 ? r->n : 1) * sizeof(RasterTri));
    int n = 0;
    for (int i = 0; i < r->n; ++i) {
        const igb_raster_tri *t = &r->tris[i];
        RasterTri pt;
        memset(&pt, 0, sizeof(pt));
        pt.tex = t->tex;
        pt.tw = t->tw;
        pt.th = t->th;
        float sumz = 0;
        int skip = 0;
        for (int k = 0; k < 3; ++k) {
            float vp[3];
            vp[0] = view[0] * t->p[k][0] + view[1] * t->p[k][1] + view[2] * t->p[k][2] + view[3];
            vp[1] = view[4] * t->p[k][0] + view[5] * t->p[k][1] + view[6] * t->p[k][2] + view[7];
            vp[2] = view[8] * t->p[k][0] + view[9] * t->p[k][1] + view[10] * t->p[k][2] + view[11];
            if (vp[2] < 0.5f) {
                skip = 1;
                break;
            }
            RasterVert *rv = &pt.v[k];
            rv->sx = cx + focal * vp[0] / vp[2];
            rv->sy = cy - focal * vp[1] / vp[2];
            rv->z = vp[2];
            rv->uv[0] = t->uv[k][0];
            rv->uv[1] = t->uv[k][1];
            memcpy(rv->c, t->col[k], 4);
            sumz += vp[2];
        }
        if (skip) {
            continue;
        }
        float ax = pt.v[1].sx - pt.v[0].sx, ay = pt.v[1].sy - pt.v[0].sy;
        float bx = pt.v[2].sx - pt.v[0].sx, by = pt.v[2].sy - pt.v[0].sy;
        if (ax * by - ay * bx >= 0) {
            continue;
        }
        pt.depth = sumz / 3.0f;
        pts[n++] = pt;
    }

    qsort(pts, (size_t)n, sizeof(RasterTri), qsort_tri);
    for (int i = 0; i < n; ++i) {
        draw_tri(&pts[i], w, h, rgba, zbuf);
    }
    free(pts);
    return n;
}

static void bmp_hdr(uint8_t hdr[54], int w, int h)
{
    memset(hdr, 0, 54);
    hdr[0] = 'B';
    hdr[1] = 'M';
    uint32_t filesz = 54u + (uint32_t)w * h * 3u;
    hdr[2] = filesz & 0xff; hdr[3] = (filesz >> 8) & 0xff;
    hdr[4] = (filesz >> 16) & 0xff; hdr[5] = (filesz >> 24) & 0xff;
    hdr[10] = 54;
    hdr[14] = 40;
    uint32_t ww = (uint32_t)w, hh = (uint32_t)h;
    hdr[18] = ww & 0xff; hdr[19] = (ww >> 8) & 0xff; hdr[20] = (ww >> 16) & 0xff; hdr[21] = (ww >> 24) & 0xff;
    hdr[22] = hh & 0xff; hdr[23] = (hh >> 8) & 0xff; hdr[24] = (hh >> 16) & 0xff; hdr[25] = (hh >> 24) & 0xff;
    hdr[26] = 1;
    hdr[28] = 24;
}

void igb_raster_save_bmp(const igb_raster *r, const float view[16], int w, int h,
                         const char *path)
{
    uint8_t *rgba = malloc((size_t)w * h * 4);
    float *zbuf = malloc((size_t)w * h * sizeof(float));
    if (!rgba || !zbuf) {
        free(rgba);
        free(zbuf);
        return;
    }
    igb_raster_frame(r, view, w, h, rgba, zbuf);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(rgba);
        free(zbuf);
        return;
    }
    uint8_t hdr[54];
    bmp_hdr(hdr, w, h);
    fwrite(hdr, 1, 54, fp);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t *px = rgba + ((size_t)y * w + (size_t)x) * 4;
            uint8_t row[3] = {px[2], px[1], px[0]};
            fwrite(row, 1, 3, fp);
        }
    }
    fclose(fp);
    free(rgba);
    free(zbuf);
}

void igb_raster_free(igb_raster *r)
{
    if (!r) {
        return;
    }
    for (int i = 0; i < r->n_tex; ++i) {
        free(r->tex_owner[i]);
    }
    free(r->tris);
    free(r);
}
