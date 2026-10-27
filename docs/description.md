# SubBruteGuard

Audits fixed-code sub-GHz remotes (garage/gate/barrier openers using CAME,
Nice FLO, Chamberlain, Linear, SMC5326, UNILARM, or Princeton-family
encoders) by transmitting the full code space, one candidate at a time,
through official firmware's own named-protocol transmit pipeline.

**Authorized security work only**: penetration tests, red team exercises, or
auditing fixed-code remotes you own or have explicit permission to test.

## Why

The well-known tool that already does this is bundled with Unleashed
firmware, not official firmware, and its own documentation says it has not
been tested outside Unleashed. SubBruteGuard builds the same candidate codes
but feeds them through the stock Sub-GHz app's own transmit pipeline, so it
runs on an unmodified Flipper.

## Protocols covered

- CAME (12-bit)
- Nice FLO (12-bit)
- Chamberlain (7, 8, and 9-bit)
- Linear (10-bit)
- SMC5326 and UNILARM (8 trinary digits)
- PT2260 and PT2262 (8 trinary digits)

## Safety

- Every single transmit is checked against the device's region settings
  before it goes out, not just the session as a whole.
- Starting a run always requires a long press of OK, a gesture kept
  deliberately distinct from the short presses used to browse protocols and
  frequencies, so the radio can never be armed by accident.
- The app has no stealth mode. It displays and logs exactly what it
  transmits.

## Status

Early release. Builds and passes its host-side logic tests. Not yet
exercised against a real fixed-code remote.
