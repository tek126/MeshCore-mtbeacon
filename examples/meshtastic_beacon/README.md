# meshtastic_beacon (prototype)

Make a MeshCore repeater periodically announce itself **on a Meshtastic network**,
so Meshtastic users see a text line like `MeshCore repeater in range` on their
default public channel.

## Why a native advert won't work

A MeshCore advert and a Meshtastic packet share nothing above the raw radio:

| Layer | MeshCore | Meshtastic LongFast |
| --- | --- | --- |
| Frequency | region default (e.g. 869.618 MHz) | region slot (e.g. 906.875 MHz US) |
| Bandwidth / SF / CR | 62.5 kHz / 8 / 4-5 | 250 kHz / 11 / 4-5 |
| **Sync word** | **0x12 (private)** | **0x2B (public)** |
| Framing | pubkey + timestamp + signature | 16-byte header + AES-CTR protobuf |

The **sync word** is different, so the radios do not hear each other, even on the
same frequency. And Meshtastic removes any packet that does not have its own
framing. Thus, to be seen, the node must *become* a Meshtastic transmitter for one
packet.

## What this prototype does

`MeshtasticBeacon.h` is self-contained and works with any chip:

1. `buildTextPacket()` — it assembles a Meshtastic broadcast text packet: the
   16-byte header, then an AES-CTR-encrypted `Data{portnum=TEXT_MESSAGE_APP,
   payload="..."}` protobuf, with the channel PSK as the key. It uses the rweather
   `Crypto` library (`CTR<AES128>`) that MeshCore already includes.
2. `channelHash()` — the Meshtastic XOR channel hash (the default channel is
   `0x08`).
3. `transmitOnce()` — it changes the radio to the Meshtastic modem preset and sync
   word with the MeshCore `RadioLibWrapper`, transmits, then changes the radio back
   to the MeshCore PHY. Thus the repeater continues normally.

`main.cpp` is a small demo: it starts the board and the radio, then beacons each
5 minutes.

## The single-radio constraint

LoRa uses one channel at a time. While `transmitOnce()` operates (a few hundred ms
at SF11), the radio is **off the MeshCore channel**, and the node does not receive
MeshCore traffic in that time. This is the main trade-off — keep the interval long
(minutes). For a real repeater, call `sendBeacon()` from the idle path of
`simple_repeater`; do not use this small loop.

## Status

**Verified on-air.** The text packet is confirmed against a live Meshtastic node —
it decodes and shows on the default channel. The interop-critical math (the channel
hash `0x08`, the region/preset frequency calculation, the AES-CTR nonce, and the
NodeInfo/Position protobuf encoders) is pinned by host unit tests in `test/` (run
`test/run.sh`). The multi-block AES-CTR counter is confirmed too: the default text
is more than 16 bytes and decodes correctly.

Confirm these on your own bench: that the **NodeInfo/Position** packets show as a
named pin on the Meshtastic map (the bytes are correct, but no person has looked at
the map display), and that you set the correct `region` and `preset` for your area.

## Building it

The beacon envs are in the variant `platformio.ini` files. Verified builds:

```
pio run -e t1000e_meshtastic_beacon         # Seeed T1000-E  (LR1110)
pio run -e Heltec_t114_meshtastic_beacon    # Heltec T114    (SX1262)
pio run -e Xiao_nrf52_meshtastic_beacon     # Seeed XIAO nRF52840 (SX1262)
```

Each env is the same shape — extend the base block of the board and add this folder
to the source filter:

```ini
[env:<board>_meshtastic_beacon]
extends = <board base>
build_flags = ${<board base>.build_flags}
  -I examples/meshtastic_beacon
build_src_filter = ${<board base>.build_src_filter}
  +<../examples/meshtastic_beacon>
```

The radio helpers are templated on the concrete radio. Thus the same code operates
with `CustomLR1110` (T1000-E) or `CustomSX1262` (T114 / XIAO) with no change. On the
repeater build, the region, preset, and frequency are runtime `mtbeacon` settings —
no per-board source changes are necessary (only `MESHCORE_SYNC_WORD`, if the
MeshCore sync word of a board is not the default `0x12`).

## Runtime control via the MeshCore CLI (repeater integration)

`MtBeaconControl.h` adds a persisted, CLI-configurable beacon to a real repeater
(not the demo). It goes into `simple_repeater` with `-D WITH_MT_BEACON`. Thus
standard repeater builds do not change (the guards compile to nothing). Build the
integrated firmware with:

```
pio run -e Heltec_t114_without_display_repeater_mtbeacon
```

Then, over the serial port or an admin remote-CLI session, use the `mtbeacon`
commands:

| Command | Function |
| --- | --- |
| `mtbeacon` / `mtbeacon status` | Show the configuration and the derived node id. |
| `mtbeacon stats` / `mtbeacon stats clear` | Show the transmit health from boot / Set the counters to zero. |
| `mtbeacon help` | Show all commands (the detailed list prints to serial). |
| `mtbeacon on` / `mtbeacon off` | Set the periodic beacon to on / off. |
| `mtbeacon send` | Transmit one beacon now, with the text (it operates even when off). |
| `mtbeacon interval <min>` | The presence interval (1–1440 min; default 30). |
| `mtbeacon preset <name>` | The modem preset: LongFast, LongMod, LongSlow, MediumFast, MediumSlow, ShortFast, ShortSlow, ShortTurbo. |
| `mtbeacon region <name>` | The region and band: US, EU_868, EU_433, ANZ, CN, JP, KR, TW, RU, IN, NZ_865, TH, UA_433, UA_868 (alias `country`). |
| `mtbeacon freq <MHz\|auto>` | Set the frequency manually. `auto` calculates it again from the region and preset. |
| `mtbeacon power <dBm>` | The TX power (−9 to 22). The region limit applies. |
| `mtbeacon hops <0-3>` | The Meshtastic hop limit for the presence and text (**default 0**). |
| `mtbeacon text <string>` | The text (63 characters maximum). |
| `mtbeacon text.mult <N>` | The text schedule. The text goes with each flood advert, and N−1 more times each period (0 = never). |
| `mtbeacon short <str\|auto>` | The Meshtastic short name / map label (4 characters maximum). `auto` = `MC` and 2 hex characters. |
| `mtbeacon nodeinfo on/off` | Also send a NodeInfo (the node name). Default on. |
| `mtbeacon position on/off` | Also send a Position (the map point). Default on. |
| `mtbeacon telemetry on/off` | Also send Telemetry (battery and run time). Default on. |
| `mtbeacon presets` / `mtbeacon regions` | Show the available preset / region names. |

**Channel block list (v0.2.7).** This is separate from the beacon. You can tell the
repeater not to forward specific `#hashtag` channels — by name, no key necessary:

| Command | Function |
| --- | --- |
| `block` / `block list` | Show the blocked channels and their hash byte. |
| `block #dispatches` | Stop the repeat of that `#hashtag` channel (up to 8). |
| `unblock #dispatches` | Start the repeat of it again. |

MeshCore `#hashtag` channels calculate their key from the name
(`SHA-256("#name")[:16]`). Thus the firmware calculates the on-air channel hash.
Two limits come from the wire format. First, it operates for `#`-name channels only
(the firmware cannot calculate the key of a private channel with a random key).
Second, the channel id on the air is one byte, so a block sometimes also stops a
different channel with the same byte (near 1 in 256; the hash in the list makes a
collision easy to see). Adverts, DMs, and ACKs do not change.

**It shows as a node on the Meshtastic map.** The periodic beacon is a silent
*presence*: a **NodeInfo** (so the repeater shows as a named node — the long name
`MC <repeater name>`, the short name `MC` and 2 hex characters of the node id, for
example `MC7a`), a **Position** (a map point, from the repeater configured lat/lon;
the node does not send it if unset), and a **Telemetry** packet (battery and run
time). At slow presets, these go out one per cycle, in turn, to limit the
off-channel time. Only the enabled ones take part, so when you turn one off, the
rest of the cycle is faster. Turn them on or off with `nodeinfo` / `position` /
`telemetry`.

**Battery and run time (v0.2.6).** The Telemetry packet includes Meshtastic
`DeviceMetrics`. Phone clients show this as the node battery ring and run time.
Thus a solar or battery repeater shows its charge on another person's app, with no
extra hardware. The percentage comes from the raw millivolt reading of the board
across the usual 1S Li-ion range (3.3 V empty → 4.2 V full). A board with no battery
sense reports nothing, not a wrong 0%. `channel_utilization` and `air_util_tx` are
**empty** on purpose: the airtime figures are for the MeshCore channel that this
node repeats on, not the Meshtastic one. To publish them would put a wrong load
number on another person's network map.

The **short name is the label that Meshtastic draws on the map marker**. This is why
it is not a flat `MC` — that would make every beacon the same pin. The 2 hex
characters give 256-way local difference, and it still shows as MeshCore. The long
name gives more detail on tap. An operator with a callsign or a site code can set
their own with `mtbeacon short <str>` (4 characters maximum), or go back to the
derived one with `mtbeacon short auto`.

**Chat text.** The `text` message is less frequent than the presence, and it is
**event-driven**. When the repeater sends a MeshCore advert — the periodic flood
advert or a CLI `advert` — the text goes out on a beacon burst a few seconds later.
Thus the chat message follows the real advert schedule of the repeater (default
47 h), and the `advert` command is also a good test. `text.mult > 1` adds more texts
between adverts (N per advert period). `text.mult 0` stops the text and leaves a
silent presence-only beacon. `mtbeacon status` shows the schedule, for example
`txt1x~47h(due 13h)`, or `(due now)` when a text is ready for the next burst.
`txt1x(noadv)` means the flood advert of the repeater is off, which stops the timer
schedule (a manual `advert` still sends the text).

**The region and preset set the frequency.** When you set `region` or `preset`, the
firmware calculates the exact Meshtastic default-channel frequency, the same way
Meshtastic firmware does (the preset bandwidth sets how many channel slots fit in
the region band; the channel name hashes to a slot — for example US LongFast →
906.875, EU_868 LongFast → 869.525). `mtbeacon freq` is for a manual value only; a
`*` in `status` shows that a manual value is active.

**Region power limit.** Each region has a legal TX-power limit (for example EU_433
12 dBm, JP 16 dBm). The firmware limits the configured power to it, and shows
`(cap)` in `status`.

**Hop limit.** The Meshtastic packets of the beacon go out at **0 hops by default**.
Thus direct neighbours receive them, and other nodes do **not** repeat them across
the Meshtastic network — a small footprint by default for a foreign beacon. Increase
it with `mtbeacon hops <0-3>` (3 maximum) if you want some relay. `status` shows it
as `h<N>`.

The settings are kept in `/mtbeacon` in the internal filesystem (separate from
`NodePrefs`; the core prefs do not change). The defaults are US LongFast, 30-minute
presence, text one time per flood advert, and off until you run `mtbeacon on`. The
node number comes from the repeater identity, so it is stable across restarts.

The beacon transmits only when the mesh is idle (`!hasPendingWork() && !receiving`
and not mid-transmit); if not, it waits a few seconds. Thus it never changes the
radio in the middle of a transaction. After the beacon, it restores the *current*
radio parameters of the repeater (including any active `tempradio` value) and enters
receive again immediately, so power-saving nodes do not sleep deaf. To add it to
another board, copy the `*_repeater_mtbeacon` env shape: extend the repeater env of
that board and add `-D WITH_MT_BEACON -I examples/meshtastic_beacon`.

## Reliability

Each burst changes the radio to the Meshtastic PHY and back. The radio normally
signals the end of a transmit with the **TxDone interrupt** — but that edge can be
lost during a radio change, even when the packet went out correctly. The firmware
does not wait on a fixed multi-second timeout (which stops the whole main loop while
the node looks dead). Instead, the **estimated airtime** of the packet limits the
send: after that time, a transmit that started has finished. Normal sends do not
change — the interrupt arrives first and the wait ends immediately.

When the interrupt does *not* arrive, the beacon **asks the radio chip directly** if
it transmitted (v0.2.5). The MeshCore `isSendComplete()` reads only a flag that an
ISR sets. Thus, from there, "sent, interrupt lost" and "never transmitted" are the
same — v0.2.4 assumed the first, so it could report a real failure as a success.
RadioLib maps the TxDone bit of each family onto one generic flag, so one register
read gives the answer on SX126x, SX127x, LR11x0, and STM32WLx. The read is *before*
`finishTransmit()`, which clears the flags.

The firmware reports the two conditions separately now, so a bad ISR and a real
transmit failure do not look the same:

```
beacon: TxDone IRQ missed on 1/2 packet(s) - checked the chip instead
beacon: 1/2 packet(s) did NOT transmit (chip reports no TxDone)
```

A burst reports a failure only when nothing left the antenna. The caller then sends
it again, and does not record a delivery that did not happen.

### Self-repair and `mtbeacon stats` (v0.2.6)

To know that a transmit is dead is useful only if the firmware acts on it. After
**3 dead transmits together** (`MT_BEACON_FAIL_STREAK`, change it with `-D`), the
beacon **starts the radio again**, not only restores it: standby, clear any latched
IRQ flags, arm the driver again — this registers the TxDone interrupt action again,
the item most likely lost during a radio change — then program the MeshCore PHY
again and enter receive. Before this, a stopped modem stayed stopped until a person
power-cycled the node.

It does not do a full chip reset. That needs per-family `begin()` parameters that
the wrapper does not expose, and a half-reset radio is worse than the stopped state.
A recovery restarts the noise-floor calibration, so the node learns the interference
threshold again over the next few seconds.

```
beacon: 3 dead transmits in a row - reinitialising the radio
```

You can read the counters at any time. Thus a drive test or a site visit gives data,
not serial scrollback:

```
mtbeacon stats
> beacon tx 61/64 ok | dead 3 irq-miss 7 nostart 0 | burst 22 lbt 1 | recov 1 (last fail 12m ago)
```

Read `dead` and `irq-miss` as **nested, not separate**: `irq-miss` counts each
transmit with no TxDone interrupt, and `dead` is the part that the chip then
confirmed did not go out. A high `irq-miss` with `dead 0` is the good case — the
interrupt path is unreliable, but the packets transmit. This is exactly the case
that v0.2.4 could not tell from a failure. `lbt` counts bursts that the node did not
send because listen-before-talk found the channel busy. `recov` counts the radio
re-inits. The counters are in RAM only — they are diagnostics, not worth a flash
write per packet, and a restart is the correct time to reset them. `mtbeacon stats
clear` sets them to zero without a restart.

### nRF52 boards

nRF52 builds (RAK4631, ProMicro/Faketec, T114, T1000-E, XIAO nRF52, and others)
start the **hardware watchdog** (90 s, fed each loop pass). If the node hangs, it
restarts itself; it does not stay dead until a person power-cycles it. DFU updates
are not affected (the UF2 bootloader feeds a running watchdog).

Each boot prints its **reset reason** to serial, and you can query it remotely on
any nRF52 board:

```
get pwrmgt.bootreason
> Reset: Watchdog
```

`Watchdog` there means the node hung and recovered itself — send a bug report.

## Scope / etiquette

- **One way only.** The node shows as a named node and a map point, and it sends
  text. It does not route or repeat Meshtastic traffic.
- **Shared spectrum.** You add airtime to another person's public channel. The
  beacons use listen-before-talk, interval jitter, and an EU duty-cycle hold. But
  keep the interval long, obey the limits of your region, and do not beacon onto
  channels that you do not operate. Private channels need the operator PSK.
