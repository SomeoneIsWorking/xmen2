#include "igb_mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int igb_obj_is(const igb *f, int idx, const char *type)
{
    if (idx < 0 || idx >= f->n_objects) {
        return 0;
    }
    const igb_object *o = &f->objects[idx];
    return o->type_name && strcmp(o->type_name, type) == 0;
}

int igb_obj_slot_i32(const igb *f, int idx, uint16_t slot)
{
    if (idx < 0 || idx >= f->n_objects) {
        return -1;
    }
    const igb_fieldval *fv = igb_object_field(&f->objects[idx], slot);
    if (!fv) {
        return -1;
    }
    if (fv->blob && fv->blob_len >= 4) {
        return (int)fv->blob[0] | ((int)fv->blob[1] << 8) | ((int)fv->blob[2] << 16) | ((int)fv->blob[3] << 24);
    }
    return fv->i32;
}

const uint8_t *igb_obj_slot_blob(const igb *f, int idx, uint16_t slot, int *out_len)
{
    if (idx < 0 || idx >= f->n_objects) {
        return NULL;
    }
    const igb_fieldval *fv = igb_object_field(&f->objects[idx], slot);
    if (!fv || !fv->blob) {
        return NULL;
    }
    *out_len = (int)fv->blob_len;
    return fv->blob;
}

/* Read the int array stored in a list object's data block. */
static int list_children(const igb *f, int list_idx, int **out)
{
    *out = NULL;
    if (list_idx < 0 || list_idx >= f->n_objects || !f->objects[list_idx].is_mem) {
        return 0;
    }
    const igb_object *o = &f->objects[list_idx];
    int n = (int)(o->mem_size / 4);
    if (n <= 0) {
        return 0;
    }
    int *arr = malloc((size_t)n * sizeof(int));
    if (!arr) {
        return 0;
    }
    for (int i = 0; i < n; ++i) {
        arr[i] = (int)(o->mem[i * 4] | (o->mem[i * 4 + 1] << 8) |
                       (o->mem[i * 4 + 2] << 16) | (o->mem[i * 4 + 3] << 24));
    }
    *out = arr;
    return n;
}

/* igObjectList-derived objects (igNodeList/igAttrList/...) have the child
 * index in slot 2 and their data block (array of refs) in slot 4. */
static int obj_children(const igb *f, int owner_idx, int **out)
{
    *out = NULL;
    int list_idx = igb_obj_slot_i32(f, owner_idx, 7);
    if (list_idx < 0 || !igb_obj_is(f, list_idx, "igNodeList")) {
        return 0;
    }
    int data_idx = igb_obj_slot_i32(f, list_idx, 4);
    if (data_idx < 0 || data_idx >= f->n_objects || !f->objects[data_idx].is_mem) {
        return 0;
    }
    int cnt = igb_obj_slot_i32(f, list_idx, 2);
    int n = list_children(f, data_idx, out);
    if (n > cnt) {
        n = cnt;
    }
    return n;
}

/* Read the int array stored in a list object's data block. */
static int attr_children(const igb *f, int owner_idx, int **out)
{
    *out = NULL;
    int list_idx = igb_obj_slot_i32(f, owner_idx, 8);
    if (list_idx < 0 || !igb_obj_is(f, list_idx, "igAttrList")) {
        return 0;
    }
    int data_idx = igb_obj_slot_i32(f, list_idx, 4);
    if (data_idx < 0 || data_idx >= f->n_objects || !f->objects[data_idx].is_mem) {
        return 0;
    }
    int cnt = igb_obj_slot_i32(f, list_idx, 2);
    int n = list_children(f, data_idx, out);
    if (n > cnt) {
        n = cnt;
    }
    return n;
}

static int first_image_of_texture(const igb *f, int tex_idx)
{
    if (tex_idx < 0 || tex_idx >= f->n_objects) {
        return -1;
    }
    const char *tn = f->objects[tex_idx].type_name;
    if (!tn) {
        return -1;
    }
    if (strcmp(tn, "igImage") == 0) {
        return tex_idx;
    }
    if (strcmp(tn, "igImageMipMapList") == 0) {
        int data_idx = igb_obj_slot_i32(f, tex_idx, 4);
        int *kids = NULL;
        int n = data_idx >= 0 ? list_children(f, data_idx, &kids) : 0;
        int img = n > 0 ? kids[0] : -1;
        free(kids);
        return img;
    }
    if (strcmp(tn, "igTextureAttr") == 0) {
        int mml = igb_obj_slot_i32(f, tex_idx, 16);
        if (mml >= 0 && igb_obj_is(f, mml, "igImageMipMapList")) {
            int data_idx = igb_obj_slot_i32(f, mml, 4);
            int *kids = NULL;
            int n = data_idx >= 0 ? list_children(f, data_idx, &kids) : 0;
            int img = n > 0 ? kids[0] : -1;
            free(kids);
            if (img >= 0) {
                return img;
            }
        }
        return igb_obj_slot_i32(f, tex_idx, 12); /* _image */
    }
    if (strcmp(tn, "igTextureList") == 0) {
        int mml = igb_obj_slot_i32(f, tex_idx, 16);
        if (mml >= 0 && igb_obj_is(f, mml, "igImageMipMapList")) {
            int data_idx = igb_obj_slot_i32(f, mml, 4);
            int *kids = NULL;
            int n = data_idx >= 0 ? list_children(f, data_idx, &kids) : 0;
            int img = n > 0 ? kids[0] : -1;
            free(kids);
            if (img >= 0) {
                return img;
            }
        }
        return igb_obj_slot_i32(f, tex_idx, 12);
    }
    return -1;
}

/* Find the first texture source (igTextureAttr/igTextureList) reachable from
 * this node's attribute list. */
static int texture_source_of_node(const igb *f, int node_idx)
{
    int *attrs = NULL;
    int n = attr_children(f, node_idx, &attrs);
    int result = -1;
    for (int i = 0; i < n; ++i) {
        int a = attrs[i];
        if (a < 0 || a >= f->n_objects) {
            continue;
        }
        if (igb_obj_is(f, a, "igTextureBindAttr")) {
            int t = igb_obj_slot_i32(f, a, 4);
            if (t >= 0 && t < f->n_objects && f->objects[t].type_name &&
                (strcmp(f->objects[t].type_name, "igTextureAttr") == 0 ||
                 strcmp(f->objects[t].type_name, "igTextureList") == 0)) {
                result = t;
                break;
            }
        }
    }
    free(attrs);
    return result;
}

typedef struct {
    int geom_idx;
    igb_mesh mesh;
} geom_out;

static int decode_geometry(const igb *f, int geom_idx, const float world[16],
                           int tex_list_idx, igb_mesh *m)
{
    memset(m, 0, sizeof(*m));
    if (geom_idx < 0 || geom_idx >= f->n_objects) {
        return -1;
    }

    int *attrs = NULL;
    int nattrs = attr_children(f, geom_idx, &attrs);
    int gattr = -1;
    for (int i = 0; i < nattrs; ++i) {
        int a = attrs[i];
        if (a >= 0 && a < f->n_objects && f->objects[a].type_name &&
            strncmp(f->objects[a].type_name, "igGeometryAttr", 14) == 0) {
            gattr = a;
            break;
        }
    }
    free(attrs);
    if (gattr < 0) {
        return -1;
    }

    int va = igb_obj_slot_i32(f, gattr, 4);
    int ia = igb_obj_slot_i32(f, gattr, 5);
    int num_prim = igb_obj_slot_i32(f, gattr, 7);
    int prim_type = igb_obj_slot_i32(f, gattr, 6);
    int pla = igb_obj_slot_i32(f, gattr, 13);
    if (va < 0 || ia < 0) {
        return -1;
    }

    /* Vertex buffer: MemoryRef -> igExternalIndexedEntry block listing the
     * per-component igObjectDirEntry data blocks. */
    int nverts = igb_obj_slot_i32(f, va, 3);
    int vformat = igb_obj_slot_i32(f, va, 6);
    int vdata = igb_obj_slot_i32(f, va, 2);
    if (nverts <= 0 || nverts > 1 << 20 || vdata < 0 || vdata >= f->n_objects ||
        !f->objects[vdata].is_mem) {
        return -1;
    }
    int ncomp = (int)(f->objects[vdata].mem_size / 4);
    int *comps = malloc((size_t)(ncomp > 0 ? ncomp : 1) * sizeof(int));
    if (!comps) {
        return -1;
    }
    for (int i = 0; i < ncomp; ++i) {
        const uint8_t *p = f->objects[vdata].mem + 4 * i;
        comps[i] = (int)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    }
    int posb = ncomp > 0 ? comps[0] : -1;
    int norb = ncomp > 1 ? comps[1] : -1;
    int colb = ncomp > 2 ? comps[2] : -1;
    int uvb = ncomp > 11 ? comps[11] : -1;
    free(comps);

    int has_pos = (vformat & 1) != 0;
    int has_nor = (vformat & 2) != 0;
    int has_col = (vformat & 4) != 0;
    int ntex = (vformat >> 16) & 0xF;
    (void)has_pos;

    float *pos = NULL, *nor = NULL, *uv = NULL;
    uint8_t *col = NULL;
    if (has_pos && posb >= 0 && f->objects[posb].is_mem &&
        f->objects[posb].mem_size >= (size_t)nverts * 12) {
        pos = malloc((size_t)nverts * 12);
        if (pos) {
            memcpy(pos, f->objects[posb].mem, (size_t)nverts * 12);
        }
    }
    if (has_nor && norb >= 0 && f->objects[norb].is_mem &&
        f->objects[norb].mem_size >= (size_t)nverts * 12) {
        nor = malloc((size_t)nverts * 12);
        if (nor) {
            memcpy(nor, f->objects[norb].mem, (size_t)nverts * 12);
        }
    }
    if (has_col && colb >= 0 && f->objects[colb].is_mem &&
        f->objects[colb].mem_size >= (size_t)nverts * 4) {
        col = malloc((size_t)nverts * 4);
        if (col) {
            const uint8_t *src = f->objects[colb].mem;
            for (int i = 0; i < nverts; ++i) {
                col[i * 4 + 0] = src[i * 4 + 2]; /* D3DCOLOR mem: b,g,r,a */
                col[i * 4 + 1] = src[i * 4 + 1];
                col[i * 4 + 2] = src[i * 4 + 0];
                col[i * 4 + 3] = src[i * 4 + 3];
            }
        }
    }
    if (ntex > 0 && uvb >= 0 && f->objects[uvb].is_mem &&
        f->objects[uvb].mem_size >= (size_t)nverts * 8) {
        uv = malloc((size_t)nverts * 8);
        if (uv) {
            memcpy(uv, f->objects[uvb].mem, (size_t)nverts * 8);
        }
    }
    if (!pos) {
        free(pos);
        free(nor);
        free(col);
        free(uv);
        return -1;
    }

    /* Index buffer. */
    int nidx = igb_obj_slot_i32(f, ia, 3);
    int isize = igb_obj_slot_i32(f, ia, 4);
    int idata = igb_obj_slot_i32(f, ia, 2);
    uint32_t *idx = NULL;
    if (nidx > 0 && nidx <= (1 << 24) && idata >= 0 && idata < f->n_objects &&
        f->objects[idata].is_mem) {
        const uint8_t *src = f->objects[idata].mem;
        size_t want = (size_t)nidx * (isize == 0 ? 2u : (isize == 1 ? 4u : 1u));
        if (f->objects[idata].mem_size >= want) {
            idx = malloc((size_t)nidx * sizeof(uint32_t));
            if (idx) {
                if (isize == 0) {
                    const uint16_t *p = (const uint16_t *)src;
                    for (int i = 0; i < nidx; ++i) {
                        idx[i] = p[i];
                    }
                } else if (isize == 1) {
                    const uint32_t *p = (const uint32_t *)src;
                    for (int i = 0; i < nidx; ++i) {
                        idx[i] = p[i];
                    }
                } else {
                    for (int i = 0; i < nidx; ++i) {
                        idx[i] = src[i];
                    }
                }
            }
        }
    }

    /* Primitive lengths. */
    uint32_t *prim = NULL;
    if (num_prim > 0 && num_prim <= (1 << 20) && pla >= 0 && pla < f->n_objects) {
        int pdata = igb_obj_slot_i32(f, pla, 2);
        if (pdata >= 0 && pdata < f->n_objects && f->objects[pdata].is_mem) {
            const uint8_t *src = f->objects[pdata].mem;
            if (f->objects[pdata].mem_size >= (size_t)num_prim * 4) {
                prim = malloc((size_t)num_prim * sizeof(uint32_t));
                if (prim) {
                    for (int i = 0; i < num_prim; ++i) {
                        prim[i] = (uint32_t)(src[i * 4] | (src[i * 4 + 1] << 8) |
                                             (src[i * 4 + 2] << 16) | (src[i * 4 + 3] << 24));
                    }
                }
            }
        }
    }

    m->pos = pos;
    m->nor = nor;
    m->col = col;
    m->uv = uv;
    m->nverts = nverts;
    m->idx = idx;
    m->nidx = idx ? nidx : 0;
    m->prim_len = prim;
    m->num_prim = prim ? num_prim : 0;
    m->prim_type = prim_type;
    m->img_idx = tex_list_idx >= 0 ? first_image_of_texture(f, tex_list_idx) : -1;
    if (world) {
        memcpy(m->world, world, sizeof(m->world));
    } else {
        for (int i = 0; i < 16; ++i) {
            m->world[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
    }
    const igb_object *go = &f->objects[geom_idx];
    if (go->fields && go->n_fields > 0) {
        for (int i = 0; i < go->n_fields; ++i) {
            if (strcmp(go->fields[i].short_name, "String") == 0 && go->fields[i].blob) {
                m->name = strdup((const char *)go->fields[i].blob);
                break;
            }
        }
    }
    return 0;
}

static void mul_mat(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] + a[i * 4 + 1] * b[1 * 4 + j] +
                           a[i * 4 + 2] * b[2 * 4 + j] + a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
    memcpy(out, r, sizeof(r));
}

typedef struct {
    int *parent;
    int n;
    int *is_root;
    int *seen_geom;
    igb *f;
    igb_scene *scene;
    int cap;
} walk_ctx;

static void scene_push_mesh(walk_ctx *w, igb_mesh *m)
{
    if (w->scene->n_meshes == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 8;
        w->scene->meshes = realloc(w->scene->meshes, (size_t)w->cap * sizeof(igb_mesh));
    }
    w->scene->meshes[w->scene->n_meshes++] = *m;
}

static void walk_node(walk_ctx *w, int node_idx, const float parent_mat[16], int tex_list)
{
    const igb *f = w->f;
    if (node_idx < 0 || node_idx >= f->n_objects || f->objects[node_idx].is_mem) {
        return;
    }
    const igb_object *o = &f->objects[node_idx];
    const char *tn = o->type_name ? o->type_name : "?";

    float local[16];
    memcpy(local, parent_mat, sizeof(local));

    /* Level tile geometry is authored at the origin and placed by igTransform
     * ancestors; model files use transforms as animation rigs with the mesh
     * already in final position.  Honor X2VIEW_TRANSFORMS=1 to compose them. */
    if (getenv("X2VIEW_TRANSFORMS")) {
        if (strcmp(tn, "igTransform") == 0) {
            int blen = 0;
            const uint8_t *b = igb_obj_slot_blob(f, node_idx, 8, &blen);
            if (b && blen >= 64) {
                float lm[16];
                for (int i = 0; i < 16; ++i) {
                    lm[i] = *(const float *)(b + 4 * i);
                }
                mul_mat(local, parent_mat, lm);
            }
        }
    }

    int my_tex = tex_list;
    if (strcmp(tn, "igGroup") == 0 || strncmp(tn, "igAttrSet", 9) == 0 ||
        strcmp(tn, "igGeometry") == 0) {
        int tl = texture_source_of_node(f, node_idx);
        if (tl >= 0) {
            my_tex = tl;
        }
    }

    if (strcmp(tn, "igGeometry") == 0) {
        if (node_idx < w->n && w->seen_geom[node_idx]) {
            return;
        }
        if (node_idx < w->n) {
            w->seen_geom[node_idx] = 1;
        }
        igb_mesh m;
        if (decode_geometry(f, node_idx, local, my_tex, &m) == 0) {
            if (getenv("X2VIEW_DEBUG")) {
                fprintf(stderr, "walk geom=%d tex_src=%d img=%d nverts=%d\n", node_idx, my_tex,
                        m.img_idx, m.nverts);
            }
            scene_push_mesh(w, &m);
        }
    }

    int *kids = NULL;
    int n = obj_children(f, node_idx, &kids);
    for (int i = 0; i < n; ++i) {
        walk_node(w, kids[i], local, my_tex);
    }
    free(kids);
}

int igb_scene_load(const igb *f, igb_scene *out)
{
    memset(out, 0, sizeof(*out));

    if (getenv("X2VIEW_DEBUG")) {
        fprintf(stderr, "igb_scene_load start\n");
    }

    /* Build a parent map from every node list. */
    int n = f->n_objects;
    int *parent = malloc((size_t)n * sizeof(int));
    if (!parent) {
        return -1;
    }
    for (int i = 0; i < n; ++i) {
        parent[i] = -1;
    }
    for (int i = 0; i < n; ++i) {
        if (f->objects[i].is_mem || !f->objects[i].type_name) {
            continue;
        }
        int *kids = NULL;
        int k = obj_children(f, i, &kids);
        for (int j = 0; j < k; ++j) {
            if (kids[j] >= 0 && kids[j] < n && parent[kids[j]] < 0) {
                parent[kids[j]] = i;
            }
        }
        free(kids);
    }

    walk_ctx w;
    memset(&w, 0, sizeof(w));
    w.parent = parent;
    w.n = n;
    w.f = (igb *)f;
    w.scene = out;
    w.seen_geom = calloc((size_t)n, sizeof(int));
    if (!w.seen_geom) {
        free(parent);
        return -1;
    }

    float ident[16];
    for (int i = 0; i < 16; ++i) {
        ident[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    /* Walk from every orphaned object that can own children.  Scene roots are
     * not always igGroup: the tutorial level roots at igLightStateSet.  Data
     * objects (igImage, igVertexArray, ...) have no node-list children, so
     * walking them is a no-op; the seen_geom set dedupes any shared geometry. */
    int walked_any = 0;
    for (int i = 0; i < n; ++i) {
        if (f->objects[i].is_mem || !f->objects[i].type_name || parent[i] >= 0) {
            continue;
        }
        if (getenv("X2VIEW_DEBUG")) {
            fprintf(stderr, "walk from root %s[%d]\n", f->objects[i].type_name, i);
        }
        walk_node(&w, i, ident, -1);
        walked_any = 1;
    }
    if (!walked_any) {
        /* Fall back: walk every geometry directly. */
        for (int i = 0; i < n; ++i) {
            if (!f->objects[i].is_mem && f->objects[i].type_name &&
                strcmp(f->objects[i].type_name, "igGeometry") == 0) {
                igb_mesh m;
                if (decode_geometry(f, i, ident, -1, &m) == 0) {
                    scene_push_mesh(&w, &m);
                }
            }
        }
    }

    free(w.seen_geom);
    free(parent);
    return 0;
}

void igb_scene_free(igb_scene *out)
{
    for (int i = 0; i < out->n_meshes; ++i) {
        igb_mesh *m = &out->meshes[i];
        free(m->pos);
        free(m->nor);
        free(m->col);
        free(m->uv);
        free(m->idx);
        free(m->prim_len);
        free(m->name);
    }
    free(out->meshes);
    memset(out, 0, sizeof(*out));
}
