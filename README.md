| Supported Targets | ESP32-P4 | ESP32 |
| ----------------- | -------- | ----- |

# TMF8829 SPI Driver & Playground (ESP32-P4)

This project provides an ESP-IDF firmware application for the **ams OSRAM TMF8829** multi-zone direct Time-of-Flight (dToF) optical distance sensor running on an **ESP32-P4** (e.g., M5Stack Unit PoE-P4).

It is ported from the official ams OSRAM Arduino reference driver (`TMF8829_Driver_Arduino_v1.2.5`), preserving the original driver architecture in `src/` while integrating an ESP-IDF hardware shim for high-speed SPI communication (with DMA), GPIO control, USB Serial/JTAG console interaction, and FreeRTOS task yielding for watchdog safety.

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

## Compiling the Protocol Buffers source

This project used NanoBP https://github.com/nanopb/nanopb for Protocol Buffers (a.k.a. ProtoBuf) with small code size.

### Prerequisite

Install protoc and nanopb extension
This only needs to be done once.

```shell
brew install protoc
brew install nanopb
```

### Compile the `.proto` file

This uses the regular protoc compiler with nanopb extension to generate the lightweight stubs.

```shell
cd src/protobuf
protoc --proto_path=../../proto --nanopb_out="$(pwd)" message.proto 
```

---

## Project Structure

- `src/main.cpp`: Application entry point (`app_main`), orchestrating the program flow and main task loop.
- `src/app.cpp` & `src/app.h`: High-level application logic, state machine, and interactive command parser.
- `src/image_pgm.cpp` & `src/image_pgm.h`: ASCII PGM (P2) image exporter, distance map rendering, and single-shot frame capture routines.
- `src/help.cpp` & `src/help.h`: Helper functions for human-readable mode names, pre-configuration labels, and optical profile descriptions.
- `src/net.cpp` & `src/net.h`: Ethernet driver initialization (ESP32-P4 EMAC + IP101 PHY) and raw sensor frame UDP packet forwarder.
- `src/net_handler.cpp` & `src/net_handler.h`: UDP server listener and Protobuf request handler (`Request` / `GetDeviceInfo` -> `DeviceInfo` response).
- `src/protobuf/`: Protocol Buffers definitions and auto-generated Nanopb bindings (`message.pb.c`, `message.pb.h`).
- `src/vendor/nanopb-0.4.9.2/`: Nanopb protocol buffers library (`pb.h`, `pb_common.c`, `pb_encode.c`, `pb_decode.c`).
- `src/tmf8829/`: Dedicated subsystem for TMF8829 sensor communication, control, and data acquisition:
  - `tmf8829_shim.cpp` & `tmf8829_shim.h`: ESP-IDF hardware shim layer (SPI bus with DMA, GPIO for `DE`, USB Serial/JTAG polling, FreeRTOS yields).
  - `tmf8829.c` & `tmf8829.h`: Core ams OSRAM driver implementing SPI protocol framing, bootloader control, firmware download, and result parsing.
  - `tmf8829_firmware.c` & `tmf8829_firmware.h`: Pre-compiled firmware hex image loaded into TMF8829 RAM during initialization.
- `documentation/`: Sensor datasheet (`DS001140`), communication protocol application note (`AN001096`), and the original Arduino reference implementation.
