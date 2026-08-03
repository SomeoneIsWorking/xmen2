#include "igb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X2_IGB_MAGIC 0xFADA
#define X2_IGB_HEADER_SIZE 48

typedef struct {
    char *short_name;
} igb_meta_field;

typedef struct {
    uint16_t type_idx;
    uint16_t slot;
    uint16_t size;
} igb_meta_field_desc;

typedef struct {
    char *name;
    int32_t parent;
    uint32_t n_fields;
    igb_meta_field_desc *fields;
} igb_meta_obj;

typedef struct {
    igb *f;
    igb_meta_field *meta_fields;
    uint32_t n_meta_fields;
    igb_meta_obj *meta_objs;
    uint32_t n_meta_objs;
} igb_ctx;

static uint32_t rd_u32(const uint8_t *p, int le)
{
    if (le) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t rd_u16(const uint8_t *p, int le)
{
    if (le) {
        return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    }
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static int32_t rd_i32(const uint8_t *p, int le)
{
    return (int32_t)rd_u32(p, le);
}

static char *dup_name(const uint8_t *p, uint32_t len)
{
    size_t n = 0;
    while (n < len && p[n] != 0) {
        ++n;
    }
    char *s = malloc(n + 1);
    if (!s) {
        return NULL;
    }
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

static void make_short_name(char *out, size_t outlen, const char *name)
{
    const char *p = name;
    if (strncmp(p, "ig", 2) == 0) {
        p += 2;
    }
    size_t n = strlen(p);
    if (n > 9 && strcmp(p + n - 9, "MetaField") == 0) {
        n -= 9;
    }
    if (n >= outlen) {
        n = outlen - 1;
    }
    memcpy(out, p, n);
    out[n] = 0;
}

static int is_mem_dir(const igb_ctx *c, uint32_t type_idx)
{
    return type_idx < c->n_meta_objs &&
           c->meta_objs[type_idx].name &&
           strcmp(c->meta_objs[type_idx].name, "igMemoryDirEntry") == 0;
}

static void ctx_free(igb_ctx *c)
{
    if (!c) {
        return;
    }
    if (c->meta_fields) {
        for (uint32_t i = 0; i < c->n_meta_fields; ++i) {
            free(c->meta_fields[i].short_name);
        }
        free(c->meta_fields);
    }
    if (c->meta_objs) {
        for (uint32_t i = 0; i < c->n_meta_objs; ++i) {
            free(c->meta_objs[i].name);
            free(c->meta_objs[i].fields);
        }
        free(c->meta_objs);
    }
    memset(c, 0, sizeof(*c));
}

static int deserialize_field(const igb_ctx *c, uint32_t type_idx, uint32_t slot,
                             uint32_t size, const uint8_t *data, size_t avail,
                             igb_fieldval *out)
{
    memset(out, 0, sizeof(*out));
    out->slot = (uint16_t)slot;
    out->type_idx = (uint16_t)type_idx;
    out->size = size;
    out->i32 = 0;
    out->blob = NULL;
    out->blob_len = 0;

    const char *sn = type_idx < c->n_meta_fields ? c->meta_fields[type_idx].short_name : "Unknown";
    snprintf(out->short_name, sizeof(out->short_name), "%s", sn ? sn : "Unknown");

    if (strcmp(out->short_name, "ObjectRef") == 0 ||
        strcmp(out->short_name, "MemoryRef") == 0) {
        out->i32 = rd_i32(data, c->f->is_le);
        return 0;
    }
    if (strcmp(out->short_name, "Float") == 0) {
        memcpy(&out->i32, data, 4);
        return 0;
    }
    if (strcmp(out->short_name, "String") == 0) {
        if (c->f->version >= 8) {
            out->i32 = rd_i32(data, c->f->is_le);
            return 0;
        }
        int32_t len = rd_i32(data, c->f->is_le);
        if (len > 0 && (size_t)len <= avail - 4) {
            out->blob = malloc((size_t)len + 1);
            memcpy(out->blob, data + 4, (size_t)len);
            out->blob[len] = 0;
            out->blob_len = (size_t)len;
        }
        return 0;
    }
    if (strcmp(out->short_name, "CharArray") == 0 ||
        strcmp(out->short_name, "UnsignedCharArray") == 0 ||
        strcmp(out->short_name, "FloatArray") == 0 ||
        strcmp(out->short_name, "IntArray") == 0 ||
        strcmp(out->short_name, "UnsignedIntArray") == 0 ||
        strcmp(out->short_name, "ShortArray") == 0 ||
        strcmp(out->short_name, "UnsignedShortArray") == 0 ||
        strcmp(out->short_name, "Vec2f") == 0 ||
        strcmp(out->short_name, "Vec3f") == 0 ||
        strcmp(out->short_name, "Vec4f") == 0 ||
        strcmp(out->short_name, "Quaternionf") == 0 ||
        strcmp(out->short_name, "Matrix44f") == 0) {
        if (size > 0 && size <= avail) {
            out->blob = malloc(size);
            memcpy(out->blob, data, size);
            out->blob_len = size;
        }
        return 0;
    }
    /* Scalar: read as the field size, store in i32. */
    uint32_t n = size < 4 ? size : 4;
    uint32_t v = 0;
    for (uint32_t k = 0; k < n; ++k) {
        v |= (uint32_t)data[k] << (8 * k);
    }
    out->i32 = (int32_t)v;
    return 0;
}

int igb_open(igb *f, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < X2_IGB_HEADER_SIZE) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    memset(f, 0, sizeof(*f));
    f->data = buf;
    f->size = (size_t)sz;

    uint32_t magic_le = rd_u32(buf + 0x28, 1);
    uint32_t magic_be = rd_u32(buf + 0x28, 0);
    if (magic_le == X2_IGB_MAGIC) {
        f->is_le = 1;
    } else if (magic_be == X2_IGB_MAGIC) {
        f->is_le = 0;
    } else {
        free(buf);
        memset(f, 0, sizeof(*f));
        return -1;
    }

    for (int i = 0; i < 12; ++i) {
        f->header[i] = rd_u32(buf + 4 * i, f->is_le);
    }
    uint32_t ver_flags = f->header[11];
    f->version = (int)(ver_flags & 0xFFFF);
    f->has_info = (ver_flags >> 31) & 1;
    f->has_external = (ver_flags >> 30) & 1;
    f->shared_entries = (ver_flags >> 29) & 1;
    f->has_memory_pool_names = (ver_flags >> 28) & 1;
    f->slot_offset = f->version <= 5 ? 1 : 0;
    f->info_list_index = -1;

    uint32_t entry_buffer_size = f->header[0];
    uint32_t entry_count = f->header[1];
    uint32_t mo_buffer_size = f->header[2];
    uint32_t mo_count = f->header[3];
    uint32_t obj_buffer_size = f->header[4];
    uint32_t mf_buffer_size = f->header[8];
    uint32_t mf_count = f->header[9];

    igb_ctx c;
    memset(&c, 0, sizeof(c));
    c.f = f;

    /* ---- Name pool (v8+) ---- */
    size_t pos = X2_IGB_HEADER_SIZE;
    if (f->version >= 8) {
        uint32_t buf_size = rd_u32(buf + pos, f->is_le);
        pos += buf_size;
    }

    /* ---- Meta-field buffer ---- */
    c.n_meta_fields = mf_count;
    c.meta_fields = calloc(mf_count ? mf_count : 1, sizeof(igb_meta_field));
    if (mf_count) {
        const uint8_t *mf_static = buf + pos;
        const uint8_t *mf_dyn = mf_static + (size_t)mf_count * 12;
        size_t dyn_off = 0;
        for (uint32_t i = 0; i < mf_count; ++i) {
            uint32_t name_len = rd_u32(mf_static + (size_t)i * 12, f->is_le);
            const char *name = dup_name(mf_dyn + dyn_off, name_len);
            c.meta_fields[i].short_name = malloc(32);
            if (name) {
                make_short_name(c.meta_fields[i].short_name, 32, name);
                free((char *)name);
            } else {
                strcpy(c.meta_fields[i].short_name, "Unknown");
            }
            dyn_off += name_len;
        }
    }
    pos += mf_buffer_size;

    /* ---- Alignment buffer ---- */
    uint32_t align_size = rd_u32(buf + pos, f->is_le);
    pos += align_size;

    /* ---- Meta-object buffer ---- */
    c.n_meta_objs = mo_count;
    c.meta_objs = calloc(mo_count ? mo_count : 1, sizeof(igb_meta_obj));
    if (mo_count) {
        const uint8_t *mo_static = buf + pos;
        const uint8_t *mo_dyn = mo_static + (size_t)mo_count * 24;
        size_t dyn_off = 0;
        for (uint32_t i = 0; i < mo_count; ++i) {
            const uint8_t *st = mo_static + (size_t)i * 24;
            uint32_t name_len = rd_u32(st, f->is_le);
            uint32_t nf = rd_u32(st + 12, f->is_le);
            int32_t parent = rd_i32(st + 16, f->is_le);
            c.meta_objs[i].name = dup_name(mo_dyn + dyn_off, name_len);
            c.meta_objs[i].parent = parent;
            c.meta_objs[i].n_fields = nf;
            c.meta_objs[i].fields = calloc(nf ? nf : 1, sizeof(igb_meta_field_desc));
            dyn_off += name_len;
            for (uint32_t j = 0; j < nf; ++j) {
                c.meta_objs[i].fields[j].type_idx = rd_u16(mo_dyn + dyn_off, f->is_le);
                c.meta_objs[i].fields[j].slot = rd_u16(mo_dyn + dyn_off + 2, f->is_le);
                c.meta_objs[i].fields[j].size = rd_u16(mo_dyn + dyn_off + 4, f->is_le);
                dyn_off += 6;
            }
        }
    }
    pos += mo_buffer_size;

    /* ---- External directories / memory pool names ---- */
    if (f->has_external) {
        uint32_t buf_size = rd_u32(buf + pos, f->is_le);
        pos += buf_size;
    }
    if (f->has_memory_pool_names) {
        uint32_t buf_size = rd_u32(buf + pos, f->is_le);
        pos += buf_size;
    }

    /* ---- Entry buffer ---- */
    size_t ep = pos;
    const uint8_t *eb = buf + ep;
    uint32_t *entry_types = calloc(entry_count ? entry_count : 1, sizeof(uint32_t));
    uint32_t *entry_sizes = calloc(entry_count ? entry_count : 1, sizeof(uint32_t));
    size_t *entry_offsets = malloc((size_t)(entry_count ? entry_count : 1) * sizeof(size_t));
    if (!entry_types || !entry_sizes || !entry_offsets) {
        free(entry_types);
        free(entry_sizes);
        free(entry_offsets);
        ctx_free(&c);
        free(buf);
        memset(f, 0, sizeof(*f));
        return -1;
    }
    {
        size_t eoff = 0;
        for (uint32_t i = 0; i < entry_count; ++i) {
            entry_offsets[i] = eoff;
            entry_types[i] = rd_u32(eb + eoff, f->is_le);
            entry_sizes[i] = rd_u32(eb + eoff + 4, f->is_le);
            eoff += entry_sizes[i];
        }
    }

    /* ---- Index buffer ---- */
    size_t ip = ep + entry_buffer_size;
    uint32_t idx_buf_size = rd_u32(buf + ip, f->is_le);
    uint32_t num_idx = rd_u32(buf + ip + 4, f->is_le);
    int *index_map = malloc((size_t)num_idx * sizeof(int));
    if (!index_map) {
        free(entry_types);
        free(entry_sizes);
        free(entry_offsets);
        ctx_free(&c);
        free(buf);
        memset(f, 0, sizeof(*f));
        return -1;
    }
    for (uint32_t i = 0; i < num_idx; ++i) {
        index_map[i] = rd_u16(buf + ip + 8 + 2 * i, f->is_le);
    }
    ip += idx_buf_size;
    if (f->version >= 6 && f->has_info) {
        f->info_list_index = (int)rd_u32(buf + ip, f->is_le);
        ip += 4;
    }

    /* ---- Build slot list: classify each index-map entry ---- */
    const int s = f->slot_offset;
    uint8_t *is_mem = calloc(num_idx, 1);
    int32_t *mem_sizes = calloc(num_idx, sizeof(int32_t));
    uint32_t *mem_types = malloc((size_t)num_idx * sizeof(uint32_t));
    uint32_t *obj_types = malloc((size_t)num_idx * sizeof(uint32_t));
    if (!is_mem || !mem_sizes || !mem_types || !obj_types) {
        free(index_map);
        free(entry_types);
        free(entry_sizes);
        free(entry_offsets);
        ctx_free(&c);
        free(buf);
        memset(f, 0, sizeof(*f));
        return -1;
    }
    for (uint32_t i = 0; i < num_idx; ++i) {
        int idx = index_map[i];
        if (idx < 0 || (uint32_t)idx >= entry_count) {
            continue;
        }
        const uint8_t *ent = eb + entry_offsets[(size_t)idx];
        uint32_t ent_type = entry_types[idx];
        uint32_t ent_size = entry_sizes[idx];
        if (is_mem_dir(&c, ent_type)) {
            is_mem[i] = 1;
            const uint8_t *fd = ent + 8;
            size_t doff = 0;
            const igb_meta_obj *mo = &c.meta_objs[ent_type];
            for (uint32_t k = 0; k < mo->n_fields && doff + 4 <= ent_size; ++k) {
                uint16_t slot = mo->fields[k].slot;
                uint16_t size = mo->fields[k].size;
                uint32_t val = rd_u32(fd + doff, f->is_le);
                if (slot == 7 + s) {
                    mem_sizes[i] = (int32_t)val;
                } else if (slot == 10 + s) {
                    mem_types[i] = val;
                }
                doff += (size + 3) & ~3u;
            }
        } else {
            is_mem[i] = 0;
            const uint8_t *fd = ent + 8;
            size_t doff = 0;
            const igb_meta_obj *mo = &c.meta_objs[ent_type];
            for (uint32_t k = 0; k < mo->n_fields && doff + 4 <= ent_size; ++k) {
                uint16_t slot = mo->fields[k].slot;
                uint16_t size = mo->fields[k].size;
                uint32_t val = rd_u32(fd + doff, f->is_le);
                if (slot == 11 + s) {
                    obj_types[i] = val;
                }
                doff += (size + 3) & ~3u;
            }
        }
    }

    /* ---- Object buffer ---- */
    size_t op = ip;
    f->n_objects = (int)num_idx;
    f->objects = calloc(num_idx ? num_idx : 1, sizeof(igb_object));
    {
        size_t opos = op;
        for (uint32_t i = 0; i < num_idx; ++i) {
            if (is_mem[i]) {
                continue;
            }
            if (opos + 8 > f->size) {
                break;
            }
            uint32_t type = rd_u32(buf + opos, f->is_le);
            uint32_t size = rd_u32(buf + opos + 4, f->is_le);
            const uint8_t *od = buf + opos + 8;
            igb_object *obj = &f->objects[i];
            obj->is_mem = 0;
            if (type < c.n_meta_objs && c.meta_objs[type].name) {
                obj->type_name = strdup(c.meta_objs[type].name);
            }
            const igb_meta_obj *mo = type < c.n_meta_objs ? &c.meta_objs[type] : NULL;
            if (mo) {
                obj->n_fields = (int)mo->n_fields;
                obj->fields = calloc(mo->n_fields ? mo->n_fields : 1, sizeof(igb_fieldval));
                size_t doff = 0;
                int nf = 0;
                for (uint32_t k = 0; k < mo->n_fields && doff + 4 <= size; ++k) {
                    uint16_t slot = mo->fields[k].slot;
                    uint16_t ftype = mo->fields[k].type_idx;
                    uint16_t fsize = mo->fields[k].size;
                    const char *sn = ftype < c.n_meta_fields ? c.meta_fields[ftype].short_name : "Unknown";
                    size_t avail = size - doff;
                    igb_fieldval *fv = &obj->fields[nf];
                    deserialize_field(&c, ftype, slot, fsize, od + doff, avail, fv);
                    size_t advance = (fsize + 3) & ~3u;
                    if (sn && strcmp(sn, "String") == 0 && c.f->version < 8) {
                        int32_t len = rd_i32(od + doff, f->is_le);
                        advance = (4 + len + 3) & ~3u;
                    }
                    doff += advance;
                    ++nf;
                }
                obj->n_fields = nf;
            }
            opos += size;
        }
    }

    /* ---- Memory reference buffer ---- */
    {
        size_t mpos = op + obj_buffer_size;
        for (uint32_t i = 0; i < num_idx; ++i) {
            if (!is_mem[i]) {
                continue;
            }
            int32_t msize = mem_sizes[i];
            if (msize <= 0 || mpos + (size_t)msize > f->size) {
                continue;
            }
            igb_object *obj = &f->objects[i];
            obj->is_mem = 1;
            obj->mem = malloc((size_t)msize);
            if (obj->mem) {
                memcpy(obj->mem, buf + mpos, (size_t)msize);
                obj->mem_size = (size_t)msize;
            }
            if (mem_types[i] < c.n_meta_objs && c.meta_objs[mem_types[i]].name) {
                obj->type_name = strdup(c.meta_objs[mem_types[i]].name);
            }
            mpos += ((size_t)msize + 3) & ~3u;
        }
    }

    free(index_map);
    free(entry_types);
    free(entry_sizes);
    free(entry_offsets);
    free(is_mem);
    free(mem_sizes);
    free(mem_types);
    free(obj_types);
    ctx_free(&c);
    return 0;
}

void igb_close(igb *f)
{
    if (!f) {
        return;
    }
    for (int i = 0; i < f->n_objects; ++i) {
        free(f->objects[i].type_name);
        if (f->objects[i].fields) {
            for (int j = 0; j < f->objects[i].n_fields; ++j) {
                free(f->objects[i].fields[j].blob);
            }
            free(f->objects[i].fields);
        }
        free(f->objects[i].mem);
    }
    free(f->objects);
    free(f->data);
    memset(f, 0, sizeof(*f));
}

const igb_object *igb_object_by_index(const igb *f, int index)
{
    if (!f || index < 0 || index >= f->n_objects) {
        return NULL;
    }
    return &f->objects[index];
}

const igb_fieldval *igb_object_field(const igb_object *obj, uint16_t slot)
{
    if (!obj) {
        return NULL;
    }
    for (int i = 0; i < obj->n_fields; ++i) {
        if (obj->fields[i].slot == slot) {
            return &obj->fields[i];
        }
    }
    return NULL;
}

int igb_find_images(const igb *f, igb_image *out, int max)
{
    if (!f || !out || max <= 0) {
        return 0;
    }
    int count = 0;
    int s = f->slot_offset;
    for (int i = 0; i < f->n_objects; ++i) {
        const igb_object *obj = &f->objects[i];
        if (obj->is_mem || !obj->type_name || strcmp(obj->type_name, "igImage") != 0) {
            continue;
        }
        const igb_fieldval *w = igb_object_field(obj, (uint16_t)(2 + s));
        const igb_fieldval *h = igb_object_field(obj, (uint16_t)(3 + s));
        const igb_fieldval *nc = igb_object_field(obj, (uint16_t)(4 + s));
        const igb_fieldval *pf = igb_object_field(obj, (uint16_t)(11 + s));
        const igb_fieldval *is = igb_object_field(obj, (uint16_t)(12 + s));
        const igb_fieldval *pd = igb_object_field(obj, (uint16_t)(13 + s));
        const igb_fieldval *bpr = igb_object_field(obj, (uint16_t)(19 + s));
        const igb_fieldval *cmp = igb_object_field(obj, (uint16_t)(20 + s));
        const igb_fieldval *nm = igb_object_field(obj, (uint16_t)(22 + s));
        if (!w || !h || !pf || !pd || pd->short_name[0] == 0) {
            continue;
        }
        int ref = pd->i32;
        if (ref < 0 || ref >= f->n_objects || !f->objects[ref].is_mem) {
            continue;
        }
        igb_image *img = &out[count];
        memset(img, 0, sizeof(*img));
        img->width = w->i32;
        img->height = h->i32;
        img->num_components = nc ? nc->i32 : 0;
        img->pixel_format = pf->i32;
        img->image_size = is ? is->i32 : 0;
        img->bytes_per_row = bpr ? bpr->i32 : 0;
        img->compressed = cmp ? (cmp->i32 != 0) : 1;
        img->data = f->objects[ref].mem;
        img->data_len = f->objects[ref].mem_size;
        img->name = (nm && nm->blob) ? (char *)nm->blob : NULL;
        ++count;
        if (count >= max) {
            break;
        }
    }
    return count;
}

/* ---- DXT decode ---- */

static void rgb565_to_rgba(uint16_t c, uint8_t out[4])
{
    out[0] = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    out[1] = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
    out[2] = (uint8_t)(((c & 0x1F) * 255) / 31);
    out[3] = 255;
}

static void decode_dxt1_block(const uint8_t *p, uint8_t out[16][4])
{
    uint16_t c0raw = (uint16_t)(p[0] | (p[1] << 8));
    uint16_t c1raw = (uint16_t)(p[2] | (p[3] << 8));
    uint32_t idx = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    uint8_t c0[4], c1[4];
    rgb565_to_rgba(c0raw, c0);
    rgb565_to_rgba(c1raw, c1);
    uint8_t pal[4][4];
    memcpy(pal[0], c0, 4);
    memcpy(pal[1], c1, 4);
    if (c0raw > c1raw) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (uint8_t)((2 * c0[k] + c1[k] + 1) / 3);
            pal[3][k] = (uint8_t)((c0[k] + 2 * c1[k] + 1) / 3);
        }
        pal[2][3] = 255;
        pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (uint8_t)((c0[k] + c1[k]) / 2);
        }
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    for (int i = 0; i < 16; ++i) {
        int bi = ((i >> 2) & 3) * 4 + (i & 3);
        int sel = (idx >> (bi * 2)) & 3;
        memcpy(out[i], pal[sel], 4);
    }
}

static void decode_dxt3_block(const uint8_t *p, uint8_t out[16][4])
{
    uint8_t col[16][4];
    decode_dxt1_block(p + 8, col);
    for (int i = 0; i < 16; ++i) {
        int byte = i >> 1;
        int hi = i & 1;
        uint8_t a4 = (uint8_t)((p[byte] >> (hi ? 4 : 0)) & 0x0F);
        uint8_t a = (uint8_t)((a4 << 4) | a4);
        memcpy(out[i], col[i], 3);
        out[i][3] = a;
    }
}

static void decode_dxt5_block(const uint8_t *p, uint8_t out[16][4])
{
    uint8_t a0 = p[0];
    uint8_t a1 = p[1];
    uint8_t apal[8];
    if (a0 > a1) {
        apal[0] = a0;
        apal[1] = a1;
        apal[2] = (uint8_t)((6 * a0 + a1 + 3) / 7);
        apal[3] = (uint8_t)((5 * a0 + 2 * a1 + 3) / 7);
        apal[4] = (uint8_t)((4 * a0 + 3 * a1 + 3) / 7);
        apal[5] = (uint8_t)((3 * a0 + 4 * a1 + 3) / 7);
        apal[6] = (uint8_t)((2 * a0 + 5 * a1 + 3) / 7);
        apal[7] = (uint8_t)((a0 + 6 * a1 + 3) / 7);
    } else {
        apal[0] = a0;
        apal[1] = a1;
        apal[2] = (uint8_t)((4 * a0 + a1 + 2) / 5);
        apal[3] = (uint8_t)((3 * a0 + 2 * a1 + 2) / 5);
        apal[4] = (uint8_t)((2 * a0 + 3 * a1 + 2) / 5);
        apal[5] = (uint8_t)((a0 + 4 * a1 + 2) / 5);
        apal[6] = 0;
        apal[7] = 255;
    }
    uint64_t abits = 0;
    for (int k = 0; k < 6; ++k) {
        abits |= (uint64_t)p[2 + k] << (8 * k);
    }
    uint8_t col[16][4];
    decode_dxt1_block(p + 8, col);
    for (int i = 0; i < 16; ++i) {
        int bi = ((i >> 2) & 3) * 4 + (i & 3);
        int sel = (int)((abits >> (bi * 3)) & 7);
        memcpy(out[i], col[i], 3);
        out[i][3] = apal[sel];
    }
}

uint8_t *igb_image_to_rgba(const igb_image *img, int *out_len)
{
    *out_len = 0;
    if (!img || !img->data || img->width <= 0 || img->height <= 0) {
        return NULL;
    }
    int w = img->width;
    int h = img->height;
    size_t out_size = (size_t)w * h * 4;
    uint8_t *out = malloc(out_size);
    if (!out) {
        return NULL;
    }

    if (img->pixel_format == X2_IGB_PFMT_RGBA8888) {
        if (img->data_len < out_size) {
            free(out);
            return NULL;
        }
        memcpy(out, img->data, out_size);
        *out_len = (int)out_size;
        return out;
    }
    if (img->pixel_format == X2_IGB_PFMT_RGB888) {
        if (img->data_len < (size_t)w * h * 3) {
            free(out);
            return NULL;
        }
        for (int i = 0; i < w * h; ++i) {
            out[i * 4] = img->data[i * 3];
            out[i * 4 + 1] = img->data[i * 3 + 1];
            out[i * 4 + 2] = img->data[i * 3 + 2];
            out[i * 4 + 3] = 255;
        }
        *out_len = (int)out_size;
        return out;
    }
    if (img->pixel_format == X2_IGB_PFMT_RGB565) {
        if (img->data_len < (size_t)w * h * 2) {
            free(out);
            return NULL;
        }
        for (int i = 0; i < w * h; ++i) {
            uint16_t v = (uint16_t)(img->data[i * 2] | (img->data[i * 2 + 1] << 8));
            rgb565_to_rgba(v, out + i * 4);
        }
        *out_len = (int)out_size;
        return out;
    }

    int blocks_x = (w + 3) / 4;
    int blocks_y = (h + 3) / 4;
    int block_size = (img->pixel_format == X2_IGB_PFMT_RGB_DXT1 ||
                      img->pixel_format == X2_IGB_PFMT_RGBA_DXT1) ? 8 : 16;
    size_t need = (size_t)blocks_x * blocks_y * block_size;
    if (img->data_len < need) {
        free(out);
        return NULL;
    }

    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            size_t bo = (size_t)(by * blocks_x + bx) * block_size;
            uint8_t px[16][4];
            switch (img->pixel_format) {
            case X2_IGB_PFMT_RGB_DXT1:
            case X2_IGB_PFMT_RGBA_DXT1:
                decode_dxt1_block(img->data + bo, px);
                break;
            case X2_IGB_PFMT_RGBA_DXT3:
                decode_dxt3_block(img->data + bo, px);
                break;
            case X2_IGB_PFMT_RGBA_DXT5:
                decode_dxt5_block(img->data + bo, px);
                break;
            default:
                free(out);
                return NULL;
            }
            for (int row = 0; row < 4; ++row) {
                int py = by * 4 + row;
                if (py >= h) {
                    break;
                }
                for (int col = 0; col < 4; ++col) {
                    int px_x = bx * 4 + col;
                    if (px_x >= w) {
                        continue;
                    }
                    size_t o = ((size_t)py * w + px_x) * 4;
                    memcpy(out + o, px[row * 4 + col], 4);
                }
            }
        }
    }
    *out_len = (int)out_size;
    return out;
}
