#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure logic for SubBruteGuard: candidate-code generation and progress
 * formatting for a fixed set of fixed-code sub-GHz protocols. No Flipper SDK
 * dependency, so it is host-testable (see tests/test_logic.c).
 *
 * The protocol table and the trinary packing algorithm (LUT + fixed gate
 * suffix) are modeled on DarkFlippers/flipperzero-subbrute
 * (subbrute_protocols.c, MIT licensed), the brute-force app bundled with
 * Unleashed firmware. That project does not run on official firmware; this
 * one targets official firmware's own subghz_transmitter pipeline, so the
 * candidate bytes this module produces are written into a FlipperFormat
 * "Key:" field using named protocols (CAME, Linear, Cham_Code, Nice FLO,
 * SMC5326, Princeton) the same way a captured .sub file would be, instead of
 * hand-rolling OOK timing.
 */

#define SBG_MAX_FREQUENCIES 5
#define SBG_CANDIDATE_BYTES 8

typedef enum {
    SbgProtoCame12,
    SbgProtoNice12,
    SbgProtoChamberlain9,
    SbgProtoChamberlain8,
    SbgProtoChamberlain7,
    SbgProtoLinear10,
    SbgProtoSmc5326,
    SbgProtoUnilarm,
    SbgProtoPt2260,
    SbgProtoPt2262,
    SbgProtoCount,
} SbgProtocolId;

typedef enum {
    SbgEncodingBinary,
    SbgEncodingTriState,
} SbgEncoding;

/* Which 3-symbol lookup table a tri-state protocol uses to pack its 8 trits.
 * Unused for SbgEncodingBinary protocols. */
typedef enum {
    SbgTristateFamilyNone,
    SbgTristateFamilySmc, /* SMC5326 and UNILARM: symbols {0, 2, 3} */
    SbgTristateFamilyPt, /* PT2260 and PT2262: symbols {0, 1, 3} */
} SbgTristateFamily;

typedef struct {
    const char* label; /* shown in the on-device protocol picker */
    const char* sub_protocol; /* FlipperFormat "Protocol:" value */
    uint8_t bits; /* FlipperFormat "Bit:" value */
    SbgEncoding encoding;
    SbgTristateFamily tristate_family;
    uint32_t te_us; /* FlipperFormat "TE:" value, 0 omits the field */
    uint8_t repeat;
    uint32_t frequencies_hz[SBG_MAX_FREQUENCIES];
    uint8_t frequency_count;
    uint8_t gate_bits; /* tri-state only: width of the fixed suffix */
    uint32_t gate_value; /* tri-state only: the fixed suffix pattern */
} SbgProtocol;

/* Returns the protocol definition for `id`. `id` must be < SbgProtoCount. */
const SbgProtocol* sbg_protocol(SbgProtocolId id);

/* Number of distinct codes this protocol will step through: 2^bits for a
 * binary protocol, 6561 (3^8) for a tri-state one, since only the 8 trits
 * vary and the gate suffix is fixed. */
uint64_t sbg_total_steps(SbgProtocolId id);

/* Writes the 8 big-endian bytes of the FlipperFormat "Key:" field's 64-bit
 * value for candidate `step` of protocol `id`. `out` must have room for
 * SBG_CANDIDATE_BYTES bytes. */
void sbg_candidate_bytes(SbgProtocolId id, uint64_t step, uint8_t* out);

/* Formats those bytes as "XX XX XX XX XX XX XX XX" (24 chars + nul). Returns
 * false if out_len is too small. */
bool sbg_candidate_hex(SbgProtocolId id, uint64_t step, char* out, size_t out_len);

/* Formats "step/total" compactly for the 128x64 display. Returns false if
 * out_len is too small. */
bool sbg_format_progress(uint64_t step, uint64_t total, char* out, size_t out_len);

/* User-selected jurisdiction for the legal transmit check. The Flipper's own
 * hardware region (furi_hal_region) only models 4 presets and can be
 * unprovisioned entirely (observed on real hardware: "--", which fails
 * closed on every frequency, including ones every preset allows) and has no
 * entry for most countries, including Indonesia. This table is an explicit,
 * user-visible, user-chosen alternative, not a replacement for the hardware
 * check: both are applied together, see AGENTS.md "Region gating" section.
 *
 * SbgRegionEu/UsCaAu/Jp bands are copied verbatim from official firmware's
 * own targets/f7/furi_hal/furi_hal_region.c. SbgRegionIndonesia is sourced
 * from SDPPI Decree No. 260/2024's short-range-device WPAN band assignments
 * (314-316 MHz, 433-434.79 MHz, 920-923 MHz) via secondary regulatory
 * summaries, not the decree text directly - verify against the current
 * decree before relying on this for real-world authorized testing.
 * SbgRegionUnrestricted mirrors firmware's own "00"/developer region
 * (0-1000 MHz, fully open) and is for lab-bench use only. */
typedef enum {
    SbgRegionUnrestricted,
    SbgRegionEu,
    SbgRegionUsCaAu,
    SbgRegionJp,
    SbgRegionIndonesia,
    SbgRegionCount,
} SbgRegionId;

#define SBG_MAX_BANDS 3

typedef struct {
    uint32_t start_hz;
    uint32_t end_hz;
} SbgBand;

typedef struct {
    const char* label;
    SbgBand bands[SBG_MAX_BANDS];
    uint8_t band_count;
} SbgRegion;

const SbgRegion* sbg_region(SbgRegionId id);

/* True if `freq_hz` falls within any of the region's allowed bands
 * (inclusive of both endpoints, matching furi_hal_region's own
 * convention). */
bool sbg_region_allows(SbgRegionId id, uint32_t freq_hz);
