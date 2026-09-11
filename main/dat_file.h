#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One parsed resource entry from a DAT file.
 */
typedef struct {
    int16_t     id;             /*!< Resource id (from the DAT index) */
    char        index[5];       /*!< 4-char category name + NUL ("pop1" for POP1 files) */
    uint32_t    offset;         /*!< Byte offset of the resource inside the DAT file */
    uint32_t    size;           /*!< Resource content size in bytes (checksum byte excluded) */
    int         type;           /*!< Classified type (tResourceType value from verifyHeader) */
    const char *type_name;      /*!< Human readable name for @ref type */
    bool        checksum_ok;    /*!< true if the stored checksum validated */
} dat_entry_t;

/**
 * @brief Result of parsing a DAT file.
 *
 * The @ref entries array is heap allocated by ::parse_dat_file and must be
 * released with ::free_dat_file.
 */
typedef struct {
    const char        *name;     /*!< Name of the parsed DAT file */
    int                version;  /*!< 1 = POP1, 2 = POP2 (0 if unknown) */
    int                count;    /*!< Number of entries */
    dat_entry_t       *entries;  /*!< Array of @ref count entries (may be NULL if count == 0) */
} dat_file_t;

/**
 * @brief Parse an embedded DAT file and classify every resource it holds.
 *
 * Looks up @p name in the embedded DAT registry, walks the resource index and
 * fills @p out with one ::dat_entry_t per resource. Every entry is also logged
 * (ESP_LOGI). On success the caller owns @p out->entries and must call
 * ::free_dat_file.
 *
 * @param[in]  name Embedded DAT file name, e.g. "TITLE.DAT".
 * @param[out] out  Caller-provided struct to populate.
 * @return - ESP_OK on success
 *         - ESP_ERR_INVALID_ARG if @p name or @p out is NULL
 *         - ESP_ERR_INVALID_STATE if the data is not a valid DAT file
 *         - ESP_ERR_NO_MEM if allocation failed
 */
esp_err_t parse_dat_file(const char *name, dat_file_t *out);

/**
 * @brief Release the resources owned by a ::dat_file_t filled by ::parse_dat_file.
 *
 * Safe to call on a zero-initialized struct. Resets the struct after freeing.
 *
 * @param[in,out] out Struct to release.
 */
void free_dat_file(dat_file_t *out);

/**
 * @brief Read the raw bytes of a single resource from an embedded DAT file.
 *
 * Opens @p name, finds the resource whose id equals @p id and returns a
 * heap-allocated copy of its content bytes. The caller owns @p out_data and
 * must release it with free().
 *
 * @param[in]  name     Embedded DAT file name, e.g. "TITLE.DAT".
 * @param[in]  id       Resource id to fetch.
 * @param[out] out_data Receives a malloc'd buffer with the resource bytes.
 * @param[out] out_size Receives the size of @p out_data in bytes.
 * @return - ESP_OK on success
 *         - ESP_ERR_INVALID_ARG if a pointer is NULL
 *         - ESP_ERR_INVALID_STATE if @p name is not a valid DAT file
 *         - ESP_ERR_NOT_FOUND if no resource has that id
 *         - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t read_dat_resource(const char *name, int16_t id,
                            uint8_t **out_data, size_t *out_size);

/**
 * @brief Find and read the palette that applies to an image resource.
 *
 * The image-to-palette association is not stored in the DAT file itself (in
 * Princed Resources it lives in external metadata, resources.xml), and resource
 * ids are only unique within a single DAT, so the lookup is keyed by both the
 * DAT filename and the image id. For the stock Prince of Persia 1 DAT files an
 * embedded table (default palette + id-range overrides) reproduces the original
 * mapping exactly; for unknown/modded DATs the palette is auto-detected (the
 * palette resource with the largest id `<=` the image id, else the lowest).
 *
 * @param[in]  name       Embedded DAT file name, e.g. "TITLE.DAT".
 * @param[in]  image_id   Id of the image whose palette is wanted.
 * @param[out] out_data   Receives a malloc'd buffer with the palette bytes.
 * @param[out] out_size   Receives the size of @p out_data in bytes.
 * @param[out] out_pal_id Receives the id of the palette that was chosen.
 * @return - ESP_OK on success
 *         - ESP_ERR_INVALID_ARG if a pointer is NULL
 *         - ESP_ERR_INVALID_STATE if @p name is not a valid DAT file
 *         - ESP_ERR_NOT_FOUND if the image has no palette (keep a fallback)
 *         - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t read_dat_palette_for(const char *name, int16_t image_id,
                               uint8_t **out_data, size_t *out_size,
                               int16_t *out_pal_id);

#ifdef __cplusplus
}
#endif
