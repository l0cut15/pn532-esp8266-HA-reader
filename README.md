# PN532 NFC Reader — ESPHome

ESPHome firmware for a **Wemos D1 Mini** (ESP8266) with a PN532 NFC module. Scanned tags deliver `tag_scanned` events to Home Assistant via the native ESPHome API — tags appear in **Settings → Tags** and trigger automations automatically, identical to the iOS/Android companion apps.

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | Wemos D1 Mini (ESP8266) |
| NFC module | PN532 in I2C mode |
| Status indicator | WS2812 NeoPixel |

### Wiring

| PN532 | D1 Mini |
|-------|---------|
| SDA | D2 (GPIO4) |
| SCL | D1 (GPIO5) |
| VCC | 3.3V |
| GND | GND |

Set PN532 DIP switches: **switch 1 ON, switch 2 OFF** (I2C mode).

| NeoPixel | D1 Mini |
|----------|---------|
| DIN | D8 (GPIO15) |
| VCC | 5V |
| GND | GND |

## Setup

```bash
cp secrets.yaml.template secrets.yaml
```

Edit `secrets.yaml` with your values:

```yaml
wifi_ssid: "YOUR_WIFI_SSID"
wifi_password: "YOUR_WIFI_PASSWORD"
ap_fallback_password: "YOUR_FALLBACK_PASSWORD"
ota_password: "YOUR_OTA_PASSWORD"
api_encryption_key: ""
```

Generate the API encryption key:

```bash
python3 -c "import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

## Flashing

```bash
esphome run nfc-reader.yaml     # compile + flash (USB on first run, OTA after)
esphome logs nfc-reader.yaml    # stream live logs
```

## LED Status

| Colour | Meaning |
|--------|---------|
| White (dim, solid) | Connected and ready |
| Green (1s flash) | Tag scanned — UUID sent to HA |
| Red (1s flash) | Tag detected but no valid HA NDEF — re-scan |
| Off | Booting or not connected to HA |

## Tags

Tags must contain an HA-format NDEF URI. Write them using the **HA companion app** (Settings → Tags → Add Tag, then tap Write):

```
https://www.home-assistant.io/tag/<uuid>
```

The reader extracts the UUID from the NDEF URI and delivers it to HA as the `tag_id`. Blank tags and non-HA NDEF content are detected but ignored (red flash, no HA event fired).

You can also write new HA-format tags directly from the device page in HA using the **Write Tag Random** button — no phone required.

## Home Assistant

After flashing, add the device in HA: **Settings → Devices & Services → Add Integration → ESPHome**.

The device provides:

- Native `tag_scanned` events (Settings → Tags, automations, `tag.*` entities)
- `esphome.tag_removed` events
- **Write Tag Random** — writes a fresh HA UUID to a blank tag
- **Clean Tag** / **Cancel Writing** buttons
- Diagnostic sensors: status, IP address, connected SSID, ESPHome version

### Automations

**Settings → Automations → New Automation → Tag trigger** → select your tag. The automation fires on every successful scan.

## Technical Notes

- PN532 communicates over I2C at 100kHz for reliable MIFARE Ultralight NDEF reads
- WiFi requires WPA2 minimum (`fast_connect` + `min_auth_mode: WPA2`)
- `homeassistant.tag_scanned` calls `tag.async_scan_tag()` internally — the same code path used by the official companion apps

## License

MIT
