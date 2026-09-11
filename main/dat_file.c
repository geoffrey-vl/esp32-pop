/*
 * dat_file.c - ESP32-facing API to parse the embedded TITLE.DAT resource.
 *
 * This is glue code for the ESP32 port. It drives the vendored Princed
 * Resources DAT reader (components/prince_dat, GPLv2) over the TITLE.DAT
 * blob embedded into the firmware and exposes a small, self-contained
 * result structure (see dat_file.h). Because it links against GPLv2 code,
 * this file and the project as a whole are distributed under the GPLv2.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "dat_file.h"

/* Vendored Princed Resources headers (components/prince_dat/include). */
#include "dat.h"        /* mReadBeginDatFile, mReadFileInDatFile, tPopVersion, PR_RESULT_* */
#include "autodetect.h" /* verifyHeader, tResourceType */

static const char *res_type_name(int t)
{
    switch (t) {
    case eResTypeNone:              return "none";
    case eResTypeRaw:               return "raw";
    case eResTypeBinary:            return "binary";
    case eResTypeImage16:           return "image-16col";
    case eResTypeImage2:            return "image-mono";
    case eResTypeImage256:          return "image-256col";
    case eResTypeLevel:             return "level";
    case eResTypeMidi:              return "midi";
    case eResTypePcspeaker:         return "pcspeaker";
    case eResTypePop1Palette4bits:  return "pop1-palette-4bit";
    case eResTypePop1PaletteGuards: return "pop1-palette-guards";
    case eResTypePop1PaletteMono:   return "pop1-palette-mono";
    case eResTypePop2PaletteNColors:return "pop2-palette-ncol";
    case eResTypePop2Palette4bits:  return "pop2-palette-4bit";
    case eResTypeText:              return "text";
    case eResTypeWave:              return "wave";
    case eResTypeTxt4:              return "txt4";
    default:                        return "unknown";
    }
}

esp_err_t parse_dat_file(const char *name, dat_file_t *out)
{
    if (name == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out->name = name;
    out->version = 0;
    out->count = 0;
    out->entries = NULL;

    unsigned short int numberOfItems = 0;
    int err = mReadBeginDatFile(&numberOfItems, name);
    if (err != PR_RESULT_SUCCESS) {
        ESP_LOGW(name, "not a valid DAT file (err=%d)", err);
        return ESP_ERR_INVALID_STATE;
    }

    tPopVersion version = mReadGetVersion();
    out->version = (int)version;
    out->count = (int)numberOfItems;

    ESP_LOGI(name, "parsed: %s, %u resource(s)",
             (version == pop1) ? "POP1" : (version == pop2) ? "POP2" : "unknown",
             (unsigned)numberOfItems);

    if (numberOfItems > 0) {
        out->entries = calloc((size_t)numberOfItems, sizeof(dat_entry_t));
        if (out->entries == NULL) {
            ESP_LOGE(name, "out of memory allocating %u entries", (unsigned)numberOfItems);
            mReadCloseDatFile();
            out->count = 0;
            return ESP_ERR_NO_MEM;
        }
    }

    for (int k = 0; k < (int)numberOfItems; k++) {
        tResource res;
        memset(&res, 0, sizeof(res));
        res.content.data = NULL;
        res.content.size = 0;

        int ret = mReadFileInDatFile(&res, k);

        dat_entry_t *e = &out->entries[k];
        e->id = (int16_t)res.id.value;
        memcpy(e->index, res.id.index, 4);
        e->index[4] = '\0';
        e->offset = (uint32_t)res.offset;
        e->size = (uint32_t)res.content.size;

        /* dat_readRes() only allocates content.data when the resource lies
           inside the file; guard against the out-of-range case where the
           pointer is left unset. */
        if (res.content.data != NULL) {
            tResourceType t = verifyHeader(res.content);
            e->type = (int)t;
            e->type_name = res_type_name((int)t);
            e->checksum_ok = (ret == PR_RESULT_SUCCESS);
            free(res.content.data);
        } else {
            e->type = (int)eResTypeNone;
            e->type_name = res_type_name((int)eResTypeNone);
            e->checksum_ok = false;
        }

        ESP_LOGI(name,
                 "  [%3d] id=%5d index=%-4s offset=%7u size=%6u type=%-20s checksum=%s",
                 k, e->id, e->index, (unsigned)e->offset, (unsigned)e->size,
                 e->type_name, e->checksum_ok ? "ok" : "FAIL");

        /* Parsing every resource of every embedded DAT is a long, log-heavy
           loop; yield periodically so the idle task can feed the task
           watchdog. */
        if ((k & 0x0F) == 0) {
            vTaskDelay(1);
        }
    }

    mReadCloseDatFile();
    return ESP_OK;
}

void free_dat_file(dat_file_t *out)
{
    if (out == NULL) {
        return;
    }
    free(out->entries);
    out->entries = NULL;
    out->count = 0;
    out->version = 0;
    out->name = NULL;
}

esp_err_t read_dat_resource(const char *name, int16_t id,
                            uint8_t **out_data, size_t *out_size)
{
    if (name == NULL || out_data == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_size = 0;

    unsigned short int numberOfItems = 0;
    if (mReadBeginDatFile(&numberOfItems, name) != PR_RESULT_SUCCESS) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_ERR_NOT_FOUND;

    for (int k = 0; k < (int)numberOfItems; k++) {
        tResource res;
        memset(&res, 0, sizeof(res));
        res.content.data = NULL;
        res.content.size = 0;

        mReadFileInDatFile(&res, k);

        if (res.content.data == NULL) {
            continue;
        }

        if ((int16_t)res.id.value == id) {
            uint8_t *copy = malloc((size_t)res.content.size);
            if (copy != NULL) {
                memcpy(copy, res.content.data, (size_t)res.content.size);
                *out_data = copy;
                *out_size = (size_t)res.content.size;
                result = ESP_OK;
            } else {
                result = ESP_ERR_NO_MEM;
            }
            free(res.content.data);
            break;
        }

        free(res.content.data);
    }

    mReadCloseDatFile();
    return result;
}

/* Detect a POP1 4-bit palette from the resource content (checksum already
   stripped by the reader): 100 bytes, and the "16 colors" marker at content[3].
   Mirrors Princed Resources' verifyPaletteHeaderPop1 (which sees the extra
   checksum byte, so its offsets are one higher). */
static bool is_pop1_palette(const tResource *res)
{
    const tBinary *c = &res->content;
    return c->data != NULL && c->size == 100 &&
           c->data[1] == 0 && c->data[2] == 0 && c->data[3] == 0x10;
}

/*
 * Image -> palette association for the stock Prince of Persia 1 (PC) DAT files.
 *
 * This mapping is NOT stored in the DAT files themselves; in Princed Resources
 * it lives in external metadata (resources.xml, via folder inheritance), so it
 * cannot be recovered from the DAT alone. The table below is derived from that
 * metadata: each DAT has a default palette plus a few id-range overrides. A
 * palette id of 0 means "no 4-bit VGA palette" (monochrome / CGA / EGA / sound
 * / level resources), for which the caller should keep its fallback palette.
 *
 * Resource ids are unique only within a single DAT (they overlap across DATs),
 * so the lookup is keyed by both DAT filename and image id.
 */
typedef struct {
    int16_t lo, hi; /* inclusive image-id range */
    int16_t pal;    /* palette id for this range (0 = none) */
} pal_range_t;

typedef struct {
    const char        *dat;         /* DAT filename (matched case-insensitively) */
    int16_t            default_pal; /* palette for ids not covered by an override */
    const pal_range_t *overrides;
    int                n_overrides;
} dat_pal_map_t;

static const pal_range_t title_ov[]  = {{41, 41, 40}, {42, 45, 0}};
static const pal_range_t prince_ov[] = {{1, 2, 150}, {151, 165, 150}, {166, 173, 0}};
static const pal_range_t pv_ov[]     = {{801, 817, 800}, {901, 930, 900}, {951, 962, 950}, {981, 981, 980}};
static const pal_range_t vdun_ov[]   = {{268, 268, 0}, {361, 377, 360}, {1314, 1323, 0}};
static const pal_range_t vpal_ov[]   = {{268, 268, 0}, {361, 365, 360}, {366, 374, 0}, {375, 377, 360}, {1314, 1323, 0}};

static const dat_pal_map_t pal_maps[] = {
    {"TITLE.DAT",    50, title_ov,  2},
    {"PRINCE.DAT",  700, prince_ov, 3},
    {"PV.DAT",      850, pv_ov,     4},
    {"KID.DAT",     400, NULL,      0},
    {"FAT.DAT",     750, NULL,      0},
    {"SHADOW.DAT",  750, NULL,      0},
    {"SKEL.DAT",    750, NULL,      0},
    {"VIZIER.DAT",  750, NULL,      0},
    {"VDUNGEON.DAT", 200, vdun_ov,  3},
    {"VPALACE.DAT",  200, vpal_ov,  5},
};

/* Resolve the palette id for (dat, image_id) from the embedded POP1 map.
   Returns the palette id (>0), 0 if the image has no VGA palette, or -1 if the
   DAT is not in the table (caller should fall back to auto-detection). */
static int lookup_pop1_palette_id(const char *name, int16_t image_id)
{
    for (size_t i = 0; i < sizeof(pal_maps) / sizeof(pal_maps[0]); i++) {
        if (strcasecmp(name, pal_maps[i].dat) != 0) {
            continue;
        }
        for (int j = 0; j < pal_maps[i].n_overrides; j++) {
            const pal_range_t *r = &pal_maps[i].overrides[j];
            if (image_id >= r->lo && image_id <= r->hi) {
                return r->pal;
            }
        }
        return pal_maps[i].default_pal;
    }
    return -1; /* unknown DAT */
}

/* Auto-detect palettes in an unknown DAT and pick the largest palette id that
   is <= image_id, falling back to the lowest palette id. Returns the id, or -1
   if the DAT contains no POP1 4-bit palette. */
static int autodetect_palette_id(const char *name, int16_t image_id)
{
    unsigned short int numberOfItems = 0;
    if (mReadBeginDatFile(&numberOfItems, name) != PR_RESULT_SUCCESS) {
        return -1;
    }

    int best_below = -1; /* largest palette id <= image_id */
    int lowest_any = -1; /* smallest palette id overall (fallback) */

    for (int k = 0; k < (int)numberOfItems; k++) {
        tResource res;
        memset(&res, 0, sizeof(res));
        res.content.data = NULL;
        res.content.size = 0;

        mReadFileInDatFile(&res, k);
        if (res.content.data == NULL) {
            continue;
        }
        if (is_pop1_palette(&res)) {
            int pid = (int)(int16_t)res.id.value;
            if (lowest_any < 0 || pid < lowest_any) {
                lowest_any = pid;
            }
            if (pid <= image_id && pid > best_below) {
                best_below = pid;
            }
        }
        free(res.content.data);
    }

    mReadCloseDatFile();
    return (best_below >= 0) ? best_below : lowest_any;
}

esp_err_t read_dat_palette_for(const char *name, int16_t image_id,
                               uint8_t **out_data, size_t *out_size,
                               int16_t *out_pal_id)
{
    if (name == NULL || out_data == NULL || out_size == NULL || out_pal_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_size = 0;
    *out_pal_id = -1;

    /* Prefer the authoritative POP1 mapping; fall back to auto-detection for
       DATs that are not in the table (e.g. mods or custom files). */
    int chosen = lookup_pop1_palette_id(name, image_id);
    if (chosen == 0) {
        /* Known image with no VGA palette: caller keeps its fallback palette. */
        return ESP_ERR_NOT_FOUND;
    }
    if (chosen < 0) {
        chosen = autodetect_palette_id(name, image_id);
        if (chosen < 0) {
            return ESP_ERR_NOT_FOUND; /* no palette available at all */
        }
    }

    *out_pal_id = (int16_t)chosen;
    return read_dat_resource(name, (int16_t)chosen, out_data, out_size);
}

