# Local PN532 driver fork

`pn532/` and `pn532_i2c/` are copies of the ESPHome **2026.10.1** components. They override the built-in ones via
`external_components` in `nfc-reader.yaml`. The YAML interface is unchanged (`on_tag`, `on_tag_removed`,
write/clean/read modes, `pn532.is_writing`), and every new option defaults to upstream behaviour.

## Added options (`pn532_i2c:`)

| Option | Default | Effect |
|---|---|---|
| `removal_misses` | 1 | Consecutive empty polls needed before `on_tag_removed` fires. Prevents duplicate scans when a single poll glitches. |
| `read_attempts` | 1 | Number of polls on which a held tag is re-read after a failed read. The UID is marked as handled only once a read succeeds. If the tag leaves before any read succeeds, `on_tag` still fires, without NDEF data, so the automation can signal "tap again". |
| `page_retries` | 0 | Within one poll: re-select the tag (InListPassiveTarget) and retry a failed Ultralight/NTAG READ. |
| `rx_gain` | unset | CIU_RFCfg RxGain (`18dB`…`48dB`). Not used in production; see notes. |
| `activation_retries` | 255 (chip default) | MxRtyPassiveActivation. Low values made the tag undetectable on this hardware. |
| `bench_mode` | false | Fixed-tag bench: re-read on every poll and log `BENCH … clean/recovered/failed`. No triggers fire. |

## Other changes vs upstream
- `turn_off_rf_()` consumes the RFConfiguration response instead of leaving it queued.
- `write_command_()` drains any stale frame before sending a command and logs a warning (`stale_frames` stat). This is a safeguard; no stale frames have been observed so far.
- Mismatched or invalid response frames and bad ACKs are logged at WARNING instead of VERBOSE.
- The RF field is turned off on every poll path, including the same-UID path.
- A `stats:` line is logged every 10 s: polls, empty, poll_errors, read_failures, page_retries, abandoned, stale_frames.

## Notes from the 2026-11-01 investigation
- The PN532 keeps RFConfiguration across ESP reboots and OTA, because it is not power-cycled. The driver now always writes MaxRetries at setup. After testing `rx_gain`, power-cycle the board to get back to chip defaults.
- Root cause of the unreliable reads was **geometry, not the module**: a credit-card tag held close to the antenna is
  overcoupled. Both coils detune, and longer replies fail with CRC/RF-protocol errors (status 0x02/0x0B); at times
  even detection fails. Flat on the bare antenna gave 0 detections in 60 s. At about 5–6 cm the same card reads
  reliably, which is within the red V3 module's 5–7 cm spec. The original enclosure (3 mm PLA + 4 mm gap, so
  ~7 mm) puts card taps in the too-close zone. Small stickers and fobs couple more weakly and will want a shorter
  distance.
- Bench workflow: `esphome -s bench_mode true run nfc-reader.yaml`, tape the tag in place, then read the `BENCH` lines from the serial log.
