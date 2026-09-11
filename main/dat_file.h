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

#ifdef __cplusplus
}
#endif
