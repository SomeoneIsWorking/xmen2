#ifndef X2_IGB_H
#define X2_IGB_H

#include <stddef.h>
#include <stdint.h>

enum {
    X2_IGB_PFMT_L8 = 0,
    X2_IGB_PFMT_A8 = 1,
    X2_IGB_PFMT_RGB888 = 5,
    X2_IGB_PFMT_RGBA8888 = 7,
    X2_IGB_PFMT_RGBA5551 = 8,
    X2_IGB_PFMT_RGBA4444 = 9,
    X2_IGB_PFMT_RGB565 = 10,
    X2_IGB_PFMT_RGB_DXT1 = 13,
    X2_IGB_PFMT_RGBA_DXT1 = 14,
    X2_IGB_PFMT_RGBA_DXT3 = 15,
    X2_IGB_PFMT_RGBA_DXT5 = 16
};

typedef struct {
    uint16_t slot;
    uint16_t type_idx;
    uint32_t size;
    int32_t i32;
    uint8_t *blob;
    size_t blob_len;
    char short_name[32];
} igb_fieldval;

typedef struct {
    uint8_t is_mem;
    char *type_name;
    igb_fieldval *fields;
    int n_fields;
    uint8_t *mem;
    size_t mem_size;
} igb_object;

typedef struct {
    char *name;
    int32_t parent;
    int n_fields;
    struct {
        uint16_t type_idx;
        uint16_t slot;
        uint16_t size;
    } *fields;
} igb_meta;

typedef struct {
    char *name;
} igb_metafield;

typedef struct {
    uint32_t header[12];
    int version;
    int has_info;
    int has_external;
    int shared_entries;
    int has_memory_pool_names;
    uint8_t *data;
    size_t size;
    igb_object *objects;
    int n_objects;
    int info_list_index;
    int slot_offset;
    int is_le;
    igb_meta *meta;
    int n_meta;
    igb_metafield *metafields;
    int n_metafields;
} igb;

typedef struct {
    int width, height;
    int num_components;
    int pixel_format;
    int image_size;
    int bytes_per_row;
    int compressed;
    const uint8_t *data;
    size_t data_len;
    char *name;
} igb_image;

int igb_open(igb *f, const char *path);
void igb_close(igb *f);
const igb_object *igb_object_by_index(const igb *f, int index);
const igb_fieldval *igb_object_field(const igb_object *obj, uint16_t slot);
int igb_find_images(const igb *f, igb_image *out, int max);
uint8_t *igb_image_to_rgba(const igb_image *img, int *out_len);
const igb_meta *igb_meta_by_index(const igb *f, int index);
const char *igb_metafield_name(const igb *f, uint16_t type_idx);

#endif
