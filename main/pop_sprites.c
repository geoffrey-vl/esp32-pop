/*
 * pop_sprites.c - ESP32 port: flash-resident sprite reader.
 *
 * The firmware cannot decode Prince of Persia sprites into RAM at runtime (the
 * two 64KB screen buffers consume nearly all byte-addressable heap, and a
 * single chtab's decode temp buffers alone can exceed the free heap). Instead,
 * every needed sprite is pre-decoded on the host by tools/pop_bake_sprites.c
 * into 8bpp indexed pixels and packed into assets/pop_sprites.bin, which the
 * build embeds via EMBED_FILES. This module parses that blob and hands out
 * flash pointers so image_type surfaces can be backed directly by flash.
 *
 * Blob format (little-endian, offsets from blob start):
 *   header:        'PSPR' (4) | u16 version | u16 chtab_count
 *   chtab dir ent: char dat[16] | u16 resource | u16 palette_bits | u16 variant
 *                  | u16 n_images | rgb_type vga[16] (48) | u32 image_dir_off
 *                  | u32 rsvd            (80 bytes)
 *   image dir ent: u16 w | u16 h (0=NULL) | u32 pixel_off (0=NULL)  (8 bytes)
 *   pixel data:    concatenated baked 8bpp
 */

#include "pop_sprites.h"
#include <string.h>
#include "esp_log.h"

/* Linker symbols for the embedded blob (see main/CMakeLists.txt EMBED_FILES). */
extern const uint8_t _binary_pop_sprites_bin_start[] asm("_binary_pop_sprites_bin_start");
extern const uint8_t _binary_pop_sprites_bin_end[]   asm("_binary_pop_sprites_bin_end");

#define CHTAB_DIR_OFFSET 8
#define CHTAB_DIR_STRIDE 80
#define IMAGE_DIR_STRIDE 8

static const uint8_t *g_blob = NULL;
static int g_chtab_count = 0;

/* Byte-assembled little-endian reads (blob is LE; flash is byte-addressable). */
static inline uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t rd32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

static const uint8_t *chtab_entry(int idx) {
    return g_blob + CHTAB_DIR_OFFSET + (size_t)idx * CHTAB_DIR_STRIDE;
}

void pop_sprites_init(void) {
    const uint8_t *b = _binary_pop_sprites_bin_start;
    if (memcmp(b, "PSPR", 4) != 0) {
        ESP_LOGE("pop_spr", "sprite blob bad magic (embed missing?)");
        g_blob = NULL;
        g_chtab_count = 0;
        return;
    }
    g_blob = b;
    g_chtab_count = rd16(b + 6);
    ESP_LOGI("pop_spr", "sprite blob: %d chtabs, %u bytes",
             g_chtab_count,
             (unsigned)(_binary_pop_sprites_bin_end - _binary_pop_sprites_bin_start));
}

int pop_sprites_find(const char *dat_name, int resource) {
    if (g_blob == NULL) return -1;
    for (int i = 0; i < g_chtab_count; ++i) {
        const uint8_t *e = chtab_entry(i);
        if ((int)rd16(e + 16) == resource &&
            strncmp((const char *)e, dat_name, 16) == 0) {
            return i;
        }
    }
    return -1;
}

int pop_sprites_n_images(int chtab_idx) {
    if (g_blob == NULL || chtab_idx < 0 || chtab_idx >= g_chtab_count) return 0;
    return rd16(chtab_entry(chtab_idx) + 22);
}

const uint8_t *pop_sprites_image(int chtab_idx, int img_i, int *w, int *h) {
    *w = 0;
    *h = 0;
    if (g_blob == NULL || chtab_idx < 0 || chtab_idx >= g_chtab_count) return NULL;
    const uint8_t *e = chtab_entry(chtab_idx);
    int n_images = rd16(e + 22);
    if (img_i < 0 || img_i >= n_images) return NULL;
    uint32_t image_dir_off = rd32(e + 72);
    const uint8_t *ie = g_blob + image_dir_off + (size_t)img_i * IMAGE_DIR_STRIDE;
    int iw = rd16(ie);
    int ih = rd16(ie + 2);
    uint32_t pixel_off = rd32(ie + 4);
    if (pixel_off == 0 || ih == 0) return NULL;
    *w = iw;
    *h = ih;
    return g_blob + pixel_off;
}
