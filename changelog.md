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
