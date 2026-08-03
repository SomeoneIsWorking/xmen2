#include <stdio.h>
#include <stdlib.h>

#include "igb.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: igb_dump <file.igb> [img_index]\n");
        return 1;
    }
    igb f;
    if (igb_open(&f, argv[1]) != 0) {
        fprintf(stderr, "igb_open failed: %s\n", argv[1]);
        return 1;
    }
    printf("version=%d flags(info=%d ext=%d shared=%d mpool=%d) objects=%d info_index=%d\n",
           f.version, f.has_info, f.has_external, f.shared_entries,
           f.has_memory_pool_names, f.n_objects, f.info_list_index);

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
