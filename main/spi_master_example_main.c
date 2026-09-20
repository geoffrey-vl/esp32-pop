/* SPI Master example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "dat_file.h"
#include "dat_image.h"

/* Set to 1 to run the standalone audio self-test (walks every sound resource,
 * plays each digi sound through the DAC feeder) instead of the full game. Handy
 * for isolating the audio pipeline from graphics/game-loop issues. Set back to 0
 * for normal gameplay. */
#define POP_AUDIO_SELFTEST 0

/*
 This code displays some fancy graphics on the 320x240 LCD on an ESP-WROVER_KIT board.
 This example demonstrates the use of both spi_device_transmit as well as
 spi_device_queue_trans/spi_device_get_trans_result and pre-transmit callbacks.

 Some info about the ILI9341: It has a C/D line, which is connected to a GPIO here. It expects this
 line to be low for a command and high for data. We use a pre-transmit callback here to control that
 line: every transaction has as the user-definable argument the needed state of the D/C line and just
 before the transaction is sent, the callback will set this line to the correct state.
*/

//////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////// Please update the following configuration according to your HardWare spec /////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////
#define LCD_HOST    SPI3_HOST

#define PIN_NUM_MISO 19 // D19
#define PIN_NUM_MOSI 23 // D23
#define PIN_NUM_CLK  18 // D18
#define PIN_NUM_CS   5 // D5

#define PIN_NUM_DC   21 // D21
#define PIN_NUM_RST  22 // D22
//Backlight is hard-wired on and not driven by the MCU. Set to a real GPIO to
//re-enable MCU control; leave as -1 to compile out the backlight code.
//NOTE: this must be a plain integer literal (not GPIO_NUM_NC), because the
//`#if PIN_NUM_BCKL >= 0` guards below are evaluated by the preprocessor, which
//treats unknown identifiers as 0 and would wrongly keep the backlight code.
#define PIN_NUM_BCKL -1

#define LCD_BK_LIGHT_ON_LEVEL   0

//To speed up transfers, every SPI transfer sends a bunch of lines. This define specifies how many. More means more memory use,
//but less overhead for setting up / finishing transfers. Make sure 240 is dividable by this.
#define PARALLEL_LINES 8   // 8 rows/band: two 320xN RGB565 DMA line buffers cost
                           // 320*8*2*2 = 10KB (vs 20KB at 16). Frees ~10KB of the
                           // scarce 8-bit DRAM at the cost of 2x more SPI bands/frame.

//Panel geometry.
#define LCD_WIDTH  320
#define LCD_HEIGHT 240

//DAT file / resource to show on the display. The palette is discovered
//automatically from the DAT, so it does not need to be configured here.
#define TITLE_DAT_NAME    "TITLE.DAT"
#define TITLE_IMAGE_ID    51

/*
 The LCD needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

//Place data into DRAM. Constant data gets placed into DROM by default, which is not accessible by DMA.
DRAM_ATTR static const lcd_init_cmd_t ili_init_cmds[] = {
    /* Power control B, power control = 0, DC_ENA = 1 */
    {0xCF, {0x00, 0x83, 0X30}, 3},
    /* Power on sequence control,
     * cp1 keeps 1 frame, 1st frame enable
     * vcl = 0, ddvdh=3, vgh=1, vgl=2
     * DDVDH_ENH=1
     */
    {0xED, {0x64, 0x03, 0X12, 0X81}, 4},
    /* Driver timing control A,
     * non-overlap=default +1
     * EQ=default - 1, CR=default
     * pre-charge=default - 1
     */
    {0xE8, {0x85, 0x01, 0x79}, 3},
    /* Power control A, Vcore=1.6V, DDVDH=5.6V */
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    /* Pump ratio control, DDVDH=2xVCl */
    {0xF7, {0x20}, 1},
    /* Driver timing control, all=0 unit */
    {0xEA, {0x00, 0x00}, 2},
    /* Power control 1, GVDD=4.75V */
    {0xC0, {0x26}, 1},
    /* Power control 2, DDVDH=VCl*2, VGH=VCl*7, VGL=-VCl*3 */
    {0xC1, {0x11}, 1},
    /* VCOM control 1, VCOMH=4.025V, VCOML=-0.950V */
    {0xC5, {0x35, 0x3E}, 2},
    /* VCOM control 2, VCOMH=VMH-2, VCOML=VML-2 */
    {0xC7, {0xBE}, 1},
    /* Memory access control, MX=MY=0, MV=1, ML=0, BGR=1, MH=0 */
    {0x36, {0x28}, 1},
    /* Pixel format, 16bits/pixel for RGB/MCU interface */
    {0x3A, {0x55}, 1},
    /* Frame rate control, f=fosc, 70Hz fps */
    {0xB1, {0x00, 0x1B}, 2},
    /* Enable 3G, disabled */
    {0xF2, {0x08}, 1},
    /* Gamma set, curve 1 */
    {0x26, {0x01}, 1},
    /* Positive gamma correction */
    {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
    /* Negative gamma correction */
    {0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
    /* Column address set, SC=0, EC=0xEF */
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
    /* Page address set, SP=0, EP=0x013F */
    {0x2B, {0x00, 0x00, 0x01, 0x3f}, 4},
    /* Memory write */
    {0x2C, {0}, 0},
    /* Entry mode set, Low vol detect disabled, normal display */
    {0xB7, {0x07}, 1},
    /* Display function control */
    {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
    /* Sleep out */
    {0x11, {0}, 0x80},
    /* Display on */
    {0x29, {0}, 0x80},
    {0, {0}, 0xff},
};

/* Send a command to the LCD. Uses spi_device_polling_transmit, which waits
 * until the transfer is complete.
 *
 * Since command transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void lcd_cmd(spi_device_handle_t spi, const uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length = 8;                   //Command is 8 bits
    t.tx_buffer = &cmd;             //The data is the cmd itself
    t.user = (void*)0;              //D/C needs to be set to 0
    ret = spi_device_polling_transmit(spi, &t); //Transmit!
    assert(ret == ESP_OK);          //Should have had no issues.
}

/* Send data to the LCD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 *
 * Since data transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void lcd_data(spi_device_handle_t spi, const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t t;
    if (len == 0) {
        return;    //no need to send anything
    }
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length = len * 8;             //Len is in bytes, transaction length is in bits.
    t.tx_buffer = data;             //Data
    t.user = (void*)1;              //D/C needs to be set to 1
    ret = spi_device_polling_transmit(spi, &t); //Transmit!
    assert(ret == ESP_OK);          //Should have had no issues.
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
void lcd_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(PIN_NUM_DC, dc);
}

//Initialize the display
void lcd_init(spi_device_handle_t spi)
{
    int cmd = 0;
    const lcd_init_cmd_t* lcd_init_cmds = ili_init_cmds;

    //Initialize non-SPI GPIOs
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = ((1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST));
#if PIN_NUM_BCKL >= 0
    io_conf.pin_bit_mask |= (1ULL << PIN_NUM_BCKL);
#endif
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    //Reset the display
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    printf("LCD ILI9341 initialization.\n");

    //Send all the commands
    while (lcd_init_cmds[cmd].databytes != 0xff) {
        lcd_cmd(spi, lcd_init_cmds[cmd].cmd);
        lcd_data(spi, lcd_init_cmds[cmd].data, lcd_init_cmds[cmd].databytes & 0x1F);
        if (lcd_init_cmds[cmd].databytes & 0x80) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        cmd++;
    }

#if PIN_NUM_BCKL >= 0
    ///Enable backlight (only when the backlight pin is driven by the MCU)
    gpio_set_level(PIN_NUM_BCKL, LCD_BK_LIGHT_ON_LEVEL);
#endif
}

/* To send a set of lines we have to send a command, 2 data bytes, another command, 2 more data bytes and another command
 * before sending the line data itself; a total of 6 transactions. (We can't put all of this in just one transaction
 * because the D/C line needs to be toggled in the middle.)
 * This routine queues these commands up as interrupt transactions so they get
 * sent faster (compared to calling spi_device_transmit several times), and at
 * the mean while the lines for next transactions can get calculated.
 */
static void send_lines(spi_device_handle_t spi, int ypos, uint16_t *linedata)
{
    esp_err_t ret;
    int x;
    //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    //function is finished because the SPI driver needs access to it even while we're already calculating the next line.
    static spi_transaction_t trans[6];

    //In theory, it's better to initialize trans and data only once and hang on to the initialized
    //variables. We allocate them on the stack, so we need to re-init them each call.
    for (x = 0; x < 6; x++) {
        memset(&trans[x], 0, sizeof(spi_transaction_t));
        if ((x & 1) == 0) {
            //Even transfers are commands
            trans[x].length = 8;
            trans[x].user = (void*)0;
        } else {
            //Odd transfers are data
            trans[x].length = 8 * 4;
            trans[x].user = (void*)1;
        }
        trans[x].flags = SPI_TRANS_USE_TXDATA;
    }
    trans[0].tx_data[0] = 0x2A;         //Column Address Set
    trans[1].tx_data[0] = 0;            //Start Col High
    trans[1].tx_data[1] = 0;            //Start Col Low
    trans[1].tx_data[2] = (320 - 1) >> 8;   //End Col High
    trans[1].tx_data[3] = (320 - 1) & 0xff; //End Col Low
    trans[2].tx_data[0] = 0x2B;         //Page address set
    trans[3].tx_data[0] = ypos >> 8;    //Start page high
    trans[3].tx_data[1] = ypos & 0xff;  //start page low
    trans[3].tx_data[2] = (ypos + PARALLEL_LINES - 1) >> 8; //end page high
    trans[3].tx_data[3] = (ypos + PARALLEL_LINES - 1) & 0xff; //end page low
    trans[4].tx_data[0] = 0x2C;         //memory write
    trans[5].tx_buffer = linedata;      //finally send the line data
    trans[5].length = 320 * 2 * 8 * PARALLEL_LINES;  //Data length, in bits
#if CONFIG_LCD_BUFFER_IN_PSRAM
    trans[5].flags = SPI_TRANS_DMA_USE_PSRAM; //using PSRAM
#else
    trans[5].flags = 0; //undo SPI_TRANS_USE_TXDATA flag
#endif

    //Queue all transactions.
    for (x = 0; x < 6; x++) {
        ret = spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
        assert(ret == ESP_OK);
    }

    //When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
    //mostly using DMA, so the CPU doesn't have much to do here. We're not going to wait for the transaction to
    //finish because we may as well spend the time calculating the next line. When that is done, we can call
    //send_line_finish, which will wait for the transfers to be done and check their status.
}

static void send_line_finish(spi_device_handle_t spi)
{
    spi_transaction_t *rtrans;
    esp_err_t ret;
    //Wait for all 6 transactions to be done and get back the results.
    for (int x = 0; x < 6; x++) {
        ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
        assert(ret == ESP_OK);
        //We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
    }
}

//Stream a decoded image to the LCD top-left, padding the rest of the 320x240
//screen with black. Rows are converted from the compact 4-bit indexed image to
//RGB565 on the fly (dat_image_render_row), so no full-screen framebuffer and no
//large RGB565 image buffer are needed - important on boards without PSRAM.
//Passing img==NULL blanks the whole screen.
static void display_image(spi_device_handle_t spi, const dat_image_t *img)
{
    uint16_t *lines[2];
#if CONFIG_LCD_BUFFER_IN_PSRAM
    uint32_t mem_cap = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA;
    printf("Get LCD buffer from PSRAM\n");
#else
    uint32_t mem_cap = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    printf("Get LCD buffer from internal\n");
#endif

    //Allocate memory for the pixel buffers
    for (int i = 0; i < 2; i++) {
        lines[i] = spi_bus_dma_memory_alloc(LCD_HOST, LCD_WIDTH * PARALLEL_LINES * sizeof(uint16_t), mem_cap);
        assert(lines[i] != NULL);
    }

    //Scratch row used when the image is wider than the screen (needs clipping).
    //dat_image_render_row() always writes img->width pixels, so it cannot write
    //directly into a 320-pixel line if the image is wider.
    int img_w = (img != NULL) ? img->width  : 0;
    int img_h = (img != NULL) ? img->height : 0;
    int copy_w = (img_w < LCD_WIDTH) ? img_w : LCD_WIDTH;
    uint16_t *scratch = NULL;
    if (img_w > LCD_WIDTH) {
        scratch = malloc((size_t)img_w * sizeof(uint16_t));
        assert(scratch != NULL);
    }

    //Indexes of the line currently being sent to the LCD and the line we're calculating.
    int sending_line = -1;
    int calc_line = 0;

    for (int y = 0; y < LCD_HEIGHT; y += PARALLEL_LINES) {
        uint16_t *dst = lines[calc_line];
        //Fill this band of PARALLEL_LINES rows from the image, padding with black.
        for (int r = 0; r < PARALLEL_LINES; r++) {
            int sy = y + r;
            uint16_t *drow = dst + r * LCD_WIDTH;
            if (img != NULL && sy < img_h) {
                if (scratch != NULL) {
                    dat_image_render_row(img, sy, scratch);
                    memcpy(drow, scratch, (size_t)copy_w * sizeof(uint16_t));
                } else {
                    dat_image_render_row(img, sy, drow);
                }
                if (copy_w < LCD_WIDTH) {
                    memset(drow + copy_w, 0, (size_t)(LCD_WIDTH - copy_w) * sizeof(uint16_t));
                }
            } else {
                memset(drow, 0, LCD_WIDTH * sizeof(uint16_t)); //0x0000 = black
            }
        }
        if (sending_line != -1) {
            send_line_finish(spi);
        }
        sending_line = calc_line;
        calc_line = (calc_line == 1) ? 0 : 1;
        send_lines(spi, y, lines[sending_line]);
    }
    send_line_finish(spi);

    free(scratch);
    free(lines[0]);
    free(lines[1]);
}

//Decode a 16-color image resource from a DAT file and show it top-left, blanking the rest.
//The palette is discovered automatically from the DAT (see read_dat_palette_for).
//Kept as a debug helper (the SDLPoP engine now drives the display); mark it
//unused so -Werror=unused-function does not fail the build.
__attribute__((unused))
static void show_dat_image(spi_device_handle_t spi, const char *dat_name, int16_t id)
{
    static const char *TAG = "dat_image";

    uint8_t *res = NULL;
    size_t res_size = 0;
    esp_err_t err = read_dat_resource(dat_name, id, &res, &res_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "resource %d not found in %s (err=%d)", id, dat_name, err);
        display_image(spi, NULL); //blank the screen
        return;
    }

    dat_image_t img;
    esp_err_t dec = dat_image_decode(res, res_size, &img);
    free(res);
    if (dec != ESP_OK) {
        ESP_LOGW(TAG, "%s resource %d is not a decodable 16-color image (err=0x%x)",
                 dat_name, id, dec);
        display_image(spi, NULL); //blank the screen
        return;
    }

    ESP_LOGI(TAG, "%s resource %d: %dx%d, drawn top-left",
             dat_name, id, img.width, img.height);

    //Discover and apply the image's palette. If none is found, dat_image_decode
    //already installed the fixed POP1 palette as a fallback.
    uint8_t *pal = NULL;
    size_t pal_size = 0;
    int16_t pal_id = -1;
    if (read_dat_palette_for(dat_name, id, &pal, &pal_size, &pal_id) == ESP_OK) {
        if (dat_image_set_palette(&img, pal, pal_size) == ESP_OK) {
            ESP_LOGI(TAG, "%s resource %d: using palette %d", dat_name, id, pal_id);
        } else {
            ESP_LOGW(TAG, "%s palette %d has unexpected size %u; using default",
                     dat_name, pal_id, (unsigned)pal_size);
        }
        free(pal);
    } else {
        ESP_LOGW(TAG, "no palette found in %s; using default palette", dat_name);
    }

    display_image(spi, &img);
    dat_image_free(&img);
}

/* SPI handle and pre-allocated DMA line buffers used by pop_present_indexed().
 * Allocated once at startup so the per-frame present path never allocates. */
static spi_device_handle_t s_pop_spi = NULL;
static uint16_t *s_pop_lines[2] = { NULL, NULL };

/* Present a 320x200 INDEX8 SDLPoP frame to the LCD, letterboxed vertically in
 * the 320x240 panel, mapping palette indices to RGB565 via the engine's current
 * VGA palette (6-bit channels). Reuses two pre-allocated DMA line buffers so the
 * per-frame path allocates nothing. */
extern unsigned char palette[]; /* rgb_type palette[256], packed r,g,b bytes (0..63) */

/* Global fade level applied when building the palette LUT below. 256 = full
 * brightness, 0 = black. USE_FADE is off in this port, so instead of the DOS
 * palette-register fade the engine ramps this value (pop_fade_ramp in seg001.c)
 * and re-presents the current frame: the whole fade is just a cheap scale on the
 * 256-entry LUT that is rebuilt every present anyway - no extra buffers, no
 * re-render. Written and read only from the game task. */
int g_pop_fade_bright = 256;

void pop_present_indexed(const unsigned char *pix, int w, int h, int pitch)
{
    if (s_pop_spi == NULL || s_pop_lines[0] == NULL || pix == NULL) return;

    /* Build an index -> RGB565 (byte-swapped for the panel) lookup, scaled by
     * the current fade brightness. */
    int bright = g_pop_fade_bright;
    if (bright < 0) bright = 0; else if (bright > 256) bright = 256;
    uint16_t lut[256];
    for (int i = 0; i < 256; i++) {
        int pr = (palette[i * 3 + 0] * bright) >> 8;
        int pg = (palette[i * 3 + 1] * bright) >> 8;
        int pb = (palette[i * 3 + 2] * bright) >> 8;
        uint16_t r5 = (uint16_t)((pr << 2) >> 3);
        uint16_t g6 = (uint16_t)((pg << 2) >> 2);
        uint16_t b5 = (uint16_t)((pb << 2) >> 3);
        uint16_t v = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        lut[i] = (uint16_t)((v >> 8) | (v << 8));
    }

    int y_off = (LCD_HEIGHT - h) / 2; /* center 200 rows in 240: 20px top/bottom */
    int cw = (w < LCD_WIDTH) ? w : LCD_WIDTH;
    int sending = -1;
    int calc = 0;
    for (int y = 0; y < LCD_HEIGHT; y += PARALLEL_LINES) {
        uint16_t *dst = s_pop_lines[calc];
        for (int r = 0; r < PARALLEL_LINES; r++) {
            int sy = y + r - y_off;
            uint16_t *drow = dst + r * LCD_WIDTH;
            if (sy >= 0 && sy < h) {
                const unsigned char *srow = pix + (size_t)sy * pitch;
                for (int x = 0; x < cw; x++) drow[x] = lut[srow[x]];
                for (int x = cw; x < LCD_WIDTH; x++) drow[x] = 0;
            } else {
                memset(drow, 0, LCD_WIDTH * sizeof(uint16_t)); /* black bar */
            }
        }
        if (sending != -1) send_line_finish(s_pop_spi);
        sending = calc;
        calc = (calc == 1) ? 0 : 1;
        send_lines(s_pop_spi, y, s_pop_lines[sending]);
    }
    send_line_finish(s_pop_spi);
}

/* --- Player input (P5) ---------------------------------------------------
 * Five momentary push-buttons wired active-low from a GPIO to GND, using the
 * ESP32 internal pull-ups. The engine polls pop_read_buttons() from its ESP
 * process_events() path and maps the bits onto SDL arrow/shift scancodes. */
#define POP_BTN_LEFT   (1u << 0)
#define POP_BTN_RIGHT  (1u << 1)
#define POP_BTN_UP     (1u << 2)
#define POP_BTN_DOWN   (1u << 3)
#define POP_BTN_SHIFT  (1u << 4)
#define POP_BTN_PAUSE  (1u << 5)

#define POP_PIN_LEFT   GPIO_NUM_32
#define POP_PIN_RIGHT  GPIO_NUM_33
#define POP_PIN_UP     GPIO_NUM_25
#define POP_PIN_DOWN   GPIO_NUM_27   // moved from 26: GPIO26 is now the audio DAC output
#define POP_PIN_SHIFT  GPIO_NUM_13   // moved from 27

// Optional sixth button that pauses/resumes the game (maps to the Esc key).
// Disabled by default: set this to a spare GPIO to enable it. Must be a plain
// integer literal (not GPIO_NUM_NC), because the `#if POP_PIN_PAUSE >= 0`
// guards below are evaluated by the preprocessor. Wire it active-low to GND
// like the other buttons (the internal pull-up is enabled).
#define POP_PIN_PAUSE  -1

void pop_input_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << POP_PIN_LEFT) | (1ULL << POP_PIN_RIGHT) |
                        (1ULL << POP_PIN_UP)   | (1ULL << POP_PIN_DOWN)  |
                        (1ULL << POP_PIN_SHIFT)
#if POP_PIN_PAUSE >= 0
                        | (1ULL << POP_PIN_PAUSE)
#endif
                        ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

/* Return a bitmask of currently-pressed buttons (active-low: 0 == pressed). */
unsigned int pop_read_buttons(void)
{
    unsigned int m = 0;
    if (gpio_get_level(POP_PIN_LEFT)  == 0) m |= POP_BTN_LEFT;
    if (gpio_get_level(POP_PIN_RIGHT) == 0) m |= POP_BTN_RIGHT;
    if (gpio_get_level(POP_PIN_UP)    == 0) m |= POP_BTN_UP;
    if (gpio_get_level(POP_PIN_DOWN)  == 0) m |= POP_BTN_DOWN;
    if (gpio_get_level(POP_PIN_SHIFT) == 0) m |= POP_BTN_SHIFT;
#if POP_PIN_PAUSE >= 0
    if (gpio_get_level(POP_PIN_PAUSE) == 0) m |= POP_BTN_PAUSE;
#endif
    return m;
}

/* --- SDLPoP engine bridge (implemented in pop_glue.c) --- */
extern void pop_main(void);
extern void pop_audio_selftest(void);   /* standalone audio test (no graphics) */
extern int pop_kid_x(void);
extern int pop_kid_y(void);
extern int pop_kid_frame(void);
extern int pop_kid_room(void);
extern int pop_kid_alive(void);
extern int pop_current_level(void);

/* SDLPoP command-line globals (data.c); the engine expects at least argv[0]. */
extern int g_argc;
extern char **g_argv;
static char *s_pop_argv[] = { "pop", NULL };

/* Reserve the three full-screen 320x200 buffers up front (in sdlpop_shim.c),
 * before pop_main() fragments the heap. */
extern void pop_screen_pool_init(void);

/* Handle of the SDLPoP engine task, so the monitor can read its stack
 * high-water mark. Declared unconditionally (referenced in app_main). */
static TaskHandle_t s_pop_game_task = NULL;

static void pop_game_task(void *arg){
    (void)arg;
    g_argc = 1;
    g_argv = s_pop_argv;
#if POP_AUDIO_SELFTEST
    ESP_LOGI("pop_port", "[P7] running audio self-test (no graphics)...");
    pop_audio_selftest();
    ESP_LOGI("pop_port", "[P7] audio self-test finished; idling");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
#else
    ESP_LOGI("pop_port", "[P1] starting pop_main()...");
    pop_main();                     // normally never returns
    ESP_LOGW("pop_port", "[P1] pop_main() returned unexpectedly");
    vTaskDelete(NULL);
#endif
}

#if !POP_AUDIO_SELFTEST
static void pop_monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        /* Report the TRUE byte-addressable (8-bit) DRAM figures: plain
         * MALLOC_CAP_INTERNAL includes the 64KB pure-IRAM region the game
         * cannot use for surface data, so it over-reports by ~64KB. Also print
         * the game task's stack high-water mark (min free bytes ever) so we can
         * see how much of its stack is really used after the 32KB->20KB trim. */
        unsigned stack_free = s_pop_game_task
            ? (unsigned)(uxTaskGetStackHighWaterMark(s_pop_game_task) * sizeof(StackType_t))
            : 0;
        ESP_LOGI("pop_port",
                 "[P1] lvl=%d kid(x=%d y=%d frame=%d room=%d alive=%d) free8=%u largest8=%u stackmin=%u",
                 pop_current_level(), pop_kid_x(), pop_kid_y(), pop_kid_frame(),
                 pop_kid_room(), pop_kid_alive(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                 stack_free);
    }
}
#endif

void app_main(void)
{
    esp_err_t ret;
    spi_device_handle_t spi;

    /* --- P1 SDLPoP port bring-up: run the engine headless -------------------
     * The game engine runs on its own FreeRTOS task with a large stack (deep
     * call tree). Video/audio/input are shimmed (no display yet); we only want
     * to confirm the flash-backed DAT reader works and the game loop advances.
     * A monitor task logs heap + Kid position/level so we can watch progress
     * over the serial console. */
    ESP_LOGI("pop_port", "[P1] internal heap: free=%u bytes, largest contiguous block=%u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    /* Reserve the three full-screen 320x200 buffers now, while the heap is
     * pristine (the two big regions are still intact). Doing this before SPI/LCD
     * init and task creation guarantees all three 64KB blocks find contiguous
     * byte-addressable RAM; later they cannot, due to fragmentation. */
    pop_screen_pool_init();

    /* Parse the embedded pre-decoded sprite blob so the engine can back its
     * chtab image_type surfaces with flash pixels (P2). */
    extern void pop_sprites_init(void);
    pop_sprites_init();

    /* Configure the five gameplay push-buttons (P5). */
    pop_input_init();

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PARALLEL_LINES * 320 * 2 + 8
    };
    spi_device_interface_config_t devcfg = {
#ifdef CONFIG_LCD_OVERCLOCK
        .clock_speed_hz = 40 * 1000 * 1000,     //Clock out at 40 MHz (needs clean/short wiring)
#else
        .clock_speed_hz = 26 * 1000 * 1000,     //Clock out at 26 MHz (~48ms/frame, fits the ~12fps game tick)
#endif
        .mode = 0,                              //SPI mode 0
        .spics_io_num = PIN_NUM_CS,             //CS pin
        .queue_size = 7,                        //We want to be able to queue 7 transactions at a time
        .pre_cb = lcd_spi_pre_transfer_callback, //Specify pre-transfer callback to handle D/C line
    };
    //Initialize the SPI bus
    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    //Attach the LCD to the SPI bus
    ret = spi_bus_add_device(LCD_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
    //Initialize the LCD
    lcd_init(spi);

    /* Keep the SPI handle and reserve the two DMA line buffers the present path
     * reuses every frame (P4). Do it now, before the engine task fragments the
     * heap, so the DMA-capable RAM is available. */
    s_pop_spi = spi;
    for (int i = 0; i < 2; i++) {
        s_pop_lines[i] = spi_bus_dma_memory_alloc(LCD_HOST,
                              LCD_WIDTH * PARALLEL_LINES * sizeof(uint16_t),
                              MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        assert(s_pop_lines[i] != NULL);
    }

    /* Start the SDLPoP engine and a monitor. LCD present is still a no-op in P1;
     * the existing TITLE.DAT viewer is retired now that the engine drives things. */
    xTaskCreatePinnedToCore(pop_game_task, "pop_game", 12288, NULL, 5, &s_pop_game_task, 1);
#if !POP_AUDIO_SELFTEST
    /* The audio self-test does its own heap logging + integrity checks; skip the
     * monitor so its periodic largest-free-block walk can't crash on (and mask)
     * a corruption the self-test is trying to localize. */
    xTaskCreate(pop_monitor_task, "pop_mon", 4096, NULL, 3, NULL);
#endif
}
