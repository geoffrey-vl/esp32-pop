/*
 * pop_bake_sprites.c - HOST build-time tool (plain C, no SDL, no ESP-IDF).
 *
 * Pre-decodes every Prince of Persia sprite chtab we need on the ESP32 into
 * 8bpp indexed pixels and writes a single flash blob (pop_sprites.bin) that the
 * firmware embeds and renders straight from flash. This exists because the
 * ESP32 has no RAM to decode sprites at runtime (the two 64KB screen buffers
 * consume nearly all byte-addressable heap).
 *
 * The decode path (decompress_rle_lr/ud, decompress_lzg_lr/ud, decompr_img,
 * calc_stride, conv_to_8bpp) is copied VERBATIM from the SDLPoP engine
 * (sdlpop/seg009.c) so the baked pixels are byte-identical to what the desktop
 * engine's decode_image() would produce. SDLPoP is GPLv3; this tool derives
 * from it and is therefore GPLv3. It is a build-time host tool only and is not
 * linked into the (proprietary-portable) firmware.
 *
 * Palette-row baking: in DOS PoP each chtab occupies one 16-color row of the
 * 256-color palette (row = bit index of palette_bits). We bake that row offset
 * into the stored pixel value so the device blit is a plain copy-skip-zero and
 * the present stage is a single palette[256] lookup:
 *     stored = (index == 0) ? 0 (transparent) : (row*16 + index)
 *
 * Usage: pop_bake_sprites <data_dir> <out_file>
 *   e.g. pop_bake_sprites main/data main/data_gen/pop_sprites.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef uint8_t  byte;
typedef int8_t   sbyte;
typedef uint16_t word;

/* Host is assumed little-endian (x86-64 / arm64). The engine reads DAT fields
 * through SDL_SwapLE16 which is the identity on little-endian hosts. */
#define SDL_SwapLE16(x) ((word)(x))

#pragma pack(push, 1)
typedef struct image_data_type {
    word height;
    word width;
    word flags;
    byte data[];
} image_data_type;
typedef struct rgb_type { byte r, g, b; } rgb_type;
typedef struct dat_pal_type {
    word     row_bits;
    byte     n_colors;
    rgb_type vga[16];
    byte     cga[16];
    byte     ega[32];
} dat_pal_type;
typedef struct dat_shpl_type {
    byte        n_images;
    dat_pal_type palette;
} dat_shpl_type;
#pragma pack(pop)

/* ------------------------------------------------------------------ */
/*  Engine decode functions - copied verbatim from sdlpop/seg009.c     */
/* ------------------------------------------------------------------ */

/* seg009:8CE6 */
static void decompress_rle_lr(byte* destination, const byte* source, int dest_length) {
    const byte* src_pos = source;
    byte* dest_pos = destination;
    short rem_length = dest_length;
    while (rem_length) {
        sbyte count = *src_pos;
        src_pos++;
        if (count >= 0) { /* copy */
            ++count;
            do {
                *dest_pos = *src_pos;
                dest_pos++;
                src_pos++;
                --rem_length;
                --count;
            } while (count && rem_length);
        } else { /* repeat */
            byte al = *src_pos;
            src_pos++;
            count = -count;
            do {
                *dest_pos = al;
                dest_pos++;
                --rem_length;
                --count;
            } while (count && rem_length);
        }
    }
}

/* seg009:8D1C */
static void decompress_rle_ud(byte* destination, const byte* source, int dest_length, int width, int height) {
    short rem_height = height;
    const byte* src_pos = source;
    byte* dest_pos = destination;
    short rem_length = dest_length;
    --dest_length;
    --width;
    while (rem_length) {
        sbyte count = *src_pos;
        src_pos++;
        if (count >= 0) { /* copy */
            ++count;
            do {
                *dest_pos = *src_pos;
                dest_pos++;
                src_pos++;
                dest_pos += width;
                --rem_height;
                if (rem_height == 0) {
                    dest_pos -= dest_length;
                    rem_height = height;
                }
                --rem_length;
                --count;
            } while (count && rem_length);
        } else { /* repeat */
            byte al = *src_pos;
            src_pos++;
            count = -count;
            do {
                *dest_pos = al;
                dest_pos++;
                dest_pos += width;
                --rem_height;
                if (rem_height == 0) {
                    dest_pos -= dest_length;
                    rem_height = height;
                }
                --rem_length;
                --count;
            } while (count && rem_length);
        }
    }
}

/* seg009:90FA */
static byte* decompress_lzg_lr(byte* dest, const byte* source, int dest_length) {
    byte* window = (byte*) malloc(0x400);
    if (window == NULL) return NULL;
    memset(window, 0, 0x400);
    byte* window_pos = window + 0x400 - 0x42; /* bx */
    short remaining = dest_length; /* cx */
    byte* window_end = window + 0x400; /* dx */
    const byte* source_pos = source;
    byte* dest_pos = dest;
    word mask = 0;
    do {
        mask >>= 1;
        if ((mask & 0xFF00) == 0) {
            mask = *source_pos | 0xFF00;
            source_pos++;
        }
        if (mask & 1) {
            *window_pos = *dest_pos = *source_pos;
            window_pos++;
            dest_pos++;
            source_pos++;
            if (window_pos >= window_end) window_pos = window;
            --remaining;
        } else {
            word copy_info = *source_pos;
            source_pos++;
            copy_info = (copy_info << 8) | *source_pos;
            source_pos++;
            byte* copy_source = window + (copy_info & 0x3FF);
            byte copy_length = (copy_info >> 10) + 3;
            do {
                *window_pos = *dest_pos = *copy_source;
                window_pos++;
                dest_pos++;
                copy_source++;
                if (copy_source >= window_end) copy_source = window;
                if (window_pos >= window_end) window_pos = window;
                --remaining;
                --copy_length;
            } while (remaining && copy_length);
        }
    } while (remaining);
    free(window);
    return dest;
}

/* seg009:91AD */
static byte* decompress_lzg_ud(byte* dest, const byte* source, int dest_length, int stride, int height) {
    byte* window = (byte*) malloc(0x400);
    if (window == NULL) return NULL;
    memset(window, 0, 0x400);
    byte* window_pos = window + 0x400 - 0x42; /* bx */
    short remaining = height; /* cx */
    byte* window_end = window + 0x400; /* dx */
    const byte* source_pos = source;
    byte* dest_pos = dest;
    word mask = 0;
    short dest_end = dest_length - 1;
    do {
        mask >>= 1;
        if ((mask & 0xFF00) == 0) {
            mask = *source_pos | 0xFF00;
            source_pos++;
        }
        if (mask & 1) {
            *window_pos = *dest_pos = *source_pos;
            window_pos++;
            source_pos++;
            dest_pos += stride;
            --remaining;
            if (remaining == 0) {
                dest_pos -= dest_end;
                remaining = height;
            }
            if (window_pos >= window_end) window_pos = window;
            --dest_length;
        } else {
            word copy_info = *source_pos;
            source_pos++;
            copy_info = (copy_info << 8) | *source_pos;
            source_pos++;
            byte* copy_source = window + (copy_info & 0x3FF);
            byte copy_length = (copy_info >> 10) + 3;
            do {
                *window_pos = *dest_pos = *copy_source;
                window_pos++;
                copy_source++;
                dest_pos += stride;
                --remaining;
                if (remaining == 0) {
                    dest_pos -= dest_end;
                    remaining = height;
                }
                if (copy_source >= window_end) copy_source = window;
                if (window_pos >= window_end) window_pos = window;
                --dest_length;
                --copy_length;
            } while (dest_length && copy_length);
        }
    } while (dest_length);
    free(window);
    return dest;
}

/* seg009:938E */
static void decompr_img(byte* dest, const image_data_type* source, int decomp_size, int cmeth, int stride) {
    switch (cmeth) {
        case 0: /* RAW left-to-right */
            memcpy(dest, &source->data, decomp_size);
            break;
        case 1: /* RLE left-to-right */
            decompress_rle_lr(dest, source->data, decomp_size);
            break;
        case 2: /* RLE up-to-down */
            decompress_rle_ud(dest, source->data, decomp_size, stride, SDL_SwapLE16(source->height));
            break;
        case 3: /* LZG left-to-right */
            decompress_lzg_lr(dest, source->data, decomp_size);
            break;
        case 4: /* LZG up-to-down */
            decompress_lzg_ud(dest, source->data, decomp_size, stride, SDL_SwapLE16(source->height));
            break;
    }
}

static int calc_stride(image_data_type* image_data) {
    int width = SDL_SwapLE16(image_data->width);
    int flags = SDL_SwapLE16(image_data->flags);
    int depth = ((flags >> 12) & 7) + 1;
    return (depth * width + 7) / 8;
}

static byte* conv_to_8bpp(byte* in_data, int width, int height, int stride, int depth) {
    byte* out_data = (byte*) malloc(width * height);
    int pixels_per_byte = 8 / depth;
    int mask = (1 << depth) - 1;
    for (int y = 0; y < height; ++y) {
        byte* in_pos = in_data + y * stride;
        byte* out_pos = out_data + y * width;
        for (int x_pixel = 0, x_byte = 0; x_byte < stride; ++x_byte) {
            byte v = *in_pos;
            int shift = 8;
            for (int pixel_in_byte = 0; pixel_in_byte < pixels_per_byte && x_pixel < width; ++pixel_in_byte, ++x_pixel) {
                shift -= depth;
                *out_pos = (v >> shift) & mask;
                ++out_pos;
            }
            ++in_pos;
        }
    }
    return out_data;
}

/* ------------------------------------------------------------------ */
/*  Minimal DAT reader                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    byte* buf;
    long  size;
} dat_file_t;

static word rd16(const byte* p) { return (word)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const byte* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int dat_open(const char* path, dat_file_t* out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    byte* buf = (byte*) malloc(sz);
    if (!buf || fread(buf, 1, sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    out->buf = buf;
    out->size = sz;
    return 0;
}

static void dat_close(dat_file_t* d) { free(d->buf); d->buf = NULL; }

/* Return pointer to resource data (past the 1-byte checksum) and its size.
 * Returns NULL if the resource id is not present. */
static const byte* dat_find(const dat_file_t* d, int id, int* out_size) {
    if (d->size < 6) return NULL;
    uint32_t table_offset = rd32(d->buf + 0);
    /* word table_size = rd16(d->buf + 4); (unused) */
    if (table_offset + 2 > (uint32_t)d->size) return NULL;
    const byte* table = d->buf + table_offset;
    word res_count = rd16(table);
    const byte* entries = table + 2;
    for (int i = 0; i < res_count; ++i) {
        const byte* e = entries + i * 8;
        word rid = rd16(e);
        if (rid == id) {
            uint32_t off = rd32(e + 2);
            word size = rd16(e + 6);
            *out_size = size;
            /* resource block: 1 checksum byte then `size` data bytes */
            if (off + 1 + size > (uint32_t)d->size) return NULL;
            return d->buf + off + 1;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Baking                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int   w, h;      /* h==0 => empty/NULL image */
    byte* pixels;    /* w*h baked bytes, or NULL */
} bimg_t;

typedef struct {
    char     dat[16];        /* images DAT (registry key with resource+variant) */
    word     resource;
    word     palette_bits;
    word     variant;        /* disambiguates same (dat,resource); e.g. guard palette */
    word     n_images;
    rgb_type vga[16];
    bimg_t*  imgs;   /* n_images entries */
} bchtab_t;

typedef struct {
    const char* images_dat;  /* DAT holding the sprite images (registry key) */
    const char* shpl_dat;    /* DAT holding the shpl palette (often == images_dat) */
    int         resource;
    int         palette_bits;
    int         variant;
} chtab_cfg_t;

/* POP1 VGA chtabs. The registry is keyed by (images DAT, resource, variant);
 * the device selects the right one per the open dat_chain / level type
 * (mirrors load_chtab_from_file / load_lev_spr).
 *
 * The normal guard (guardtype 0) is special: its images live in GUARD.DAT but
 * its shpl palette (res 750) comes from GUARD2.DAT (dungeon levels) or
 * GUARD1.DAT (palace levels) -> two variants. The other guards, and every
 * other chtab, carry their own shpl in the images DAT. */
static const chtab_cfg_t g_cfgs[] = {
    { "PRINCE.DAT",   "PRINCE.DAT",   700, 1 << 2, 0 }, /* sword */
    { "PRINCE.DAT",   "PRINCE.DAT",   150, 1 << 3, 0 }, /* flame / sword / potion */
    { "KID.DAT",      "KID.DAT",      400, 1 << 7, 0 }, /* kid */
    { "GUARD.DAT",    "GUARD2.DAT",   750, 1 << 8, 0 }, /* normal guard, dungeon palette */
    { "GUARD.DAT",    "GUARD1.DAT",   750, 1 << 8, 1 }, /* normal guard, palace palette */
    { "FAT.DAT",      "FAT.DAT",      750, 1 << 8, 0 }, /* fat guard */
    { "SKEL.DAT",     "SKEL.DAT",     750, 1 << 8, 0 }, /* skeleton */
    { "VIZIER.DAT",   "VIZIER.DAT",   750, 1 << 8, 0 }, /* vizier */
    { "SHADOW.DAT",   "SHADOW.DAT",   750, 1 << 8, 0 }, /* shadow */
    { "VDUNGEON.DAT", "VDUNGEON.DAT", 200, 1 << 5, 0 }, /* dungeon environment (VGA) */
    { "VDUNGEON.DAT", "VDUNGEON.DAT", 360, 1 << 6, 0 }, /* dungeon walls (VGA) */
    { "VPALACE.DAT",  "VPALACE.DAT",  200, 1 << 5, 0 }, /* palace environment (VGA) */
    { "VPALACE.DAT",  "VPALACE.DAT",  360, 1 << 6, 0 }, /* palace walls (VGA) */
};
#define N_CFGS ((int)(sizeof(g_cfgs) / sizeof(g_cfgs[0])))

static int lowest_bit_index(int mask) {
    for (int i = 0; i < 16; ++i) if (mask & (1 << i)) return i;
    return 0;
}

/* Decode + bake one image resource. Returns 0 and fills img (img->pixels NULL,
 * img->h 0 for empty/absent). */
static void bake_image(const dat_file_t* d, int resource_id, int base_index, bimg_t* img) {
    img->w = 0;
    img->h = 0;
    img->pixels = NULL;
    int size = 0;
    const byte* data = dat_find(d, resource_id, &size);
    if (data == NULL || size <= 2) return; /* absent / empty -> NULL image */
    image_data_type* image_data = (image_data_type*) data;
    int height = SDL_SwapLE16(image_data->height);
    if (height == 0) return; /* empty -> NULL image */
    int width = SDL_SwapLE16(image_data->width);
    int flags = SDL_SwapLE16(image_data->flags);
    int depth = ((flags >> 12) & 7) + 1;
    int cmeth = (flags >> 8) & 0x0F;
    int stride = calc_stride(image_data);
    int dest_size = stride * height;
    byte* dest = (byte*) malloc(dest_size);
    memset(dest, 0, dest_size);
    decompr_img(dest, image_data, dest_size, cmeth, stride);
    byte* img8 = conv_to_8bpp(dest, width, height, stride, depth);
    free(dest);
    /* bake palette-row offset; index 0 stays transparent (0) */
    int n = width * height;
    for (int i = 0; i < n; ++i) {
        byte idx = img8[i];
        img8[i] = (idx == 0) ? 0 : (byte)(base_index + idx);
    }
    img->w = width;
    img->h = height;
    img->pixels = img8;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <data_dir> <out_file>\n", argv[0]);
        return 2;
    }
    const char* data_dir = argv[1];
    const char* out_path = argv[2];

    bchtab_t* chtabs = (bchtab_t*) calloc(N_CFGS, sizeof(bchtab_t));
    int chtab_count = 0;

    for (int c = 0; c < N_CFGS; ++c) {
        const chtab_cfg_t* cfg = &g_cfgs[c];
        char path[512];
        dat_file_t dimg;
        snprintf(path, sizeof(path), "%s/%s", data_dir, cfg->images_dat);
        if (dat_open(path, &dimg) != 0) {
            fprintf(stderr, "WARN: cannot open %s (skipping chtab res %d)\n", path, cfg->resource);
            continue;
        }
        /* shpl may live in a different DAT (normal guard palette) */
        dat_file_t dshpl;
        int shpl_separate = strcmp(cfg->images_dat, cfg->shpl_dat) != 0;
        const dat_file_t* dsp = &dimg;
        if (shpl_separate) {
            snprintf(path, sizeof(path), "%s/%s", data_dir, cfg->shpl_dat);
            if (dat_open(path, &dshpl) != 0) {
                fprintf(stderr, "WARN: cannot open shpl DAT %s (skipping)\n", path);
                dat_close(&dimg);
                continue;
            }
            dsp = &dshpl;
        }

        int shpl_size = 0;
        const byte* shpl_data = dat_find(dsp, cfg->resource, &shpl_size);
        if (shpl_data == NULL) {
            fprintf(stderr, "WARN: %s has no shpl resource %d (skipping)\n", cfg->shpl_dat, cfg->resource);
            if (shpl_separate) dat_close(&dshpl);
            dat_close(&dimg);
            continue;
        }
        const dat_shpl_type* shpl = (const dat_shpl_type*) shpl_data;
        int n_images = shpl->n_images;
        int base_index = lowest_bit_index(cfg->palette_bits) * 16;

        bchtab_t* ct = &chtabs[chtab_count++];
        memset(ct, 0, sizeof(*ct));
        snprintf(ct->dat, sizeof(ct->dat), "%s", cfg->images_dat);
        ct->resource = (word) cfg->resource;
        ct->palette_bits = (word) cfg->palette_bits;
        ct->variant = (word) cfg->variant;
        ct->n_images = (word) n_images;
        memcpy(ct->vga, shpl->palette.vga, sizeof(ct->vga));
        ct->imgs = (bimg_t*) calloc(n_images > 0 ? n_images : 1, sizeof(bimg_t));

        int non_null = 0;
        long chtab_bytes = 0;
        for (int i = 0; i < n_images; ++i) {
            /* engine loads images at resource+1 .. resource+n_images */
            bake_image(&dimg, cfg->resource + 1 + i, base_index, &ct->imgs[i]);
            if (ct->imgs[i].pixels) {
                ++non_null;
                chtab_bytes += (long)ct->imgs[i].w * ct->imgs[i].h;
            }
        }
        printf("  %-14s res %3d var %d  bits 0x%04x row %2d  images %3d (%3d non-null)  pixels %6ld B\n",
               cfg->images_dat, cfg->resource, cfg->variant, cfg->palette_bits,
               base_index / 16, n_images, non_null, chtab_bytes);
        if (shpl_separate) dat_close(&dshpl);
        dat_close(&dimg);
    }

    /* --------- serialize blob --------- */
    /* Layout: [header][chtab dir][per-chtab image dirs][pixel data]
     * header:        'PSPR' u16 version u16 chtab_count
     * chtab dir ent: char dat[16]; u16 resource; u16 palette_bits; u16 variant;
     *                u16 n_images; rgb_type vga[16] (48); u32 image_dir_off;
     *                u32 rsvd = 80 bytes
     * image dir ent: u16 w; u16 h; u32 pixel_off  = 8 bytes  (pixel_off 0 => NULL)
     * All offsets are from the start of the blob, little-endian. */
    const int HDR = 8;
    const int CDIR = 80;
    const int IDIR = 8;

    long chtab_dir_bytes = (long)CDIR * chtab_count;
    long image_dir_bytes = 0;
    for (int c = 0; c < chtab_count; ++c) image_dir_bytes += (long)IDIR * chtabs[c].n_images;

    long image_dir_base = HDR + chtab_dir_bytes;
    long pixel_base = image_dir_base + image_dir_bytes;

    /* precompute per-chtab image_dir_off and per-image pixel_off */
    long* chtab_idir_off = (long*) calloc(chtab_count, sizeof(long));
    long cur_idir = image_dir_base;
    long cur_pix = pixel_base;
    for (int c = 0; c < chtab_count; ++c) {
        chtab_idir_off[c] = cur_idir;
        cur_idir += (long)IDIR * chtabs[c].n_images;
    }

    FILE* out = fopen(out_path, "wb");
    if (!out) { fprintf(stderr, "ERROR: cannot open %s for writing\n", out_path); return 1; }

    /* header */
    fwrite("PSPR", 1, 4, out);
    word version = 1;
    word cc = (word) chtab_count;
    fwrite(&version, 2, 1, out);
    fwrite(&cc, 2, 1, out);

    /* chtab dir */
    for (int c = 0; c < chtab_count; ++c) {
        bchtab_t* ct = &chtabs[c];
        char dat[16];
        memset(dat, 0, sizeof(dat));
        memcpy(dat, ct->dat, strnlen(ct->dat, sizeof(dat)));
        fwrite(dat, 1, 16, out);
        fwrite(&ct->resource, 2, 1, out);
        fwrite(&ct->palette_bits, 2, 1, out);
        fwrite(&ct->variant, 2, 1, out);
        fwrite(&ct->n_images, 2, 1, out);
        fwrite(ct->vga, 1, 48, out);
        uint32_t idir_off = (uint32_t) chtab_idir_off[c];
        uint32_t rsvd = 0;
        fwrite(&idir_off, 4, 1, out);
        fwrite(&rsvd, 4, 1, out);
    }

    /* image dirs (assign pixel offsets in the same running order used below) */
    for (int c = 0; c < chtab_count; ++c) {
        bchtab_t* ct = &chtabs[c];
        for (int i = 0; i < ct->n_images; ++i) {
            word w = (word) ct->imgs[i].w;
            word h = (word) ct->imgs[i].h;
            uint32_t poff = 0;
            if (ct->imgs[i].pixels) {
                poff = (uint32_t) cur_pix;
                cur_pix += (long)ct->imgs[i].w * ct->imgs[i].h;
            }
            fwrite(&w, 2, 1, out);
            fwrite(&h, 2, 1, out);
            fwrite(&poff, 4, 1, out);
        }
    }

    /* pixel data (same order) */
    for (int c = 0; c < chtab_count; ++c) {
        bchtab_t* ct = &chtabs[c];
        for (int i = 0; i < ct->n_images; ++i) {
            if (ct->imgs[i].pixels) {
                fwrite(ct->imgs[i].pixels, 1, (size_t)ct->imgs[i].w * ct->imgs[i].h, out);
            }
        }
    }

    long total = ftell(out);
    fclose(out);

    printf("\nWrote %s: %d chtabs, %ld bytes total (pixels start at %ld)\n",
           out_path, chtab_count, total, pixel_base);

    /* cleanup */
    for (int c = 0; c < chtab_count; ++c) {
        for (int i = 0; i < chtabs[c].n_images; ++i) free(chtabs[c].imgs[i].pixels);
        free(chtabs[c].imgs);
    }
    free(chtabs);
    free(chtab_idir_off);
    return 0;
}
