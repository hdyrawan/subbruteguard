# SubBruteGuard

A fixed-code sub-GHz brute-force/replay audit tool for the Flipper Zero,
built against official firmware.

**Why this exists:** the well-known tool that does this — DarkFlippers'
[flipperzero-subbrute](https://github.com/DarkFlippers/flipperzero-subbrute),
bundled with Unleashed firmware — does not run on official firmware; its own
README says so. SubBruteGuard builds the same candidate codes but transmits
them through official firmware's own `subghz_transmitter` pipeline, the same
path the stock Sub-GHz app uses to replay a saved `.sub` file, so it runs on
an unmodified Flipper.

## What it covers

Ten protocol/width combinations, all transmitted via their official-firmware
named encoder:

| Protocol | Bits | Search space | Frequencies (MHz) |
|---|---|---|---|
| CAME | 12 | 4096 | 303.875, 307.8, 315, 330, 433.92 |
| Nice FLO | 12 | 4096 | 433.92, 868.35 |
| Chamberlain | 9 / 8 / 7 | 512 / 256 / 128 | 300, 315, 318, 390, 433.92 (varies by width) |
| Linear | 10 | 1024 | 300, 310 |
| SMC5326 | 8 trits | 6561 | 330, 433.92 |
| UNILARM | 8 trits | 6561 | 330, 433.92 |
| PT2260 | 8 trits | 6561 | 315, 330, 390, 433.92 |
| PT2262 | 8 trits | 6561 | 315, 418, 430, 430.5, 433.92 |

SMC5326/UNILARM and PT2260/PT2262 each encode exactly 8 trinary digits plus
a fixed gate suffix, so their real search space is 3^8 = 6561, not the
2^24-2^25 their on-air bit width suggests — see `AGENTS.md` for why.

## Jurisdiction / region picker

Right after the intro screen, every launch shows a mandatory region picker:
**Unrestricted (lab)**, **EU**, **US/CA/AU**, **Japan**, or **Indonesia**.
This exists because the Flipper's own hardware region setting
(`furi_hal_region`) only models those first four presets, has no entry for
most countries including Indonesia, and was observed on a real test unit to
be completely unprovisioned (`hardware_region_provisioned: --`) — a state
that silently blocks every frequency, including ones every real region
allows. Rather than let an absent or wrong hardware fuse make the app
unusable, you tell it explicitly where you are; the app enforces that choice
on every single transmit, not just at startup. The chosen region's badge
(`EU`, `US`, `JP`, `ID`, or `LAB`) stays visible next to the title on every
later screen. See `AGENTS.md` for exactly how this combines with the
hardware's own region check, and where the Indonesia band table
(314-316, 433-434.79, 920-923 MHz, from SDPPI Decree No. 260/2024) comes
from.

## Controls

| Key | Action |
|---|---|
| `Up` / `Down` (region picker) | Change jurisdiction |
| `OK` (region picker) | Confirm and continue to the protocol/frequency picker |
| `Up` / `Down` (picker) | Pick protocol |
| `Left` / `Right` (picker) | Pick frequency |
| `OK` long press | Arm and start the run from step 0 |
| `OK` short press (while paused) | Resume the paused run from where it stopped |
| `Back` (while running) | Pause, save a checkpoint |
| `Back` (while picking, paused, or on the region screen) | Exit |

Starting a run always requires a **long** press of OK. Browsing protocol and
frequency only ever uses short presses, so the same gesture can never both
browse and arm the radio.

## How it works

1. Pick a protocol and frequency.
2. Long-press OK to arm. The app transmits candidate 0, waits ~100ms, then
   candidate 1, and so on, repeating each candidate the number of times the
   real remote's protocol expects (3-5x, matching the source protocol).
3. Every candidate is built as an in-memory `FlipperFormat` key file —
   frequency, preset, named protocol, bit count, and the candidate's 8-byte
   key — and handed to `subghz_transmitter_deserialize`, exactly like
   replaying a saved `.sub` file.
4. Before every transmit, the frequency is checked against the CC1101's
   tuning range, your chosen jurisdiction's band table, and (when the
   hardware actually has one provisioned) the hardware's own region check.
   A blocked frequency stops the run with a plain on-screen reason instead
   of silently doing nothing.
5. Every 50 candidates, progress (`{region, protocol, frequency, step}`) is
   saved to `/ext/apps_data/subbruteguard/checkpoint.txt`, so a long run
   survives a battery swap or an accidental exit without restarting the
   whole key space.

## Install

```
pip install ufbt
git clone https://github.com/hdyrawan/subbruteguard.git
cd subbruteguard
ufbt launch
```

Built against SDK API 88.2, same baseline as this project's sibling,
[bleepsentry](https://github.com/hdyrawan/bleepsentry).

## Intended use

Authorized security work only: penetration tests, red team exercises, or
auditing fixed-code remotes you own or have explicit permission to test.
This app has no stealth mode and no detection-evasion behavior — it displays
and logs exactly what it transmits.

## Status

v0.2. Builds and passes host logic tests.

Verified on hardware: install, launch, intro screen, jurisdiction/region
picker, protocol and frequency picker navigation (including wraparound),
and the arm gesture, all via RPC-driven input and screenshots. A real
on-hardware crash (`furi_check failed`) was found and fixed the first time
an armed run actually tried to transmit — the TX path now matches official
firmware's own `subghz tx` CLI command sequence; see `AGENTS.md` for the
full root cause.

The Running state (a confirmed successful transmit) has not been observed:
the test device's hardware region is completely unprovisioned, and the
stock `subghz tx` CLI command — not this app's code — refuses to transmit
on any frequency as a result. That's confirmed to be a firmware-level
safety interlock independent of any app, not a gap in subbruteguard. The
in-app region picker still serves its purpose on properly-provisioned
hardware. No screenshots captured yet toward the catalog format.

## License

GPL-3.0. See [LICENSE](LICENSE). Changes are listed in
[changelog.md](changelog.md).
