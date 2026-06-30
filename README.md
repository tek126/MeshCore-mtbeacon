# MeshCore-mtbeacon

A fork of [MeshCore](https://github.com/meshcore-dev/MeshCore) that adds a
**Meshtastic beacon** to a MeshCore repeater: the repeater periodically retunes
to the Meshtastic channel, emits real Meshtastic packets, then returns to its
MeshCore channel — so it shows up **on a Meshtastic network** without being a
Meshtastic node.

> The whole add-on lives under [`examples/meshtastic_beacon/`](examples/meshtastic_beacon/).
> Everything else in this tree is unmodified MeshCore — see the
> [upstream project](https://github.com/meshcore-dev/MeshCore) for the base firmware.

## What it does

- **Silent presence** — sends Meshtastic *NodeInfo* + *Position*, so the repeater
  appears as a **named node and a pin on the Meshtastic map** (no chat spam).
- **Occasional announcement** — a chat text (`MeshCore repeater in range`) posted
  rarely, paced off the repeater's own MeshCore flood advert.
- **Region/preset aware** — computes the exact Meshtastic default-channel
  frequency the way Meshtastic firmware does; per-region TX-power caps; per-board
  `hw_model` so it reports the right hardware.
- **Good citizen** — listen-before-talk, interval jitter, EU duty-cycle hold, and
  off-channel airtime bounding.
- **Runtime CLI** — `mtbeacon on|off|status|send|interval|text.mult|preset|region|…`
  over serial or an admin remote-CLI session. Nothing is hard-coded.

It's integrated into `simple_repeater` behind `-D WITH_MT_BEACON`, so a stock
repeater build (without the flag) is byte-identical to upstream.

## Firmware

Pre-built images for 11 boards (nRF52 / RP2040 / ESP32-S3 / STM32WL) are attached
to the [latest release](../../releases/latest). Flash the one for your board, then
configure it over the CLI (`mtbeacon help`).

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

## License

MIT, same as upstream MeshCore (see [`license.txt`](license.txt)). Not affiliated
with the MeshCore or Meshtastic projects.

## Built with generative AI

The beacon add-on, its docs, and this fork's tooling were developed with the
assistance of generative AI (Anthropic's Claude), then reviewed and tested by a
human. Read the source, verify on-air behavior, and test before relying on it.
