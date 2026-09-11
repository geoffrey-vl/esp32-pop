/*
 * dat_image.c - ESP32 port: decode a Prince of Persia "image-16col" DAT
 * resource into an RGB565 bitmap.
 *
 * The decompression is done by the vendored Princed Resources routines
 * (expandRle / expandLzg). The header parsing, transposition, 4-bit nibble
 * unpacking and palette conversion below are derived from Princed Resources'
 * src/lib/object/image/image16.c (mExpandGraphic). Because it links against
 * and derives from GPLv2 code, this file is distributed under the GPLv2.
 */

#include <stdlib.h>
#include <string.h>

#include "dat_image.h"

/* Vendored Princed Resources decompressors and constants. */
#include "compress.h" /* expandRle, expandLzg, COMPRESS_* */

/*
 * The panel is driven RGB565 with MADCTL BGR=1 and the pixels are sent
 * big-endian over SPI (matching the previous JPEG path, which byte-swapped
 * each pixel). If red and blue appear swapped on your display, set this to 1.
 */
#ifndef DAT_IMAGE_SWAP_RB
#define DAT_IMAGE_SWAP_RB 0
#endif

/* Fixed POP1 16-color palette (SAMPLE_PAL16 from Princed Resources palette.h). */
static const struct {
    uint8_t r, g, b;
} pop1_pal16[16] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xa0}, {0x00, 0xa7, 0x00}, {0x00, 0xa7, 0xa0},
    {0xa0, 0x00, 0x00}, {0xa0, 0x00, 0xa0}, {0xa0, 0x50, 0x00}, {0xa0, 0xa7, 0xa0},
    {0x50, 0x50, 0x50}, {0x50, 0x50, 0xff}, {0x50, 0xf8, 0x50}, {0x50, 0xf8, 0xff},
    {0xff, 0x50, 0x50}, {0xff, 0x50, 0xff}, {0xff, 0xf8, 0x50}, {0xff, 0xff, 0xff},
};

/* Read a little-endian 16-bit value. */
static inline int rd16(const uint8_t *p)
{
    return (int)p[0] | ((int)p[1] << 8);
}

/* Transpose the up-down (column-major) layout into row-major, as image16.c does:
   out[(i % h) * w + i / h] = in[i], for i in [0, w*h). Frees @p in on success. */
static uint8_t *transpose(uint8_t *in, int w, int h)
{
    int size = w * h;
    uint8_t *out = malloc((size_t)size);
    if (out == NULL) {
        return NULL;
    }
    for (int i = 0; i < size; i++) {
        out[(i % h) * w + i / h] = in[i];
    }
    free(in);
    return out;
}

/* Convert an RGB888 color to a byte-swapped RGB565 pixel for the display. */
static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = r >> 3;
    uint16_t g6 = g >> 2;
    uint16_t b5 = b >> 3;

#if DAT_IMAGE_SWAP_RB
    uint16_t v = (uint16_t)((b5 << 11) | (g6 << 5) | r5);
#else
    uint16_t v = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
#endif

    /* The ILI9341 SPI stream is big-endian; store MSB first. */
    return (uint16_t)((v >> 8) | (v << 8));
}

esp_err_t dat_image_decode(const uint8_t *data, size_t size, dat_image_t *out)
{
    if (data == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out->width = 0;
    out->height = 0;
    out->width_in_bytes = 0;
    out->pix = NULL;

    if (size < 6) {
        return ESP_ERR_INVALID_SIZE;
    }

    int height   = rd16(&data[0]);
    int width    = rd16(&data[2]);
    int reserved = data[4];
    int type     = data[5];

    /* Faithful to mExpandGraphic: reserved byte must be 0 or 1. */
    if (reserved > 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    int bpp  = ((type >> 4) & 7) + 1;
    int algo = type & 0x0F;
    if (bpp != 4) {
        return ESP_ERR_INVALID_SIZE; /* only 16-color images supported */
    }

    int width_in_bytes = (width + 1) / 2; /* 2 pixels per byte */
    int expected = width_in_bytes * height;
    if (width <= 0 || height <= 0 || expected <= 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *pdata = data + 6;
    int psize = (int)size - 6;

    uint8_t *pix = NULL;
    int out_size = 0;

    switch (algo) {
    case COMPRESS_RAW:
        if (psize < expected) {
            return ESP_ERR_INVALID_SIZE;
        }
        pix = malloc((size_t)expected);
        if (pix == NULL) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(pix, pdata, (size_t)expected);
        out_size = expected;
        break;

    case COMPRESS_RLE_LR:
        out_size = 0;
        expandRle(pdata, psize, &pix, &out_size);
        break;

    case COMPRESS_RLE_UD:
        out_size = 0;
        expandRle(pdata, psize, &pix, &out_size);
        break;

    case COMPRESS_LZG_LR:
        out_size = 0;
        expandLzg(pdata, psize, &pix, &out_size);
        break;

    case COMPRESS_LZG_UD:
        out_size = 0;
        expandLzg(pdata, psize, &pix, &out_size);
        break;

    default:
        return ESP_ERR_INVALID_SIZE; /* unknown algorithm */
    }

    if (pix == NULL || out_size < expected) {
        free(pix);
        return ESP_ERR_INVALID_SIZE;
    }

    if (algo == COMPRESS_RLE_UD || algo == COMPRESS_LZG_UD) {
        uint8_t *t = transpose(pix, width_in_bytes, height);
        if (t == NULL) {
            free(pix);
            return ESP_ERR_NO_MEM;
        }
        pix = t;
    }

    /* Keep the compact 4-bit indexed buffer; it is converted to RGB565 one row
       at a time by dat_image_render_row(). This avoids allocating a large
       (e.g. 128 KB) RGB565 framebuffer, which will not fit in internal RAM on
       boards without PSRAM. */
    out->width = width;
    out->height = height;
    out->width_in_bytes = width_in_bytes;
    out->pix = pix;

    /* Default to the fixed POP1 palette; the caller may override it with the
       image's real palette via dat_image_set_palette(). */
    for (int i = 0; i < 16; i++) {
        out->pal565[i] = rgb888_to_rgb565(pop1_pal16[i].r, pop1_pal16[i].g,
                                          pop1_pal16[i].b);
    }
    return ESP_OK;
}

esp_err_t dat_image_set_palette(dat_image_t *img, const uint8_t *pal_data, size_t pal_size)
{
    if (img == NULL || pal_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* POP1 4-bit palette resource: 100 bytes, 16 colors of 3 bytes (6-bit VGA
       values) starting at offset 4. See Princed Resources pop1_4bit.c. */
    if (pal_size != 100) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (int i = 0; i < 16; i++) {
        uint8_t r = (uint8_t)(pal_data[i * 3 + 4] << 2);
        uint8_t g = (uint8_t)(pal_data[i * 3 + 5] << 2);
        uint8_t b = (uint8_t)(pal_data[i * 3 + 6] << 2);
        img->pal565[i] = rgb888_to_rgb565(r, g, b);
    }
    return ESP_OK;
}

void dat_image_render_row(const dat_image_t *img, int y, uint16_t *dst)
{
    if (img == NULL || dst == NULL || y < 0 || y >= img->height) {
        return;
    }

    const uint8_t *row = &img->pix[(size_t)y * img->width_in_bytes];
    for (int x = 0; x < img->width; x++) {
        uint8_t byte = row[x >> 1];
        uint8_t idx = (x & 1) ? ((byte >> 4) & 0x0F) : (byte & 0x0F);
        dst[x] = img->pal565[idx];
    }
}

void dat_image_free(dat_image_t *out)
{
    if (out == NULL) {
        return;
    }
    free(out->pix);
    out->pix = NULL;
    out->width = 0;
    out->height = 0;
    out->width_in_bytes = 0;
}
