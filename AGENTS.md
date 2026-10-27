# AGENTS.md

Guidance for AI coding agents (and anyone else) working in this repository.
Read this before changing code or the catalog submission.

Upstream rules that override anything here:
the Flipper Apps Catalog `documentation/Contributing.md`, `documentation/Manifest.md`
and `AGENTS.md` in <https://github.com/flipperdevices/flipper-application-catalog>.

## What this app is

SubBruteGuard audits fixed-code sub-GHz remotes (garage/gate/barrier openers
using CAME, Nice FLO, Chamberlain, Linear, SMC5326, UNILARM, or
Princeton-family encoders) by transmitting the full code space one candidate
at a time, exactly the way a captured `.sub` file would replay. It exists
because the one widely-known tool that already does this —
DarkFlippers/flipperzero-subbrute, bundled with Unleashed firmware — does not
run on official firmware; its own README says so, and a known forum report
confirms an "unsupported protocol" error when its output is replayed on
stock firmware. This app builds the same candidate codes but feeds them
through official firmware's own `subghz_transmitter`/`FlipperFormat`
pipeline, the same path the stock Sub-GHz app uses for a saved key file, so
it runs on an unmodified Flipper.

**This is an active transmit tool. Treat it as more dangerous than a passive
app by default:**

- Every single candidate, not just the session as a whole, is checked against
  **both** `furi_hal_region_is_frequency_allowed(freq)` and
  `furi_hal_subghz_is_frequency_valid(freq)` before any TX call. The HAL
  itself also refuses `furi_hal_subghz_start_async_tx` on a disallowed
  frequency, so this is defense in depth, not the only gate — but never
  remove the explicit app-level check to "simplify" the TX path; it's the
  reason a blocked session shows a clear reason instead of a silent stall.
- Starting a run requires a **long press** of OK from the picker, never a
  short press. Up/Down/Left/Right (protocol and frequency selection) are all
  short presses. This asymmetry is deliberate: it must not be possible to
  arm the radio with the same gesture used to browse. Don't change the arm
  gesture to match browsing without re-reading this paragraph.
- Intended use is authorized security work only: a penetration test, red
  team exercise, or testing hardware you own or have explicit permission to
  test. This app has no feature that hides who it's talking to or disguises
  what it's doing (no silent mode, no stealth timing) — it's an audit tool
  that logs and displays what it's sending, not a covert one. Keep it that
  way; do not add detection-evasion behavior.

## Repository map

| Path | Purpose |
|---|---|
| `subbruteguard.c` | The app: message queue loop, timer, input handling, region-gated TX, checkpoint save/load |
| `subbruteguard_logic.c` / `.h` | Pure logic with no Flipper SDK dependency: protocol table, candidate-code packing (binary and tri-state), progress formatting |
| `tests/test_logic.c` | Host-side assert suite for the pure logic, run in CI |
| `application.fam` | FAP manifest: `appid`, `name`, category, version, icon, sources |
| `docs/description.md` | Catalog description text, written in the restricted markdown subset |
| `changelog.md` | Version history, loaded by the catalog manifest |
| `screenshots/` | Catalog screenshots plus `README.md` (pending first hardware capture, see Status below) |
| `.github/workflows/build.yml` | CI: host logic tests plus a `ufbt` build on dev and release SDK channels |

## The protocol table and candidate encoding

`subbruteguard_logic.c`'s protocol table and its trinary-packing algorithm
(an 8-symbol LUT shifted into a fixed-width field, plus a fixed gate suffix)
are modeled directly on
[DarkFlippers/flipperzero-subbrute](https://github.com/DarkFlippers/flipperzero-subbrute)'s
`subbrute_protocols.c` (MIT licensed) — frequencies, bit widths, `TE` values,
and repeat counts were read from that source, not guessed. Two differences
from that project, both deliberate:

- **Named-protocol TX, not raw timing.** Unleashed's subbrute writes a `TE:`
  tail and in some cases relies on raw bit-banging. This app always builds a
  `FlipperFormat` with a `Protocol:` field naming an encoder that ships in
  official firmware (`CAME`, `Nice FLO`, `Cham_Code`, `Linear`, `SMC5326`,
  `Princeton`) and feeds it to `subghz_transmitter_deserialize`, the same
  pipeline the stock Sub-GHz app uses to replay a saved key file. This is
  what makes official-firmware compatibility real instead of aspirational;
  don't reintroduce a hand-rolled OOK envelope path to "simplify" a protocol
  without checking that the named encoder actually isn't available first.
- **Tri-state search space is 3^8 = 6561, not 2^24 or 2^25.** SMC5326,
  UNILARM, PT2260, and PT2262 encode exactly 8 trinary digits (symbol values
  packed from a 3-entry LUT) plus a *fixed* gate/button suffix appended after
  packing. The `.bits` field in the protocol table (24 or 25) is the on-air
  bit width for the `FlipperFormat` `Bit:` field, not the search space —
  `sbg_total_steps` returns 6561 for these, the real number of candidates
  that differ. Do not "fix" this to `1 << bits`; that would brute-force a
  space 2500x larger than the one that actually varies.

Two of the four tri-state protocols — `SbgTristateFamilySmc` covers both
SMC5326 and UNILARM (same LUT, different gate), `SbgTristateFamilyPt` covers
both PT2260 and PT2262 — share a `sub_protocol` name too (`"SMC5326"` and
`"Princeton"` respectively), because the upstream project's own file-type
table maps them that way: UNILARM and PT2262 are gate/button variants of the
same underlying encoder, not distinct ones.

## Build, test, device loop

```
ufbt            # build, output in dist/ and ~/.ufbt/build/
ufbt lint       # SDK format/tidy gate, must stay clean
ufbt launch     # build, install to /ext/apps/Sub-GHz/subbruteguard.fap, start
```

Host tests, no device needed:

```
cd tests
gcc -std=c11 -Wall -Wextra -I.. test_logic.c ../subbruteguard_logic.c -lm -o test_logic
./test_logic
```

Pitfalls carried over from the bleepsentry sibling project, still true here:

- **`ufbt launch` can end with `Application "SubBruteGuard" has to be closed
  manually`.** The install already succeeded; the loader can't kill a plain
  `ViewPort` app remotely. Send `Back` over RPC, then relaunch. Don't re-flash
  chasing that message.
- **`MissingImports` on load** means the FAP targets a newer API than the
  device runs. This repo targets API 88.2, tested on official firmware 1.5.1
  (same baseline as bleepsentry).
- `sources=["*.c*", "!tests"]` in `application.fam` is deliberate, same reason
  as bleepsentry: the default glob is recursive and would compile the host
  test file into the FAP.

## Coding rules

- Match the Flipper coding style. `.clang-format` is in the repo; run
  `ufbt lint` before pushing.
- Keep `subbruteguard_logic.c` free of Flipper headers so it stays
  host-testable. New candidate-encoding logic, protocol table entries, or
  formatting belong there, with a case in `tests/test_logic.c`.
- Never block while holding `app->mutex`: the draw callback takes the same
  lock. `sbg_send_one_code` is called with the mutex released for exactly
  this reason — it blocks for the duration of an actual radio transmission.
- Write only to `APP_DATA_PATH(...)`, which resolves to
  `/ext/apps_data/subbruteguard/`. The checkpoint file (`checkpoint.txt`)
  lives there. Never write into `/ext/apps/` or another app's directory.
- Free every allocation on every exit path: `FlipperFormat`,
  `SubGhzEnvironment`, and `SubGhzTransmitter` are all allocated fresh per
  candidate in `sbg_send_one_code` and must all be freed there, including on
  the failure branches.
- No dead code, no leftover debug logging beyond the existing `FURI_LOG_D`
  line, no placeholder branches in shipped sources.
- All on-screen strings are ASCII and English, same font-glyph constraint as
  bleepsentry.

## Checkpointing

A brute-force run over a real key space can span many thousands of codes;
`CHECKPOINT_PERIOD` (50) codes between SD card writes is a deliberate
trade-off between resume granularity and not stalling TX cadence on slow SD
I/O. Checkpoint format is plain text, `protocol_id freq_index step`, one
line, overwritten each time — see `sbg_checkpoint_save`/`sbg_checkpoint_load`.
If you change the protocol table's ordering or add a protocol in the middle
of the enum, any checkpoint written by an older build becomes meaningless
(the index points at a different protocol); that's accepted as a known
limitation for a pre-1.0 app, not something to add migration code for.

## Catalog submission

Not yet submitted. This repository is pre-1.0 and has not been exercised on
hardware. Follow bleepsentry's `AGENTS.md` catalog-submission section for the
mechanics (manifest path convention, markdown subset, `commit_sha` pinning)
when this app is ready; don't duplicate that process here until there's a
hardware-verified build to submit.

## Definition of done for a change

1. `ufbt` builds clean and `ufbt lint` passes.
2. Host logic tests pass; new logic has a test.
3. The app is installed and exercised on hardware: picker navigation, the arm
   gesture, a short run on a protocol known to be region-allowed, pause and
   resume, Back to exit. Verifying that a *correct* code actually opens a
   real device is out of scope without a cooperating fixed-code remote to
   test against — note this limitation rather than claiming it's confirmed.
4. `fap_version` and `changelog.md` are updated.
5. Only after 1-4: screenshots captured and `screenshots/README.md` written,
   following bleepsentry's capture method (RPC snapshot, exact two-colour
   palette, no rescale wording).
