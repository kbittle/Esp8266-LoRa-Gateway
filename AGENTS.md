# Esp8266-LoRa-Gateway — Agent Instructions

## Project Overview
ESP8266 RTOS SDK project for a WiFi (ESP8266) to LoRa (Ra-01H/SX127X) gateway with SK9822 RGB LED status indicator. Uses FreeRTOS, custom PCB for DMB-4770 enclosure.

## Build System
- **Framework**: ESP8266_RTOS_SDK (CMake + GNU Make)
- **Toolchain**: xtensa-lx106-elf-gcc8_4_0-esp-2020r3
- **Project root**: `esp-gateway-project/`
- **Source**: `esp-gateway-project/main/`

### Required Environment Variables
```bash
export PATH="$HOME/esp/xtensa-lx106-elf/bin:$PATH"
export IDF_PATH="$HOME/esp/ESP8266_RTOS_SDK"
```

### Build Commands
```bash
cd esp-gateway-project
source ~/esp/esp-env/bin/activate  # or use docker container
make -j$(nproc) CONFIG_SDK_PYTHON=python3
```

### Flash Commands
```bash
make flash PORT=/dev/ttyUSB0 CONFIG_SDK_PYTHON=python3
make monitor PORT=/dev/ttyUSB0
make flash monitor PORT=/dev/ttyUSB0 CONFIG_SDK_PYTHON=python3
```

### Clean Build
```bash
make clean  # removes entire build directory
```

## Docker Build Environment
- **Image**: `ghcr.io/kbittle/esp8266-build-env:latest` (built via `docker-publish.yml`)
- **Dockerfile**: `docker/Dockerfile` — Ubuntu 22.04 with toolchain, SDK, Python deps preinstalled
- **CI builds** use this container (`build.yml`)

## Key Hardware Pinout (from README)
| Component | ESP8266 GPIO | NodeMCU Pin | Function |
|-----------|-------------|-------------|----------|
| LoRa SCK  | GPIO 14     | D5          | SPI Clock |
| LoRa MISO | GPIO 12     | D6          | SPI MISO |
| LoRa MOSI | GPIO 13     | D7          | SPI MOSI |
| LoRa NSS  | GPIO 5      | D1          | Chip Select |
| LoRa RST  | GPIO 4      | D2          | Hardware Reset |
| SK9822 DATA | GPIO 2    | D4          | Serial Data |
| SK9822 CLK  | GPIO 16   | D0          | Serial Clock |

## Architecture Notes
- **Entry point**: `main.c` → `app_main()` creates 3 FreeRTOS tasks:
  - `net_init` (priority 5): WiFi AP+STA + HTTP webserver
  - `led_task` (priority 4): SK9822 LED status patterns
  - `lora_task` (priority 3): LoRa TX/RX loop
- **External driver**: SX126x driver in `SWSD003/sx126x/` (included via CMakeLists.txt)
- **Config storage**: NVS (Non-Volatile Storage) for LoRa/LED settings
- **Web UI**: Embedded `index.html` served by HTTP server

## CI/CD
- **build.yml**: Runs on push/PR to main, uses docker container, uploads `.bin` artifacts
- **docker-publish.yml**: Manual trigger to publish build-env image to GHCR

## WSL USB Flashing
```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```
Detach: `usbipd detach --busid <BUSID>`

## Flash Memory Layout
| Offset | File | Description |
|--------|------|-------------|
| 0x00000 | bootloader.bin | First-stage bootloader |
| 0x08000 | partition-table.bin | Partition map |
| 0x10000 | esp-gateway-project-<hash>.bin | Main application |

`make flash` handles all three automatically via esptool.py.