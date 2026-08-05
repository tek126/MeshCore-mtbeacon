# How the beacon hooks into `simple_repeater`

All of the code is behind `#ifdef WITH_MT_BEACON`. Thus a standard repeater build
(without the flag) compiles the guards to nothing and is byte-identical to upstream.
The add-on is header-only (`MtBeaconControl.h` + `MeshtasticBeacon.h` /
`MeshtasticProto.h`). The repeater has one `MtBeaconControl` instance and calls into
it at six points.

## Hook points

### 1. Member + accessor — `MyMesh.h`

```cpp
#ifdef WITH_MT_BEACON
#include "MtBeaconControl.h"   // via -I examples/meshtastic_beacon
...
  MtBeaconControl _beacon;     // private member
...
  MtBeaconControl* getBeacon() { return &_beacon; }   // for the UI task
#endif
```

`FIRMWARE_VERSION` also gets a `+mtbeacon` suffix, so `ver` shows the build that is
flashed.

### 2. Identity + persisted config — `MyMesh::begin()`

The Meshtastic node number and the start packet id come from the public key of the
repeater. Thus they are stable across restarts, with no extra state:

```cpp
const uint8_t* pk = self_id.pub_key;
uint32_t node = pk[0..3];   // big-endian
uint32_t seed = pk[4..7];
_beacon.begin(_fs, node, seed);   // loads /mtbeacon from the filesystem
```

### 3. CLI verbs — `MyMesh::handleCommand()`

The firmware tries `_beacon.handleCommand(command, reply, _fs)` before the common
CLI. It claims only `mtbeacon ...` and returns false for all else, so the existing
commands do not change. It operates over serial and admin remote-CLI sessions.

### 4. The beacon tick — tail of `MyMesh::loop()`

The repeater builds a `Context` (the node name, the configured lat/lon, the RTC
epoch, the flood-advert interval, and the CURRENT MeshCore radio parameters to
restore — including any active `tempradio` value) and calls:

```cpp
bool busy = hasPendingWork() || radio_driver.isReceiving()
         || !radio_driver.isInRecvMode();   // in-flight TX guard
_beacon.tick(radio_driver, radio, busy, ctx);
```

The `busy` guard is important. `hasPendingWork()` does not see a packet that the
firmware already moved into an in-flight transmit, and `isReceiving()` is false
during TX. `!isInRecvMode()` covers that time, so the beacon cannot change the radio
in the middle of a transmit. When busy, the beacon waits a few seconds and tries
again.

`tick()` does all the rest inside: the interval schedule with jitter,
listen-before-talk (bounded CAD — it never calls the RadioLib blocking
`scanChannel()`, whose IRQ wait has no timeout), the EU duty-cycle hold, the
change/transmit/restore cycle, and a `startRecv()` after the restore every time, so
a power-saving node cannot sleep with the radio in standby.

The transmit wait has the same bound (v0.2.4). `radioSendChecked()` normally ends on
the TxDone interrupt, but it limits the wait to the packet estimated airtime x2 +
500 ms. After that, a transmit that started has finished. Thus a lost interrupt does
not stop the loop for seconds.

When the interrupt is lost, the firmware then reads the chip TxDone flag with the
RadioLib family-independent `checkIrq(RADIOLIB_IRQ_TX_DONE)` (v0.2.5). It returns a
`TxOutcome` that says which happened:

| outcome | meaning |
|---|---|
| `TX_IRQ` | normal — the interrupt arrived |
| `TX_HW_CONFIRMED` | interrupt missed, chip confirms it transmitted |
| `TX_HW_FAILED` | interrupt missed, chip says it never transmitted |
| `TX_PRESUMED` | interrupt missed, chip can't report — airtime bound only |
| `TX_NOT_STARTED` | `startSendRaw()` refused |

Use `txDelivered(r)` for the "did it go out" question and `txUsedFallback(r)` for
the ISR-health question. The register read must be before `onSendFinished()`; its
`finishTransmit()` clears the IRQ flags.

These three (`TxOutcome`, `txDelivered`, `txUsedFallback`) are in
`MeshtasticProto.h`, not `MeshtasticBeacon.h`. Thus you can test the code below on a
host.

### 4b. Transmit health and self-repair (v0.2.6)

Give each outcome to `txStatsRecord(stats, r, millis())`. It updates a `TxStats` and
returns the current **failure streak**. When that reaches `MT_BEACON_FAIL_STREAK`
(default 3, change it with `-D`), call `radioRecover()` *instead of*
`radioRestoreMeshCore()` at the end of the burst:

```cpp
meshtastic::TxOutcome r = meshtastic::radioSendChecked(driver, radio, pkt, len);
if (meshtastic::txStatsRecord(tx, r, millis()) >= MT_BEACON_FAIL_STREAK)
  wedged = true;
...
if (wedged) {
  tx.recoveries++;
  tx.fail_streak = 0;                       // give the re-init a clean run
  meshtastic::radioRecover(driver, radio, /* same home_* args as restore */);
} else {
  meshtastic::radioRestoreMeshCore(driver, radio, /* ... */);
}
```

`radioRecover()` is `radioRestoreMeshCore()` plus, first: `standby()`,
`clearIrqFlags(0xFFFFFFFF)`, and `driver.begin()`. That last call is the point of
the function — `RadioLibWrapper::begin()` registers the TxDone interrupt action
again, which is the item most likely lost during a radio change. It does not do a
full chip reset (that needs per-family `begin()` parameters that the wrapper does
not expose). Side effect: `begin()` restarts the noise-floor calibration.

`formatTxStats(out, cap, stats, tag, millis() - stats.last_fail_ms)` makes the
one-line CLI summary. The counters nest: `irq_missed` counts each transmit with no
interrupt, and `hw_failed` is the part that the chip confirmed did not go out.

### 4c. Telemetry — battery and uptime (v0.2.6)

`buildTelemetryPayload(out, batt_mv, uptime_s, epoch)` encodes a Meshtastic
`Telemetry{DeviceMetrics}` for `PORT_TELEMETRY` (67). Give the two new `Context`
fields from the repeater:

```cpp
ctx.batt_millivolts = board.getBattMilliVolts();   // 0 if no battery sense
ctx.uptime_secs     = (uint32_t)(uptime_millis / 1000);
```

It returns 0 when there is nothing to report. Thus the caller does not send the
packet, and does not broadcast an empty submessage. `Telemetry.time` is `fixed32`,
the same nanopb trap as `Position.time`.

### 5. Flood-advert events — the chat text pacing (v0.2.3)

Both places where the repeater sends a MeshCore flood advert notify the beacon, so
the chat text goes out on a beacon burst near 15 s later:

```cpp
// periodic timer block in MyMesh::loop()
updateFloodAdvertTimer();
_beacon.onFloodAdvert();

// and in sendSelfAdvertisement(delay, flood) when flood == true (CLI 'advert')
_beacon.onFloodAdvert();
```

This paces the text to the real advert schedule of the repeater, not a boot-reset
uptime timer. `onFloodAdvert()` arms `pending_text` and moves the next burst close.
The single pending flag still removes duplicate deliveries.

### 6. UI line — `UITask`

`ui_task.setBeacon(the_mesh.getBeacon())` (in `main.cpp` setup). The task calls
`_beacon->uiLine()` to show `Beacon ON LongFast` / `Beacon off` on the OLED home
screen.

## Shared-radio requirements

The beacon drives the raw RadioLib object directly for the off-band burst. Thus
`RadioLibWrapper::startRecv()` is public (it arms the hardware RX again and syncs the
cached wrapper state every time — `recvRaw()` is not a substitute, because the
wrapper state can be old after the raw radio change).

## Adding a board

Copy the `*_repeater_mtbeacon` env shape onto the repeater env of the board:

```ini
[env:<board>_repeater_mtbeacon]
extends = env:<board>_repeater
build_flags = ${env:<board>_repeater.build_flags}
  -D WITH_MT_BEACON
  -D MT_HW_MODEL=<n>        ; Meshtastic HardwareModel (255 = PRIVATE_HW)
  -I examples/meshtastic_beacon
lib_deps = ${env:<board>_repeater.lib_deps}
```

You need to override `MESHCORE_SYNC_WORD` only if the MeshCore sync word of a board
is not the default `0x12`. The radio helpers are templated on the concrete radio
class, so SX1262 / SX1276 / LR1110 all bind unchanged.
