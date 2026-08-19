# MeshCore-mtbeacon

A fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) that lets a
MeshCore node show up **on a Meshtastic network** without being a Meshtastic
node: it periodically retunes to the Meshtastic channel, emits real Meshtastic
packets, then returns to its MeshCore channel. Like upstream MeshCore, both
node roles are covered, on one branch:

- **Repeater beacon** (`-D WITH_MT_BEACON`) — a fixed repeater appears as a
  named node + map pin, with an occasional chat announcement.
- **Companion presence** (`-D WITH_MT_PRESENCE`) — a phone-paired BLE
  companion appears as a silent presence, configured entirely from the phone
  app; its position is deliberately fuzzed into an uncertainty circle.

> The beacon engine lives under
> [`examples/meshtastic_beacon/`](examples/meshtastic_beacon/); the companion
> presence adds a thin layer in
> [`examples/companion_radio/`](examples/companion_radio/MTPRESENCE.md).
> Everything else in this tree is unmodified MeshCore — see the
> [upstream project](https://github.com/meshcore-dev/MeshCore) for the base firmware.

## What it does

- **Silent presence** — sends Meshtastic *NodeInfo* + *Position*, so the repeater
  appears as a **named node and a pin on the Meshtastic map** (no chat spam).
- **Occasional announcement** — a chat text (`MeshCore repeater in range`)
  posted rarely: it rides a beacon burst right after each of the repeater's own
  MeshCore flood adverts (so `advert` doubles as a live test), never on a
  spammy timer.
- **Region/preset/slot aware** — computes the exact Meshtastic default-channel
  frequency the way Meshtastic firmware does, or takes an explicit
  **frequency slot** (`mtbeacon slot <N>`, 1-based as the Meshtastic app shows
  it) for meshes on a non-default slot; per-region TX-power caps; per-board
  `hw_model` so it reports the right hardware.
- **Position precision** — `mtbeacon precision <bits>` fuzzes the map position
  into a Meshtastic-native uncertainty circle (32 = exact pin, the repeater
  default).
- **Good citizen** — listen-before-talk, interval jitter, EU duty-cycle hold, and
  off-channel airtime bounding.
- **Runtime CLI** — `mtbeacon on|off|status|send|interval|text.mult|preset|region|…`
  over serial or an admin remote-CLI session. Nothing is hard-coded.
- **Battery on the map** — a Meshtastic telemetry packet carries battery
  percentage, voltage and uptime, so a solar or battery repeater shows its state
  of charge in any Meshtastic client.
- **Self-recovering** — a transmit whose TxDone interrupt goes missing across a
  retune falls back to an airtime bound instead of freezing the main loop, then
  reads the radio chip's own TxDone flag to say whether the packet actually went
  out. After a run of transmits the chip reports as dead, the radio
  **re-initialises itself** rather than staying wedged until a power cycle, and
  `mtbeacon stats` shows the running transmit health. On nRF52 a hardware
  watchdog reboots a hung node instead of leaving it dead until a power cycle,
  with `get pwrmgt.bootreason` reporting why the last reset happened.

It's integrated into `simple_repeater` behind `-D WITH_MT_BEACON`, so a stock
repeater build (without the flag) is byte-identical to upstream.

## Companion presence

The same engine, on the companion side: a BLE companion node appears in
Meshtastic node lists with a name, battery, and an optional **fuzzed**
position (default ≈ 2.9 km circle) — never chat text. A BLE companion has no
serial console, so everything is set from the phone app's
**custom-variables editor** with zero app changes: `mt.presence`,
`mt.interval`, `mt.position`, `mt.precision`, `mt.region`, `mt.preset`,
`mt.slot` — and on boards with a switchable LoRa front-end (e.g. the Heltec
V4's LNA), `radio.fem.rxgain` / `radio.fem.txgain`, the same settings the
repeater CLI exposes under those names.

```
pio run -e Heltec_t114_companion_radio_ble_mtpresence   # nRF52840
pio run -e heltec_v4_companion_radio_ble_mtpresence     # ESP32-S3
```

Full doc: [`examples/companion_radio/MTPRESENCE.md`](examples/companion_radio/MTPRESENCE.md).
Prebuilt V4 images: [kc2kvy.com/v4presence](https://kc2kvy.com/v4presence/).

## Firmware

Pre-built images for **63 boards** (nRF52840 / ESP32-S3 / ESP32-C3 / ESP32-C6 /
RP2040 / STM32WL, with SX1262 / SX1276 / LR1110 radios) are attached to the
[latest release](../../releases/latest). Flash the one for your board, then
configure it over the CLI (`mtbeacon help`).

Nearly every repeater-capable MeshCore board now has a `*_repeater_mtbeacon` env —
the release covers the whole fleet that builds cleanly from this tree.

## Build it yourself

Each board has a `*_repeater_mtbeacon` PlatformIO env, e.g.:

```
pio run -e Heltec_t114_repeater_mtbeacon     # nRF52 + SX1262
pio run -e RAK_4631_repeater_mtbeacon
pio run -e Heltec_v3_repeater_mtbeacon       # ESP32-S3
```

To add the beacon to another board: extend that board's repeater env with
`-D WITH_MT_BEACON -I examples/meshtastic_beacon` (and optionally
`-D MT_HW_MODEL=<n>` for the Meshtastic hardware id).

## Tests

Pure logic (frequency math, channel hash, protobuf encoders) has host unit tests
— no hardware needed:

```
examples/meshtastic_beacon/test/run.sh
```

CI runs these plus representative builds on every push.

## Documentation

- [`examples/meshtastic_beacon/README.md`](examples/meshtastic_beacon/README.md) — full reference (CLI, flashing, caveats)
- [`examples/meshtastic_beacon/INTEGRATION.md`](examples/meshtastic_beacon/INTEGRATION.md) — exactly how it hooks into `simple_repeater`
- [`examples/companion_radio/MTPRESENCE.md`](examples/companion_radio/MTPRESENCE.md) — the companion presence and its phone-app variables

## Related repos

This repo is the canonical home of the Meshtastic interop engine. Two sibling
projects build on it:

- [**MeshCore-Carpeater**](https://github.com/tek126/MeshCore-Carpeater) — the
  car node: a mobile repeater that beacons its parked GPS location onto both
  networks.
- [**MeshCore-dualmode**](https://github.com/tek126/MeshCore-dualmode) —
  Dual-Node: the car node and the presence companion in one firmware, switched
  by a 5× button press (T1000-E, Heltec V4, Heltec T114).

## License

MIT, same as upstream MeshCore (see [`license.txt`](license.txt)). Not affiliated
with the MeshCore or Meshtastic projects.

## Built with generative AI

The beacon add-on, its docs, and this fork's tooling were developed with the
assistance of generative AI (Anthropic's Claude), then reviewed and tested by a
human. Read the source, verify on-air behavior, and test before relying on it.
