# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESPHome firmware for a Wemos D1 Mini (ESP8266) + PN532 NFC module, wired via I2C. Scanned Home Assistant tags fire `tag_scanned` events through the native ESPHome API. They appear in Settings → Tags, and `tag.<id>` entities are created automatically.

The firmware uses a **local fork of the ESPHome PN532 driver** (`components/`) that adds read retries, removal hysteresis, diagnostics and a bench mode. See `components/README.md`.

## Hardware

| Component | Connection |
|-----------|-----------|
| Wemos D1 Mini | USB to the Mac at `/dev/cu.usbserial-510` (CH340) for serial logs |
| PN532 module | Red "PN532 NFC RFID Module V3", I2C mode (DIP: switch 1 ON, switch 2 OFF) |
| PN532 SDA / SCL | D2 (GPIO4) / D1 (GPIO5) |
| PN532 VCC | **5V** (measured 4.68 V; the module has its own regulator) |
| WS2812 status LED | D8 |

Enclosure: 3D-printed PLA, 3 mm cover, about 4 mm air gap above the PN532. **Known issue:** this PN532 module has a narrow sweet spot and dead zones, even with a card-sized tag on the bare antenna. That hardware problem is the main cause of unreliable reads (see `components/README.md`).

## Build / Flash / Logs

The HA server can't see `components/` when building from a local path. Day-to-day iteration therefore builds on this Mac:

```bash
# one-time: Python 3.12+ venv with esphome==2026.10.1 (match the HA server version)
uv venv -p 3.13 .venv && uv pip install -p .venv/bin/python esphome==2026.10.1 pyserial
.venv/bin/esphome compile nfc-reader.yaml
.venv/bin/esphome upload nfc-reader.yaml --device 10.10.11.198     # OTA
.venv/bin/esphome -s bench_mode true run nfc-reader.yaml --device 10.10.11.198   # fixed-tag bench build
```

`secrets.yaml` is required and gitignored. Copy it from the HA server's `secrets.yaml` (MCP `get_config`), or from `secrets.yaml.template`.

**Serial logs:** the logger runs on UART0 at 115200 baud. Open `/dev/cu.usbserial-510` with DTR and RTS deasserted, and make sure nothing else (e.g. a VS Code PlatformIO monitor) holds the port. On macOS, opening the port resets the board once.

**The PN532 keeps its RF configuration across ESP reboots and OTA updates.** After experimenting with `rx_gain`, unplug USB to power-cycle it.

## Source of Truth

The ESPHome Device Builder on Home Assistant (`hoas.local.zeroart.net`, config `nfc-reader.yaml`) holds the deployed config. The server copy pulls the driver fork from GitHub (`external_components: github://l0cut15/pn532-esp8266-HA-reader@<sha>`). The local copy uses `type: local` so iteration works without pushing. Otherwise the two should match. After changing either, mirror the change to the other and keep the pinned SHA current.

## Architecture

```
nfc-reader.yaml          — ESPHome firmware (D1 Mini + PN532 I2C), diagnostics, tag-writing buttons
components/pn532/        — forked PN532 core (polling, presence state machine, retries, bench mode)
components/pn532_i2c/    — forked I2C transport (desync warnings)
secrets.yaml.template    — template for secrets.yaml (not committed)
```

**Data flow:**
PN532 detects an ISO14443A tag → driver reads the NDEF data, re-selecting and retrying on RF errors → YAML lambda extracts the UUID from the `https://www.home-assistant.io/tag/<uuid>` URI record → `homeassistant.tag_scanned`.

Tags without a Home Assistant NDEF record, and reads that fail on every retry, are **not** sent to HA. They are logged ("Tag ignored") and the LED flashes red. A good scan flashes green.

**Diagnostics:** every event logs an `NFCSTAT` line, and the driver logs a `stats:` line every 10 s. HA diagnostic sensors: Tag Events, HA Tag Scans, Unreadable Reads, Tag Removals, Last Tag Event.

## HA Integration

The device exposes:
- `tag_scanned` events, and `esphome.tag_removed` events
- Write Tag Random / Clean Tag / Cancel Writing / Restart buttons
- Status, IP Address, ESPHome Version and Connected SSID diagnostics, plus the counters above
