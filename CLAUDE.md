# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

An ESPHome firmware for a Wemos D1 Mini (ESP8266) + PN532 NFC module, wired via I2C. Scanned tags fire `tag_scanned` events in Home Assistant via the native ESPHome API integration — tags appear in Settings → Tags and `tag.<id>` entities are created automatically.

The Python implementation (Raspberry Pi + USB serial) is preserved in this repo as legacy reference but is no longer the primary target.

## Hardware

| Component | Connection |
|-----------|-----------|
| Wemos D1 Mini | — |
| PN532 module | I2C mode (DIP: switch 1 ON, switch 2 OFF) |
| PN532 SDA | D2 (GPIO4) |
| PN532 SCL | D1 (GPIO5) |
| PN532 VCC | 3.3V |
| PN532 GND | GND |

## Setup

```bash
cp secrets.yaml.template secrets.yaml
# Edit secrets.yaml: set wifi_ssid, wifi_password, ota_password, api_encryption_key
# Generate api_encryption_key:
#   python3 -c "import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

## ESPHome Commands

```bash
esphome run nfc-reader.yaml        # compile + OTA flash (or serial on first flash)
esphome logs nfc-reader.yaml       # stream device logs
esphome compile nfc-reader.yaml    # compile only (check for errors)
```

`secrets.yaml` is required and not committed — copy from `secrets.yaml.template`.

## Architecture

```
nfc-reader.yaml          — Primary ESPHome firmware (D1 Mini + PN532 I2C)
secrets.yaml.template    — Template for secrets.yaml (not committed)
```

**Data flow:**
PN532 detects ISO14443A card → ESPHome reads NDEF → extracts UUID from `home-assistant.io/tag/` URL → `homeassistant.tag_scanned` action over native API → HA Settings → Tags updated.

For tags without HA NDEF, the raw UID is sent so the tag still registers in HA.

**Key design decisions:**
- ESPHome native API replaces the Python WebSocket + mobile_app webhook path; no token or registration file needed
- HA NDEF UUID is extracted in C++ lambda (same `home-assistant.io/tag/<uuid>` pattern as the Python code)
- Raw UID fallback means blank/unformatted tags still fire events (unlike the Python v3 which silently dropped them)
- `status_led` on D4 (built-in, active-LOW) shows WiFi/API connectivity without any extra hardware
- "Write Tag Random" button on the HA device page lets you provision blank cards without a separate tool

## HA Integration

After flashing, add the device in HA via Settings → Devices & Services → ESPHome. The device exposes:
- `tag_scanned` events (native, no extra config)
- `esphome.tag_removed` events
- Write Tag Random / Clean Tag / Cancel Writing buttons
- Status, IP Address, ESPHome Version, Connected SSID diagnostic sensors

---

## Legacy Python Implementation

The Python files remain as reference. They implement the same HA tag scanning via WebSocket + mobile_app webhook on a Raspberry Pi with a USB-serial PN532.

```
nfc_config.py           — Config loader (reads env vars) + PN532 auto-detection
nfc_reader_ha_events.py — NFCReaderHA class: raw HSU serial, NDEF parsing
nfc_reader_service.py   — Async systemd daemon
ha_websocket.py         — HAWebSocketClient: auth, heartbeat, exponential backoff reconnect
ha_registration.py      — DeviceRegistrar: mobile_app registration
ha_tag_scanner.py       — TagScanner: webhook dispatch, offline queue
tests/                  — Pytest unit + integration tests
```

Configuration via `.env` (copy from `.env.template`).
