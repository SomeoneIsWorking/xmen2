#ifndef X2_IGB_MESH_H
#define X2_IGB_MESH_H

#include "igb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float *pos;          /* nverts * 3 */
    float *nor;          /* nverts * 3, or NULL */
    uint8_t *col;        /* nverts * 4 RGBA, or NULL */
    float *uv;           /* nverts * 2, or NULL */
    int nverts;
    uint32_t *idx;       /* nidx */
    int nidx;
    uint32_t *prim_len;  /* num_prim */
    int num_prim;
    int prim_type;       /* IG_GFX_DRAW enum (3=triangles,4=strip,5=fan) */
    int img_idx;         /* igImage object index for the texture, or -1 */
    float world[16];     /* row-major local->world */
    char *name;
} igb_mesh;

typedef struct {
    igb_mesh *meshes;
    int n_meshes;
} igb_scene;

/* Walk the root igGroup(s), collect every igGeometry with its resolved
 * vertex/index/primitive buffers, texture image, and composed world matrix. */
int igb_scene_load(const igb *f, igb_scene *out);
void igb_scene_free(igb_scene *out);

/* Object type / field helpers used by the scene walker. */
int igb_obj_is(const igb *f, int idx, const char *type);
int igb_obj_slot_i32(const igb *f, int idx, uint16_t slot);
const uint8_t *igb_obj_slot_blob(const igb *f, int idx, uint16_t slot, int *out_len);

#ifdef __cplusplus
}
#endif

#endif
