/*
 * dat_image.h - ESP32 port: decode a Prince of Persia "image-16col" DAT
 * resource into a compact 4-bit indexed bitmap, rendered to RGB565 row by row.
 *
 * This is glue code for the ESP32 port. The decompression is performed by the
 * vendored Princed Resources routines (expandRle / expandLzg, GPLv2); the
 * header parsing, transposition, nibble unpacking and palette conversion here
 * are derived from Princed Resources' image16.c. Because it links against and
 * derives from GPLv2 code, this file is distributed under the GPLv2.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A decoded 16-color image, kept as compact 4-bit palette indices.
 *
 * The image is stored as 4-bit indices (two pixels per byte) rather than
 * expanded to RGB565. This keeps memory use low on boards without PSRAM: a
 * 320x200 image needs 32 KB here instead of 128 KB as RGB565. Convert to
 * RGB565 one row at a time with ::dat_image_render_row when pushing to the LCD.
 *
 * @ref pix is heap allocated by ::dat_image_decode and must be released with
 * ::dat_image_free.
 */
typedef struct {
    int      width;          /*!< Image width in pixels */
    int      height;         /*!< Image height in pixels */
    int      width_in_bytes; /*!< Bytes per row ((width+1)/2 for 4-bit) */
    uint8_t *pix;            /*!< width_in_bytes*height 4-bit indexed pixels */
    uint16_t pal565[16];     /*!< 16-entry palette, RGB565 byte-swapped for SPI */
} dat_image_t;

/**
 * @brief Decode an image-16col resource into a 4-bit indexed bitmap.
 *
 * Parses the 6-byte image header, decompresses the pixel data (RAW / RLE / LZG,
 * transposing the up-down variants) and keeps the raw 4-bit palette indices.
 * The palette is initialized to the fixed POP1 16-color palette; call
 * ::dat_image_set_palette to override it with the image's real palette.
 *
 * @param[in]  data Raw resource content (as stored in the DAT file).
 * @param[in]  size Size of @p data in bytes.
 * @param[out] out  Caller-provided struct to populate on success.
 * @return - ESP_OK on success
 *         - ESP_ERR_INVALID_ARG if a pointer is NULL
 *         - ESP_ERR_INVALID_SIZE if the data is too small / not a 16-color image
 *         - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t dat_image_decode(const uint8_t *data, size_t size, dat_image_t *out);

/**
 * @brief Apply a POP1 4-bit palette resource to a decoded image.
 *
 * Parses a 100-byte "palette-pop1-4bits" resource (16 colors, 3 bytes each of
 * 6-bit VGA values starting at offset 4) and stores the resulting RGB565
 * entries in @ref dat_image_t::pal565, replacing the default palette.
 *
 * @param[in,out] img      Decoded image to update.
 * @param[in]     pal_data Raw palette resource content (as stored in the DAT).
 * @param[in]     pal_size Size of @p pal_data in bytes (must be 100).
 * @return - ESP_OK on success
 *         - ESP_ERR_INVALID_ARG if a pointer is NULL
 *         - ESP_ERR_INVALID_SIZE if @p pal_size is not 100
 */
esp_err_t dat_image_set_palette(dat_image_t *img, const uint8_t *pal_data, size_t pal_size);

/**
 * @brief Render one image row to RGB565, mapping indices through the POP1 palette.
 *
 * Unpacks the 4-bit indices of row @p y and writes @ref dat_image_t::width
 * RGB565 pixels (byte-swapped for the big-endian ILI9341 SPI bus) into @p dst.
 *
 * @param[in]  img Decoded image (from ::dat_image_decode).
 * @param[in]  y   Row index in [0, height).
 * @param[out] dst Destination buffer of at least @ref dat_image_t::width pixels.
 */
void dat_image_render_row(const dat_image_t *img, int y, uint16_t *dst);

/**
 * @brief Release the buffer owned by a ::dat_image_t.
 *
 * Safe to call on a zero-initialized struct. Resets the struct after freeing.
 */
void dat_image_free(dat_image_t *out);

#ifdef __cplusplus
}
#endif
