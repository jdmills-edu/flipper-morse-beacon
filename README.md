# Morse Beacon — sub-GHz CW / MCW station ID for Flipper Zero

A Flipper FAP that keys Morse code out of the CC1101. Two jobs:

1. **Beacon ID** — key your identifier automatically: either after the first
   transmission that follows a stretch of dead air (repeater-style), or on a
   fixed interval regardless of the channel (fox hunt or plain beacon).
2. **Remote text** — send arbitrary text as Morse, typed on the Flipper, or
   pushed from a phone over BLE / a device wired to pins 13/14.

Built against the **Unleashed unlshd-092 SDK, API 88.4**, which is the exact API
version reported by the RogueMaster `RM0819-2255-b3dd8981` firmware on the
device (`Oagyak`).

## Build and install

```
pipx install ufbt
ufbt update --index-url=https://up.unleashedflip.com/directory.json --channel=release
cd ~/AI/flipper-morse
ufbt              # build -> dist/morse_beacon.fap
ufbt launch       # build, upload to /ext/apps/Sub-GHz/, and run
```

## How the signal is generated

This is the part that matters, because "Morse on sub-GHz" has two very different
meanings and only one of them is audible on a GMRS or ham FM radio.

**MCW (default).** The CC1101 is put in 2-FSK with the deviation set to ±2.38 kHz
(narrowband) or ±4.76 kHz (wideband), and the async-TX data line is square-waved
between the two tones at audio rate — 800 Hz by default. An FM discriminator
turns that back into an 800 Hz tone. During the spaces between dits and dahs the
line is held at one tone, so the carrier stays up and the audio simply goes
quiet — which is exactly what a hardware repeater's ID sounds like.

**OOK.** The carrier itself is keyed on and off. This is real CW, and it is what
you want if something is listening with an AM/CW detector — but on an FM
receiver it just opens and closes the squelch, with no tone.

The deviation values come from patching `DEVIATN` in a custom CC1101 preset
built at runtime from the stock 2-FSK register table (`0x04` → 2.38 kHz,
`0x14` → 4.76 kHz). The same preset narrows the RX channel filter (`MDMCFG4`)
from the stock 270 kHz down to 101 kHz by default and drops the IF to 152 kHz
(`FSCTRL1`), so the squelch hears the channel rather than half the band.

Timing is PARIS-standard: dit = 1200/wpm ms. Verified on the host — five
`PARIS` at 20 wpm renders 14.580 s of segments, which is exactly 243 dit units,
and 15.000 s once you add the trailing word space. Farnsworth spacing uses the
ARRL formula (`ta = (60c − 37.2s)/cs`, split 3/19 and 7/19), so 20 wpm characters
at 10 wpm overall lands on 30.000 s per five words.

## Beacon triggers

`Trigger` selects when the ID goes out:

| Trigger | Behaviour |
|---|---|
| `On activity` | ID after the first transmission that follows `Quiet time` of dead air. Silent on a dead channel. |
| `Interval` | ID every `Interval`, whether or not anyone is on the channel. For a fox or a plain beacon. |
| `Both` | Whichever comes first. |

`Interval` runs 15 s to 30 min. It is the **gap between transmissions** — the
time from the end of one ID to the start of the next — not a start-to-start
cycle. Measured on air with a 15 s interval and a 6.84 s ID, the cycle came out
at a very steady 22.65 s (15 + 6.84 + ~0.8 s of retune and preset reload). If
you need a fixed start-to-start cycle instead, that is a small change to the
worker.

`Quiet time` and `Max ID gap` apply only to the activity rule and are ignored in
`Interval` mode.

## Activity trigger logic

This is the `On activity` rule — repeater-controller behaviour. The controller
polls RSSI every 20 ms and runs this state machine:

```
LISTENING ──carrier > squelch for min_carrier_ms──> BUSY
BUSY ──carrier gone for hang_ms──> was the channel quiet long enough
                                    before this transmission started?
                                    ├── yes ──> PENDING
                                    └── no  ──> LISTENING
PENDING ──channel still clear after courtesy_delay──> key the ID
        └──traffic returns──> back to BUSY, ID still owed
```

So the ID goes out **at the end of the first transmission that follows the
configured quiet time**, not in the middle of someone's over. `Max ID gap` is a
backstop: if the channel has been busy continuously and that much time has
passed since the last ID, the next transmission end triggers one anyway.

`OK` on the beacon screen keys an ID immediately.

## Settings

| Setting | Notes |
|---|---|
| Station ID | The text to send. `DE CALLSIGN` placeholder — change it. |
| Frequency | Entered in 100 Hz steps over whatever the firmware can tune. Defaults to 433.920. |
| Preset | GMRS 1–7 and 15–22, plus 315.000, 432.300, 433.920, 446.000, 915.000. Shows `Custom` when the frequency is not one of them. |
| Mode | MCW (FM) or OOK (CW). |
| Deviation | 2.4 kHz (12.5 kHz channels) or 4.8 kHz (25 kHz channels). |
| RX filter | 58 / 101 / 135 / 270 kHz. Narrower = better squelch. |
| Tone | 400–1200 Hz. |
| Speed / Farnsworth | Character speed and optional stretched spacing. |
| Squelch | −110 to −50 dBm. Tune this against the live channel. |
| Trigger | On activity / Interval / Both — see above. |
| Interval | 15 s–30 min. Gap between IDs in the interval modes. |
| Quiet time | Dead air needed before the next transmission triggers an ID (activity rule only). |
| Max ID gap | Backstop interval, or OFF. |
| Courtesy | Delay after the channel clears before keying. |
| Preamble / Tail | Dead carrier before and after the message (MCW only). |
| Radio | Internal CC1101 or an attached external module. |
| Remote | BLE or UART for the remote-text screen. |
| ID on start | Off by default, so entering beacon mode does not immediately key. |

Settings persist to `/ext/apps_data/morse_beacon/beacon.conf`.

## Remote text

`Remote text` on the main menu starts the link and keys every complete line it
receives.

- **BLE** — the app takes over the Flipper's BLE serial service and advertises
  under the Flipper's own name. Connect with nRF Connect or any BLE terminal:

  | | |
  |---|---|
  | service | `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` |
  | write here | `19ed82ae-ed21-4c9d-4145-228e62fe0000` (write request or command; both work) |
  | subscribe here | `19ed82ae-ed21-4c9d-4145-228e61fe0000` — *indications*, for the `ok` / `err:` replies |

  The write characteristic carries `ATTR_PERMISSION_AUTHEN_WRITE`, so the link
  must be bonded — an unpaired write fails with insufficient authentication
  rather than doing nothing visible. While this screen is open the official
  Flipper mobile app cannot connect; the profile is restored when you back out.
- **UART** — pins 13 (TX) / 14 (RX), 8N1, baud rate configurable. For an ESP32,
  a keyboard bridge, or anything else that can emit a line of text.

A line ends at a newline **or** after a ~400 ms pause, because BLE terminals
send a message as one packet with no line ending. Lines are capped at 64
characters and the queue holds 4; if you push text faster than the key can send
it, the oldest line is dropped.

## Frequency range

This app imposes **no region policy**. It never consults a region table; the
firmware alone decides what may be transmitted. The only check here is whether
the radio can be tuned at all:

    281-361, 378-481, 749-962 MHz

which mirrors `furi_hal_subghz_is_frequency_valid()` in Unleashed/RogueMaster.
Those builds carry no country table — `furi_hal_subghz_is_tx_allowed()` permits
300-350, 387-467.75 and 779-928 MHz by default, widened to the full range above
by the firmware's extended-range setting. If the firmware refuses a transmission
the reason is logged rather than silently swallowed.

The check is duplicated here deliberately: `subghz_devices_is_frequency_valid()`
**cannot be used as a test** on the internal radio, because its implementation
calls `furi_crash()` on an out-of-range frequency instead of returning false.
Asking it about a frequency in one of the gaps reboots the device.

## Frequency presets

GMRS **15–22** (462.5500–462.7250) are the main high-power channels and the
repeater outputs RP15–RP22 — where a repeater's own ID is transmitted, which is
what this app was originally for. GMRS **1–7** (462.5625–462.7125) are the
interstitial simplex channels.

Outside GMRS:

| Preset | What it is |
|---|---|
| 315.000 | US ISM, a common Part 15 remote frequency. Bench testing. |
| 432.300 | Start of the 70 cm propagation-beacon subband (432.300–432.400, ARRL plan) — the one segment on 70 cm actually set aside for beacons. |
| 433.920 | ISM, and the frequency the Flipper's antenna is matched for. Busy with Part 15 traffic. |
| 446.000 | 70 cm national FM simplex **calling** frequency. A landmark for finding your way around the band — not somewhere to park a beacon. |
| 915.000 | US ISM centre. Bench testing. |

Band plans are voluntary and regional; coordinate locally before running a fox.

Channels **8–14** are deliberately absent: they sit at 467.5625–467.7125 MHz,
outside the band the CC1101 is rated for. The 467 MHz repeater *inputs* are
missing for the same reason, so this cannot key a repeater through its input —
only transmit on its output.

## Limits worth knowing

- The CC1101 is rated **300-348, 387-464 and 779-928 MHz**. Unleashed raises the
  default TX ceiling to 467.75 MHz, so GMRS repeater *inputs* at 467 MHz are
  within what the firmware allows — but they are outside the chip's rated band
  and the Flipper's RF matching is tuned for 433, so output power and spurious
  performance there are unspecified.
- Output is roughly **10 mW** into an antenna that is matched for 433 MHz, so
  462 MHz range is short.
- There is **no CTCSS/DCS**. Binary FSK can only sit on one of two frequencies at
  a time, so a sub-audible tone cannot be summed with the audio tone. Generating
  arbitrary audio would need duty-cycle dithering at ~100 kHz in the TX ISR —
  possible in principle, not implemented.
- `Send text` and `Remote text` key immediately without checking whether the
  channel is busy. Only the beacon mode listens first.
- A Flipper is not FCC type-accepted for Part 95 (GMRS). Under Part 97, a
  licensed amateur may use homebrew equipment. Transmit only where you are
  licensed to.

## Layout

```
morse.c/h    Morse table, PARIS + Farnsworth timing, text -> timed segments
radio.c/h    CC1101 ownership: custom presets, async-TX ISR generator, RSSI
beacon.c/h   channel-watching state machine and the ID trigger rules
link.c/h     BLE serial / UART line receiver with a worker thread
config.c/h   settings struct, defaults, persistence
morse_beacon.c   views, menus, settings list, navigation
```

## Verified

Builds clean against API 88.4, `APPCHK` matches the device, and the encoder was
checked on the host against PARIS and ARRL Farnsworth timing.

**Verified on air, 2026-09-08.** The Flipper keyed `DE CALLSIGN` on 433.920 MHz;
an RTL-SDR on the far end of an `rtl_tcp` link recorded 13 s of IQ, which was
FM-demodulated and decoded blind:

| Measured | Sent | Result |
|---|---|---|
| tone 797 Hz | 800 Hz | 0.4% |
| dit 66.4 ms → 18.1 wpm | 18 wpm, dit 66.7 ms | 0.4% |
| 4.82 kHz peak-to-peak shift | ±2.38 kHz (4.76 kHz) | 1.3% |
| carrier up 6.84 s | 6.83 s incl. preamble/tail | exact |
| **decoded `DE CALLSIGN`** | `DE CALLSIGN` | exact |

`morse_id_offair.wav` is that capture, demodulated. Scripts to repeat it are in
`tools/`.

Also exercised on the device over the CLI `input` command with no crash and flat
heap across repeated entry/exit cycles: menu, beacon RX, every settings row, and
the BLE link screen.

**BLE remote text verified on air, 2026-09-08.** `General Kenobi!` typed on an
iPhone in nRF Connect, keyed by the Flipper, decoded off air by a third radio:

```
device : link: rx "General Kenobi!"
         send: 433920000 Hz MCW 75 seg, 9633 ms planned, 9638 ms actual
off air: 9.65s tone 797 Hz dev 4.80 kHz dit 66.5 ms (18.1 wpm) -> "GENERAL KENOBI!"
```

`general_kenobi_offair.wav` is that capture.

**Interval beaconing verified on air, 2026-09-08.** Trigger `Interval` at 15 s on
an empty channel produced four IDs at 20.79, 43.44, 66.09 and 88.74 s — a 22.65 s
cycle with no jitter, each decoding as `DE CALLSIGN` at 797 Hz / 18.1 wpm.

### Bugs this shook out

- `variable_item_list_add` returns a pointer into an m-lib array that reallocates
  as later rows are added. Holding one across further adds dangles it and crashed
  the firmware on the first keypress. Use `variable_item_list_get(list, row)`.
- `subghz_devices_begin()` returns **false for the internal CC1101** — its
  interconnect has `.begin = NULL`. Only an external module reports real status,
  so treating false as failure means never acquiring the radio.
- The async TX engine **never clocks out the first and last run of same-level
  samples**. A 300 ms unkeyed preamble therefore never reached the air, and with
  preamble/tail set to zero the first and last elements of the message would be
  clipped instead. Measured directly: 1000 ms preamble + 1000 ms tail gave
  `8333 ms planned, 6338 ms actual` and 3 ms of preamble on air. The fix is to
  bracket the stream with a 1 ms run of the *opposite* level, which absorbs the
  loss; the same test then gave `8343 ms actual` with 1002 ms of preamble and
  1006 ms of tail measured off air.
- **The bt service steals the serial callback the moment a client connects.**
  `bt_on_gap_event_callback` → `bt_open_rpc_connection()` sees that the current
  profile is `ble_profile_serial`, opens an RPC session, and installs
  `bt_serial_event_callback` over the app's. Everything the phone writes then
  goes to the RPC protobuf decoder and is silently discarded — the app sees zero
  bytes and there is nothing in any log. `bt_close_rpc_connection()` is not
  exported to apps, but re-registering the callback on `BtStatusConnected` is
  enough, because the service dispatches to whichever callback is installed at
  the time.
- The serial service decrements a flow-control credit per packet and only
  restores it via `notify_buffer_is_empty()`. The release build discards the
  RX callback's return value, so the credit is never handed back on its own.
  Call it from a worker thread — never from inside the RX callback, which is
  already holding the same mutex that function takes.
- `saved_struct_load` will happily load a settings file written by an older build
  with a different struct. Bump `MORSE_CONFIG_VERSION` on any struct change, and
  clamp every field on load — `morse_config_validate()` does this, so a stale or
  corrupt file can never index off the end of a settings value table.

## Logging

Every ID and every radio error appends a timestamped line to
`/ext/apps_data/morse_beacon/id_log.txt` — both a station ID record and the only
way to see what the radio did when nobody is watching the screen:

```
2026-09-08 13:25:11 ui: entering beacon, 433920000 Hz
2026-09-08 13:25:11 listen: asked 433920000 Hz, tuned 433919830 Hz
2026-09-08 13:26:11 id: keying "DE CALLSIGN"
2026-09-08 13:26:18 send: 433920000 Hz MCW 57 seg, 6833 ms planned, 6840 ms actual
```

The file grows without bound; delete it when it gets large.
