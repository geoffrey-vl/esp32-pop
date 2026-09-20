| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-H21 | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 | ESP32-S31 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | --------- | -------- | -------- | -------- | -------- | --------- |

# SPI Host Driver Example

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This example aims to show how to use SPI Host driver API, like `spi_transaction_t` and spi_device_queue.

If you are looking for code to drive LCDs in general, rather than code that uses the SPI master, that may be a better example to look at as it uses ESP-IDFs built-in LCD support rather than doing all the low-level work itself, which can be found at `examples/peripherals/lcd/tjpgd/`

## How to Use Example

### Hardware Required

* An ESP development board with an ILI9341 SPI LCD

**Connection** :

Depends on boards. The GPIO number used by this example can be changed in `spi_master_example_main.c` No wiring is required on ESP-WROVER-KIT

Especially, please pay attention to the level used to turn on the LCD backlight, some LCD module needs a low level to turn it on, while others take a high level. You can change the backlight level macro LCD_BK_LIGHT_ON_LEVEL in `spi_master_example_main.c`.

### Wiring

The pin assignments below are the defaults in `main/spi_master_example_main.c`.

**ILI9341 SPI display**

| LCD pin | ESP32 GPIO | Notes |
| ------- | ---------- | ----- |
| VCC | 3V3 | |
| GND | GND | |
| CLK / SCK | GPIO18 | SPI clock |
| MOSI / SDI | GPIO23 | SPI data to LCD |
| MISO / SDO | GPIO19 | SPI data from LCD (optional) |
| CS | GPIO5 | Chip select |
| DC / RS | GPIO21 | Data/command select |
| RST | GPIO22 | Reset |
| LED / BLK | 3V3 | Backlight hard-wired on (set `PIN_NUM_BCKL` to a GPIO for MCU control) |

**Player buttons** — five momentary push-buttons. Wire one leg of each button to
its GPIO and the other leg to **GND**. The firmware enables the ESP32 internal
pull-ups, so a pressed button reads LOW; no external resistors are needed.

| Button | ESP32 GPIO | In-game action |
| ------ | ---------- | -------------- |
| Left | GPIO32 | Walk / run left |
| Right | GPIO33 | Walk / run right |
| Up | GPIO25 | Jump / climb up |
| Down | GPIO27 | Crouch / climb down / pick up |
| Shift | GPIO13 | Grab ledge / careful step / draw & sheathe sword |

> GPIO26 is reserved for the audio output (internal DAC -> NS4871 amp), so the
> Down and Shift buttons were moved off it (Down 26->27, Shift 27->13).
> Avoid the GPIOs already used by the LCD (5, 18, 19, 21, 22, 23) and the
> input-only pins 34–39 (they have no internal pull-up).

**Full wiring diagram**

Drawn for a common 30-pin **ESP32 DevKit v1** (DOIT/WROOM) board. The five
buttons all live on the left header and the LCD pins on the right header, so the
two peripherals wire to opposite sides.

```
                          ESP32 DevKit v1
                        +-----------------+
                        |                 |
                        |                 |        ILI9341 SPI TFT
                     EN o                 o GND    +--------------+
                   VP36 o                 o 23 ----| VCC   -> 3V3 |
                   VN39 o                 o 22 ----| GND   -> GND |
                     34 o                 o TX0    | CLK   -> 18  |
  Buttons:           35 o                 o RX0    | MOSI  -> 23  |
  (Left)  GND-[/_]-- 32 o                 o 21 ----| MISO  -> 19  |
  (Right) GND-[/_]-- 33 o                 o 19 ----| CS    -> 5   |
  (Up)    GND-[/_]-- 25 o                 o 18 ----| DC/RS -> 21  |
  (Audio) DAC ------ 26 o                 o 5  ----| RST   -> 22  |
  (Down)  GND-[/_]-- 27 o                 o 17     | LED   -> 3V3 |
                     14 o                 o 16     +--------------+
                     12 o                 o 4
  (Shift) GND-[/_]-- 13 o                 o 0
                    GND o-----------------o 2   (optional: LCD backlight)
                    VIN o                 o 15
                        |                 o GND
                        |      [USB]      o 3V3 --> LCD VCC / LED
                        +-----------------+
```

Legend: `o` = header pin · `[ /_ ]` = momentary push-button. The firmware
enables the ESP32 internal pull-ups, so an open button reads HIGH and a pressed
button pulls its GPIO to GND (LOW). Tie all the button GNDs and the LCD GND to a
common board GND.

**Audio (NS4871 mono amplifier)**

Sound is output on **GPIO26**, which is DAC channel 1 (the ESP32's built-in
8-bit digital-to-analog converter), driven continuously over DMA. Route it to a
mono analog Class-AB amplifier such as the **NS4871** (4 Ω / 2 W speaker):

| Signal | From | To |
| ------ | ---- | -- |
| Audio | GPIO26 (DAC) | amp input tip (via ~10 kΩ volume pot / attenuator) |
| Ground | ESP32 GND | amp GND |

- The **volume pot / attenuator is important**: the DAC swings the full 0–3.3 V,
  which is much hotter than the ~0.5–1 V the amp expects and will clip/overdrive
  without it. A software attenuation (`POP_AUDIO_VOLUME`, 0–256, in
  `main/sdlpop_shim.c`) is also available for bare-wire bench tests.
- If the amp's input is not already AC-coupled, add a 1–10 µF series capacitor to
  block the DAC's DC bias.
- Power the amp from its **own supply/battery** (its 2 W speaker current would
  brown out the ESP32 3V3 rail); only the ground is shared.

### Build and Flash

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Example Output

On ESP-WROVER-KIT there will be:

```
LCD ILI9341 initialization.
```

At the meantime a *Prince of Persia* title image (resource 51 from
`TITLE.DAT`) will be displayed in the top-left of the connected LCD screen,
with the rest of the screen left black.

## Troubleshooting

For any technical queries, please open an [issue] (https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.

## License

This project is distributed under the **GNU General Public License, version 2**
(see [LICENSE](LICENSE)).

The reason is that it reuses source code from the
[Princed Resources](http://forum.princed.org) project (the *Prince of Persia*
DAT format library), which is licensed under GPLv2. Because the firmware links
against that GPLv2 code, the combined work is also GPLv2.

### Reused third-party code and modifications

The `main/prince_dat` directory contains files taken from Princed
Resources:

* `src/dat.c`, `src/autodetect.c`, `src/auxiliary.c`, `src/rle_uncompress.c`,
  `src/lzg_uncompress.c` and the headers under `include/` are copied
  **verbatim** (their original GPL copyright notices are retained).
* Modifications made for this port, as required by the GPL:
  * `include/dat.h` — the `PR_DAT_INCLUDE_DATWRITE` define is commented out so
    that only the read path is compiled.
  * `src/pr_disk_shim.c` — **new** file. It replaces the original disk I/O
    loader (`disk.c`'s `mLoadFileArray`) with an in-memory loader that returns
    the DAT bytes embedded into the firmware, looked up by name through the
    generated registry (`dat_registry.h`/`dat_registry.c`), so the DAT reader
    runs unchanged on the ESP32.
  * `src/lzg_uncompress.c` — its 65500-byte decompression work buffer was
    changed from a stack array to a heap allocation so it does not overflow the
    (small) FreeRTOS task stack on the ESP32.
* `main/dat_image.c` — **new** file, *derived* from Princed Resources'
  `image16.c`. It parses the image header, drives the vendored `expandRle` /
  `expandLzg` decompressors and converts the 16-color result to RGB565.

The `main/data/*.DAT` resources are *Prince of Persia* data files; they are all
embedded into the firmware and, at start-up, each one is parsed and classified
by `parse_dat_file()` (see `main/dat_file.c`), which logs every resource it
finds over the serial monitor.

### Displaying a DAT image on the LCD

After parsing, the firmware decodes one 16-color image resource from
`TITLE.DAT` (resource id `51`, configurable via `TITLE_IMAGE_ID` in
`main/spi_master_example_main.c`) and draws it at native resolution in the
top-left corner, leaving the rest of the 320×240 screen black. There is no
scaling or centering; images larger than the panel are clipped.

**How a Prince of Persia image is parsed** (`main/dat_image.c`):

1. **Resource bytes.** `read_dat_resource("TITLE.DAT", 51, …)` returns the raw
   resource content (the DAT reader already verifies the per-resource
   checksum).
2. **Header (6 bytes, little-endian).**

   | Offset | Field | Meaning |
   | ------ | ----- | ------- |
   | 0–1 | `height` | image height in pixels |
   | 2–3 | `width` | image width in pixels |
   | 4 | reserved | must be 0 (or 1) |
   | 5 | `type` | high bits = bits/pixel, low nibble = compression |

   Bits per pixel is `((type >> 4) & 7) + 1`; a 16-color image is 4 bpp. The
   compression algorithm is `type & 0x0F`.
3. **Decompression.** The pixel data after the header is expanded according to
   the algorithm:
   * `0` — raw (uncompressed)
   * `1` / `2` — RLE, left-to-right / up-to-down (`expandRle`)
   * `3` / `4` — LZG, left-to-right / up-to-down (`expandLzg`)

   The "up-to-down" variants store the image transposed (column-major), so the
   decompressed buffer is transposed back to row-major.
4. **Pixel unpacking.** 16-color pixels are packed two per byte
   (`widthInBytes = (width + 1) / 2`): the low nibble is the even column, the
   high nibble is the odd column. Each nibble is a 0–15 palette index. The
   decoded image is kept in this compact 4-bit form (a 320×200 image is only
   ~32 KB, versus ~128 KB as RGB565).
5. **Palette + color conversion.** Prince of Persia images do not embed a
   palette; the palette is a separate resource, and the image-to-palette
   association is *not* stored in the DAT either (in Princed Resources it lives
   in external metadata, `resources.xml`, via folder inheritance). Resource ids
   are also unique only *within* a single DAT — they overlap between DATs (id
   200, for example, exists in every dungeon/palace DAT) — so the lookup is
   keyed by both the DAT filename and the image id. `read_dat_palette_for`
   resolves the palette in two steps:
   - For the stock POP1 DAT files it uses an embedded table (`pal_maps[]`),
     derived from `resources.xml`, that gives each DAT a default palette plus a
     few id-range overrides (e.g. `TITLE.DAT` → palette 50 by default, but 40
     for image 41; `VDUNGEON.DAT` → 200 by default, but 360 for tiles 361–377).
     This reproduces the original game's mapping exactly. A palette id of 0 means
     the image has no VGA palette (monochrome / CGA / EGA / sound / level data),
     in which case the fixed fallback palette is kept.
   - For an unknown or modded DAT it falls back to auto-detection: it finds the
     POP1 4-bit palette resources by their header signature (100 bytes, "16
     colors" marker) and picks the one with the largest id `<=` the image id
     (else the lowest), or the fixed `SAMPLE_PAL16` if the DAT has none.
   A palette resource is
   100 bytes: 16 colors of three 6-bit VGA values (starting at offset 4, each
   `<< 2` to make 8-bit), parsed by `dat_image_set_palette`. When the image is
   pushed to the LCD, each row is converted on the fly (`dat_image_render_row`):
   every nibble indexes the palette and its RGB888 color is reduced to RGB565
   (`r >> 3`, `g >> 2`, `b >> 3`) and stored **byte-swapped**, because the
   ILI9341 receives each pixel most-significant byte first over SPI.

**Memory / no framebuffer.** Rows are converted to RGB565 straight into the
small DMA line buffers and streamed to the panel band by band, so the firmware
never allocates a full-screen framebuffer or a full RGB565 copy of the image.
This keeps peak RAM low enough to run on ESP32 boards without PSRAM (a
320×240×2 framebuffer alone would be 150 KB).

**Color notes.** The panel is initialized with `MADCTL` BGR ordering, matching
the previous JPEG path. If red and blue appear swapped for your display, define
`DAT_IMAGE_SWAP_RB` to `1` (see `main/dat_image.c`) to swap the R and B fields
during RGB565 packing.

