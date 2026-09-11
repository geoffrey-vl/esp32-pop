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
