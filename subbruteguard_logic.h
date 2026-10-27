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
