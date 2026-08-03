#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igb.h"
#include "igb_anim.h"

static void dump_types(const igb *f)
{
    const igb_object *any = NULL;
    for (int i = 0; i < f->n_objects; ++i) {
        if (!f->objects[i].is_mem && f->objects[i].type_name) {
            any = &f->objects[i];
            break;
        }
    }
    (void)any;    /* type inventory: count per type_name */
    int cap = 128, n = 0;
    char **names = malloc((size_t)cap * sizeof(char *));
    int *counts = malloc((size_t)cap * sizeof(int));
    int *idx = malloc((size_t)cap * sizeof(int));
    for (int i = 0; i < f->n_objects; ++i) {
        const igb_object *o = &f->objects[i];
        const char *tn = o->type_name ? o->type_name : (o->is_mem ? "igMemoryBlock" : "?");
        int j = 0;
        for (; j < n; ++j) {
            if (strcmp(names[j], tn) == 0) {
                break;
            }
        }
        if (j == n) {
            if (n == cap) {
                cap *= 2;
                names = realloc(names, (size_t)cap * sizeof(char *));
                counts = realloc(counts, (size_t)cap * sizeof(int));
                idx = realloc(idx, (size_t)cap * sizeof(int));
            }
            names[n] = strdup(tn);
            counts[n] = 0;
            idx[n] = i;
            ++n;
        }
        ++counts[j];
    }
    printf("object types:\n");
    for (int i = 0; i < n; ++i) {
        printf("  %-44s %5d   (first @%d)%s\n", names[i], counts[i], idx[i],
               idx[i] < f->n_objects && f->objects[idx[i]].is_mem ? " [mem]" : "");
    }
    for (int i = 0; i < n; ++i) {
        free(names[i]);
    }
    free(names);
    free(counts);
    free(idx);
}

static void dump_object(const igb *f, int idx)
{
    const igb_object *o = igb_object_by_index(f, idx);
    if (!o) {
        printf("no object at %d\n", idx);
        return;
    }
    printf("object[%d] type=%s is_mem=%d mem_size=%zu n_fields=%d\n",
           idx, o->type_name ? o->type_name : "?", o->is_mem, o->mem_size, o->n_fields);
    for (int i = 0; i < o->n_fields; ++i) {
        const igb_fieldval *fv = &o->fields[i];
        if (fv->blob && fv->blob_len) {
            printf("  slot=%-3u type=%-18s size=%-4u blob=%zu bytes:",
                   fv->slot, fv->short_name, fv->size, fv->blob_len);
            size_t nb = fv->blob_len;
            if (nb > 64) {
                nb = 64;
            }
            for (size_t k = 0; k < nb; ++k) {
                printf(" %02x", fv->blob[k]);
            }
            printf("\n");
        } else if (strcmp(fv->short_name, "String") == 0 && fv->i32) {
            printf("  slot=%-3u type=%-18s size=%-4u i32=%d\n",
                   fv->slot, fv->short_name, fv->size, fv->i32);
        } else {
            printf("  slot=%-3u type=%-18s size=%-4u i32=%d\n",
                   fv->slot, fv->short_name, fv->size, fv->i32);
        }
    }
}

static void dump_meta(const igb *f, const char *want)
{
    for (int i = 0; i < f->n_meta; ++i) {
        const igb_meta *m = &f->meta[i];
        if (!m->name || (want && strcmp(m->name, want) != 0)) {
            continue;
        }
        const igb_meta *p = m->parent >= 0 ? igb_meta_by_index(f, m->parent) : NULL;
        printf("meta[%d] %s  (parent=%s)  %d fields\n", i, m->name,
               p ? p->name : "-", m->n_fields);
        for (int j = 0; j < m->n_fields; ++j) {
            printf("    slot=%-3u type=%-18s size=%-4u\n",
                   m->fields[j].slot,
                   igb_metafield_name(f, m->fields[j].type_idx),
                   m->fields[j].size);
        }
    }
}

static void dump_mem(const igb *f, int idx, int maxbytes)
{
    const igb_object *o = igb_object_by_index(f, idx);
    if (!o) {
        printf("no object at %d\n", idx);
        return;
    }
    printf("object[%d] type=%s is_mem=%d mem_size=%zu\n",
           idx, o->type_name ? o->type_name : "?", o->is_mem, o->mem_size);
    if (!o->mem || o->mem_size == 0) {
        return;
    }
    size_t n = o->mem_size;
    if (maxbytes > 0 && (size_t)maxbytes < n) {
        n = (size_t)maxbytes;
    }
    for (size_t i = 0; i < n; i += 16) {
        printf("  %04zx:", i);
        for (size_t j = 0; j < 16 && i + j < n; ++j) {
            printf(" %02x", o->mem[i + j]);
        }
        printf("\n");
    }
}

static void dump_scan(const igb *f, const char *want, const char *slots_csv)
{
    int nslots = 0;
    int slots[8];
    if (slots_csv) {
        const char *p = slots_csv;
        while (*p && nslots < 8) {
            slots[nslots++] = atoi(p);
            while (*p && *p != ',') {
                ++p;
            }
            if (*p == ',') {
                ++p;
            }
        }
    }
    for (int i = 0; i < f->n_objects; ++i) {
        const igb_object *o = &f->objects[i];
        if (!o->type_name || strstr(o->type_name, want) == NULL) {
            continue;
        }
        printf("object[%d] %s %s mem=%zu\n", i, o->type_name,
               o->is_mem ? "[mem]" : "", o->mem_size);
        for (int k = 0; k < nslots; ++k) {
            const igb_fieldval *fv = igb_object_field(o, (uint16_t)slots[k]);
            if (fv) {
                if (fv->blob && fv->short_name && strstr(fv->short_name, "String")) {
                    printf("    slot=%d %s str=\"%.*s\"\n", slots[k], fv->short_name,
                           fv->blob_len > 0 ? (int)fv->blob_len : 1,
                           fv->blob_len > 0 ? (const char *)fv->blob : "");
                } else {
                    printf("    slot=%d %s i32=%d\n", slots[k], fv->short_name, fv->i32);
                }
            }
        }
    }
}

static void dump_parents(const igb *f, int target)
{
    int n = f->n_objects;
    int *parent = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; ++i) {
        parent[i] = -1;
    }
    for (int i = 0; i < n; ++i) {
        const igb_object *o = &f->objects[i];
        if (o->is_mem || !o->type_name) {
            continue;
        }
        const igb_fieldval *nl = igb_object_field(o, 7);
        if (!nl) {
            continue;
        }
        int list_idx = nl->i32;
        if (list_idx < 0 || list_idx >= n || !f->objects[list_idx].is_mem) {
            const igb_object *lo = list_idx >= 0 && list_idx < n ? &f->objects[list_idx] : NULL;
            if (!lo || lo->is_mem || !lo->type_name || strcmp(lo->type_name, "igNodeList") != 0) {
                continue;
            }
        }
        /* node list data block */
        const igb_fieldval *dl = igb_object_field(&f->objects[list_idx], 4);
        const igb_fieldval *ct = igb_object_field(&f->objects[list_idx], 2);
        if (!dl || dl->i32 < 0 || dl->i32 >= n) {
            continue;
        }
        const igb_object *do_ = &f->objects[dl->i32];
        if (!do_->is_mem) {
            continue;
        }
        int cnt = ct ? ct->i32 : 0;
        if (cnt <= 0 || cnt * 4 > (int)do_->mem_size) {
            continue;
        }
        for (int k = 0; k < cnt; ++k) {
            const uint8_t *p = do_->mem + 4 * k;
            int child = (int)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
            if (child >= 0 && child < n && parent[child] < 0) {
                parent[child] = i;
            }
        }
    }
    int cur = target;
    int depth = 0;
    while (cur >= 0 && depth < 64) {
        const igb_object *o = &f->objects[cur];
        printf("%*sobject[%d] %s", depth * 2, "", cur, o->type_name ? o->type_name : "?");
        if (!o->is_mem) {
            const igb_fieldval *al = igb_object_field(o, 8);
            if (al && al->i32 >= 0) {
                printf(" attrList->%d", al->i32);
            }
        }
        printf("\n");
        cur = parent[cur];
        ++depth;
    }
    free(parent);
}

static void dump_anims(const igb *f, int want_idx, int want_frame)
{
    int found = 0;
    for (int i = 0; i < f->n_objects; ++i) {
        const igb_object *o = &f->objects[i];
        if (o->is_mem || !o->type_name ||
            strcmp(o->type_name, "igEnbayaAnimationSource") != 0) {
            continue;
        }
        if (want_idx >= 0 && want_idx != i) {
            continue;
        }
        ++found;
        const igb_fieldval *fv = igb_object_field(o, 2);
        if (!fv || fv->i32 < 0 || fv->i32 >= f->n_objects) {
            printf("anim[%d] igEnbayaAnimationSource: no MemoryRef\n", i);
            continue;
        }
        const igb_object *mo = &f->objects[fv->i32];
        if (!mo->is_mem || !mo->mem || mo->mem_size < 0x50) {
            printf("anim[%d] igEnbayaAnimationSource: blob obj=%d not mem (mem=%zu)\n",
                   i, fv->i32, mo->mem_size);
            continue;
        }
        igb_enbaya_anim a;
        if (igb_enbaya_decode(mo->mem, mo->mem_size, &a) != 0) {
            printf("anim[%d] igEnbayaAnimationSource: decode failed (blob obj=%d, %zu bytes)\n",
                   i, fv->i32, mo->mem_size);
            continue;
        }
        printf("anim[%d] source=%d blob=%d bytes=%zu tracks=%d duration=%.3fs fps=%.1f frames=%d\n",
               i, i, fv->i32, mo->mem_size, a.track_count, a.duration,
               1.0f / a.interval, a.frame_count);
        if (want_frame < 0) {
            igb_enbaya_free(&a);
            continue;
        }
        if (want_frame >= a.frame_count) {
            want_frame = a.frame_count - 1;
        }
        for (int t = 0; t < a.track_count; ++t) {
            const igb_enbaya_pose *p = &a.poses[(size_t)want_frame * a.track_count + t];
            printf("  t%-3d quat(% .4f,% .4f,% .4f,% .4f) pos(% .3f,% .3f,% .3f)\n",
                   t, p->quat[0], p->quat[1], p->quat[2], p->quat[3],
                   p->pos[0], p->pos[1], p->pos[2]);
        }
        igb_enbaya_free(&a);
        if (want_idx >= 0) {
            break;
        }
    }
    if (want_idx >= 0 && !found) {
        printf("no igEnbayaAnimationSource object at %d\n", want_idx);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: igb_dump <file.igb> [img_index [out]] | igb_dump -types <file.igb>\n");
        return 1;
    }
    const char *path = argv[1];
    int types_mode = 0;
    int obj_mode = 0;
    int obj_index = -1;
    const char *meta_want = NULL;
    int meta_mode = 0;
    int mem_mode = 0;
    int mem_index = -1;
    int mem_max = 0;
    int scan_mode = 0;
    const char *scan_want = NULL;
    const char *scan_slots = NULL;
    int parents_mode = 0;
    int parents_idx = -1;
    int anim_mode = 0;
    int anim_idx = -1;
    int anim_frame = -1;
    if (strcmp(path, "-types") == 0) {
        types_mode = 1;
        if (argc < 3) {
            fprintf(stderr, "usage: igb_dump -types <file.igb>\n");
            return 1;
        }
        path = argv[2];
    } else if (strcmp(path, "-obj") == 0) {
        obj_mode = 1;
        if (argc < 4) {
            fprintf(stderr, "usage: igb_dump -obj <file.igb> <object_index>\n");
            return 1;
        }
        path = argv[2];
        obj_index = atoi(argv[3]);
    } else if (strcmp(path, "-meta") == 0) {
        meta_mode = 1;
        if (argc < 4) {
            fprintf(stderr, "usage: igb_dump -meta <file.igb> <type_name>\n");
            return 1;
        }
        path = argv[2];
        meta_want = argv[3];
    } else if (strcmp(path, "-mem") == 0) {
        mem_mode = 1;
        if (argc < 4) {
            fprintf(stderr, "usage: igb_dump -mem <file.igb> <object_index> [maxbytes]\n");
            return 1;
        }
        path = argv[2];
        mem_index = atoi(argv[3]);
        if (argc >= 5) {
            mem_max = atoi(argv[4]);
        }
    } else if (strcmp(path, "-scan") == 0) {
        /* usage: igb_dump -scan <file.igb> <type_substring> [slots...] */
        if (argc < 4) {
            fprintf(stderr, "usage: igb_dump -scan <file.igb> <type_substring> [slot,...]\n");
            return 1;
        }
        path = argv[2];
        scan_want = argv[3];
        if (argc >= 5) {
            scan_slots = argv[4];
        }
        scan_mode = 1;
    } else if (strcmp(path, "-parents") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: igb_dump -parents <file.igb> <object_index>\n");
            return 1;
        }
        path = argv[2];
        parents_idx = atoi(argv[3]);
        parents_mode = 1;
    } else if (strcmp(path, "-anim") == 0) {
        /* usage: igb_dump -anim <file.igb> [source_index] [frame_index] */
        if (argc < 3) {
            fprintf(stderr, "usage: igb_dump -anim <file.igb> [source_index] [frame_index]\n");
            return 1;
        }
        path = argv[2];
        if (argc >= 4) {
            anim_idx = atoi(argv[3]);
        }
        if (argc >= 5) {
            anim_frame = atoi(argv[4]);
        }
        anim_mode = 1;
    }
    igb f;
    if (igb_open(&f, path) != 0) {
        fprintf(stderr, "igb_open failed: %s\n", path);
        return 1;
    }
    printf("version=%d flags(info=%d ext=%d shared=%d mpool=%d) objects=%d info_index=%d\n",
           f.version, f.has_info, f.has_external, f.shared_entries,
           f.has_memory_pool_names, f.n_objects, f.info_list_index);

    if (types_mode) {
        dump_types(&f);
        igb_close(&f);
        return 0;
    }
    if (meta_mode) {
        dump_meta(&f, meta_want);
        igb_close(&f);
        return 0;
    }
    if (obj_mode) {
        dump_object(&f, obj_index);
        igb_close(&f);
        return 0;
    }
    if (mem_mode) {
        dump_mem(&f, mem_index, mem_max);
        igb_close(&f);
        return 0;
    }
    if (scan_mode) {
        dump_scan(&f, scan_want, scan_slots);
        igb_close(&f);
        return 0;
    }
    if (parents_mode) {
        dump_parents(&f, parents_idx);
        igb_close(&f);
        return 0;
    }
    if (anim_mode) {
        dump_anims(&f, anim_idx, anim_frame);
        igb_close(&f);
        return 0;
    }
        igb_image imgs[128];
        int n = igb_find_images(&f, imgs, 128);
        printf("images=%d\n", n);
        for (int i = 0; i < n; ++i) {
            printf("  [%d] %dx%d pfmt=%d size=%d bpr=%d comp=%d datalen=%zu name=%s\n",
                   i, imgs[i].width, imgs[i].height, imgs[i].pixel_format,
                   imgs[i].image_size, imgs[i].bytes_per_row, imgs[i].compressed,
                   imgs[i].data_len, imgs[i].name ? imgs[i].name : "-");
        }

        if (argc >= 3) {
            int idx = atoi(argv[2]);
            if (idx >= 0 && idx < n) {
                int len = 0;
                uint8_t *rgba = igb_image_to_rgba(&imgs[idx], &len);
                if (rgba) {
                    printf("rgba %dx%d len=%d (expected %d)\n", imgs[idx].width,
                           imgs[idx].height, len, imgs[idx].width * imgs[idx].height * 4);
                    if (argc >= 4) {
                        FILE *of = fopen(argv[3], "wb");
                        if (of) {
                            fwrite(rgba, 1, (size_t)len, of);
                            fclose(of);
                            printf("wrote %s\n", argv[3]);
                        }
                    }
                    free(rgba);
                } else {
                    printf("rgba decode failed\n");
                }
            }
        }

    igb_close(&f);
    return 0;
}