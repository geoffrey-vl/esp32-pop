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

### Build and Flash

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Example Output

On ESP-WROVER-KIT there will be:

```
LCD ILI9341 initialization.
```

At the meantime `ESP32` will be displayed on the connected LCD screen.

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

* `src/dat.c`, `src/autodetect.c`, `src/auxiliary.c` and the headers under
  `include/` are copied **verbatim** (their original GPL copyright notices are
  retained).
* Modifications made for this port, as required by the GPL:
  * `include/dat.h` — the `PR_DAT_INCLUDE_DATWRITE` define is commented out so
    that only the read path is compiled.
  * `src/pr_disk_shim.c` — **new** file. It replaces the original disk I/O
    loader (`disk.c`'s `mLoadFileArray`) with an in-memory loader that returns
    the DAT bytes embedded into the firmware, looked up by name through the
    generated registry (`dat_registry.h`/`dat_registry.c`), so the DAT reader
    runs unchanged on the ESP32.

The `main/data/*.DAT` resources are *Prince of Persia* data files; they are all
embedded into the firmware and, at start-up, each one is parsed and classified
by `parse_dat_file()` (see `main/dat_file.c`), which logs every resource it
finds over the serial monitor.
