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

## Controls

| Key | Action |
|---|---|
| `Up` / `Down` | Pick protocol |
| `Left` / `Right` | Pick frequency |
| `OK` long press | Arm and start the run from step 0 |
| `OK` short press (while paused) | Resume the paused run from where it stopped |
| `Back` (while running) | Pause, save a checkpoint |
| `Back` (while picking or paused) | Exit |

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
4. Before every transmit, the frequency is checked against
   `furi_hal_region_is_frequency_allowed` and
   `furi_hal_subghz_is_frequency_valid`. A blocked frequency stops the run
   with a plain on-screen reason instead of silently doing nothing.
5. Every 50 candidates, progress (`{protocol, frequency, step}`) is saved to
   `/ext/apps_data/subbruteguard/checkpoint.txt`, so a long run survives a
   battery swap or an accidental exit without restarting the whole key space.

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

v0.1. Builds and passes host logic tests. **Not yet exercised on hardware.**
No screenshots yet — see `AGENTS.md`'s Definition of Done; hardware
verification comes before screenshot capture, and this hasn't happened yet.

## License

GPL-3.0. See [LICENSE](LICENSE). Changes are listed in
[changelog.md](changelog.md).
