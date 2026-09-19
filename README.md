| Supported Targets | ESP32-P4 | ESP32 |
| ----------------- | -------- | ----- |

# TMF8829 SPI Driver & Playground (ESP32-P4)

This project provides an ESP-IDF firmware application for the **ams OSRAM TMF8829** multi-zone direct Time-of-Flight (dToF) optical distance sensor running on an **ESP32-P4** (e.g., M5Stack Unit PoE-P4).

It is ported from the official ams OSRAM Arduino reference driver (`TMF8829_Driver_Arduino_v1.2.5`), preserving the original driver architecture in `main/` while integrating an ESP-IDF hardware shim for high-speed SPI communication (with DMA), GPIO control, USB Serial/JTAG console interaction, and FreeRTOS task yielding for watchdog safety.

---

## Hardware Wiring

The default firmware configuration matches the soldered module pinout:

| Signal | ESP32-P4 GPIO | Description |
| :--- | :--- | :--- |
| **MOSI** | `GPIO13` | SPI Master Out / Slave In |
| **SCLK** | `GPIO12` | SPI Clock |
| **MISO** | `GPIO11` | SPI Master In / Slave Out |
| **CS** | `GPIO10` | SPI Chip Select |
| **DE** | `GPIO9` | Device Enable (active high) |
| **GND** | `GND` | Ground |
| **3V3** | `3.3V` | Power Supply |

---

## Configuration & Defaults

Project defaults in `sdkconfig.defaults.esp32p4` include:
- **Flash Size**: 16 MB (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`)
- **Console Interface**: Native USB Serial/JTAG controller as primary console (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`)
- **Main Task Stack Size**: 8 KB (`CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`)
- **SPI DMA**: Auto DMA channel enabled with max transfer size up to 4 KB to support 500-byte FIFO firmware chunks and raw result frames

Project options can be further adjusted using `menuconfig`:

1. Set target (if not already set):
   ```bash
   idf.py set-target esp32p4
   ```

2. Open the configuration menu:
   ```bash
   idf.py menuconfig
   ```

3. In `TMF8829 UDP Forwarder Configuration` (or project configuration), adjust:
   - **SPI Host**: default `2` (`SPI2_HOST`)
   - **SPI Clock**: default `20000000` Hz (20 MHz)
   - **Pin Mapping**: `MOSI` (13), `MISO` (11), `SCLK` (12), `CS` (10), `DE` (9)

---

## Build, Flash, and Monitor

Make sure your ESP-IDF environment is active (e.g., `source ~/.espressif/tools/activate_idf_v5.4.3.sh`), then build, flash, and open the serial monitor:

```bash
idf.py -p <PORT> flash monitor
```

*(Replace `<PORT>` with your serial device port, e.g., `/dev/ttyACM0` or `/dev/cu.usbmodem...`)*

To exit the monitor, use the shortcut `Ctrl + ]`.

---

## Interactive Serial Console

The application provides an interactive command-line interface via the monitor console. Simply press any of the following keys:

| Key | Action | Description |
| :---: | :--- | :--- |
| `h` | **Help** | Prints the list of available commands |
| `e` | **Enable & Download FW** | Enables the sensor via `DE`, powers up, and downloads RAM firmware |
| `m` | **Measure** | Starts continuous distance measurements |
| `s` | **Stop** | Stops continuous measurement |
| `c` / `p` | **Next Configuration** | Cycles through optical configurations (8x8, 16x16, 32x32, 48x32, high accuracy, etc.) |
| `u` | **Get Configuration** | Reads and displays current configuration registers |
| `1` / `o` / `O` | **Single-Shot 48x32 Dump & PGM** | Captures a 48x32 frame (dual sub-frames), prints raw hex dumps, and outputs ASCII PGM (P2) image |
| `a` | **Dump Registers** | Dumps 256 sensor registers over SPI (when stopped) |
| `w` | **Wakeup** | Wakes up sensor from power-down state |
| `P` | **Power Down** | Puts sensor into power-down (standby) state |
| `d` | **Disable** | Drives `DE` low to disable the sensor |
| `z` | **Histogram** | Toggles histogram readout / dumping |
| `x` | **Clock Correction** | Toggles distance clock correction on/off |
| `+` | **Log Level +** | Increases logging verbosity |
| `-` | **Log Level -** | Decreases logging verbosity |
| `#` | **Reset** | Tests software reset on the sensor |
| `b` | **Binary Mode** | Enters binary command input mode |

### Typical Usage Walkthroughs

#### 1. Continuous Distance Measurement
1. Open `idf.py monitor`.
2. Press `e` to enable the sensor and download the RAM firmware image. The console will report `CPU ready` and confirm `state=stopped`.
3. (Optional) Press `c` or `p` to select your preferred optical profile (e.g., 8x8 Default, 8x8 Long Range, 16x16, 32x32, 48x32).
4. Press `m` to start measuring. Measurement frames stream to the console.
5. Press `s` to stop measurement at any time.

#### 2. Single-Shot 48x32 Capture & PGM Image Export
1. Press `e` to ensure the device is initialized.
2. Press `1` (or `o` / `O`).
3. The firmware configures 48x32 mode, captures both Sub-frame 0 (even rows) and Sub-frame 1 (odd rows), outputs raw SPI frame hexdumps, and prints the complete 1536-pixel depth map in Netpbm ASCII PGM (`P2`) format (48 pixels per row across 32 rows).
4. The PGM text can be copied or redirected into a `.pgm` file and viewed in standard image viewers (GIMP, ImageMagick, VS Code PGM viewer).

---

## Project Structure

- `main/main.c`: Application entry point (`app_main`), initializes and runs the `initial_setup()` / `main_loop()` lifecycle.
- `main/tmf8829_shim.cpp` & `tmf8829_shim.h`: ESP-IDF hardware shim layer:
  - SPI bus and device initialization/transactions with DMA support.
  - GPIO output configuration for Device Enable (`DE`).
  - USB Serial/JTAG console non-blocking character polling.
  - FreeRTOS cooperative task delays and yield points to satisfy the Task Watchdog Timer (TWDT).
- `main/tmf8829_app.cpp` & `tmf8829_app.h`: High-level application logic, state machine, single-shot 48x32 PGM exporter, and interactive command parser.
- `main/tmf8829_help.cpp` & `tmf8829_help.h`: Helper functions for human-readable mode names, pre-configuration labels, and optical profile descriptions.
- `main/tmf8829.c` & `tmf8829.h`: Core ams OSRAM driver implementing SPI protocol framing (`0x02` write, `0x03` read), bootloader control, firmware download, and result parsing.
- `main/tmf8829_firmware.c` & `tmf8829_firmware.h`: Pre-compiled firmware hex image loaded into TMF8829 RAM during initialization.
- `documentation/`: Sensor datasheet (`DS001140`), communication protocol application note (`AN001096`), and the original Arduino reference implementation.
