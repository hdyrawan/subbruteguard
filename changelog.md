## 0.2

- Added a mandatory jurisdiction/region picker shown once per launch, right
  after the intro screen: Unrestricted (lab), EU, US/CA/AU, Japan, or
  Indonesia. Exists because the hardware's own region setting has no entry
  for most countries including Indonesia, and was observed on a real test
  unit to be completely unprovisioned, a state that silently blocked every
  frequency regardless of what region would actually allow. The selected
  region's badge stays visible next to the title on every later screen.
- The per-candidate transmit gate is now three independent checks: the
  CC1101's physical tuning range, the user's chosen jurisdiction's band
  table (new, pure-logic, host-tested), and the hardware's own region check
  (only consulted when the hardware actually has a region provisioned, so an
  absent region fuse can no longer block a jurisdiction the user explicitly
  selected).
- EU/US/CA/AU/Japan band tables copied verbatim from official firmware's own
  `furi_hal_region.c`. Indonesia's band table (314-316, 433-434.79,
  920-923 MHz) is sourced from SDPPI Decree No. 260/2024's short-range-device
  allocations via secondary regulatory summaries, documented in
  `subbruteguard_logic.h` as needing verification against the decree text
  before relying on it for real authorized testing.
- Checkpoint format gained a region field (`region protocol freq_index
  step`); checkpoints from 0.1 are not compatible.
- Shortened the Chamberlain protocol labels ("Chamberlain 9-bit" ->
  "Cham 9-bit") after noticing the full names risked overflowing the
  128px-wide frequency line.
- Fixed a real on-hardware crash (`furi_check failed`) in the TX path,
  found the first time an armed run actually tried to transmit: the
  transmitter's `SubGhzEnvironment` never had a protocol registry attached
  (`subghz_environment_set_protocol_registry`), and TX was driven through
  raw `furi_hal_subghz_*` calls with a manual repeat loop instead of the
  `subghz_devices_*` abstraction official firmware's own CLI and GUI both
  use. Rewrote `sbg_send_one_code` to match official firmware's own
  `subghz_cli.c` "subghz tx" command sequence exactly. Confirmed fixed:
  armed runs no longer crash the device.
- Confirmed, using the stock `subghz tx` CLI command (not this app's code),
  that the test device refuses to transmit on *any* frequency regardless of
  app-level region selection, because its hardware region is completely
  unprovisioned. This is a firmware-level safety interlock independent of
  any app, not a subbruteguard bug — the in-app region picker still has a
  real purpose on properly-provisioned hardware (filling the gap for
  countries like Indonesia the hardware has no preset for), but it cannot
  and should not try to override an unprovisioned device's hard refusal.
- Partially verified on hardware: install, launch, intro screen, region
  picker, picker navigation (including wraparound), and the arm gesture all
  confirmed via RPC-driven input and screenshots. The Running state itself
  (a successful transmit) still has not been observed, now confirmed to be
  a hardware limitation of this specific test unit rather than anything
  left to fix in the app.

## 0.1

- Initial scaffold: protocol table and candidate-encoding logic for CAME,
  Nice FLO, Chamberlain (7/8/9-bit), Linear, SMC5326, UNILARM, PT2260, and
  PT2262, modeled on DarkFlippers/flipperzero-subbrute's published
  frequencies, bit widths, and timing, but transmitted through official
  firmware's own named-protocol `subghz_transmitter` pipeline instead of raw
  OOK timing.
- Picker UI (protocol/frequency), a long-press arm gesture kept deliberately
  distinct from the short presses used to browse, pause/resume, and a
  periodic on-SD checkpoint so a long run survives a battery swap.
- Per-candidate region gating: every transmit is checked against
  `furi_hal_region_is_frequency_allowed` and
  `furi_hal_subghz_is_frequency_valid` before it goes out.
- Host logic test suite covers total-step counts, the binary candidate
  encoding, the tri-state LUT-and-gate packing (verified against known step
  0 and step 1 vectors), hex/progress formatting, and protocol table
  sanity.
- Not yet tested on hardware; no screenshots yet.
