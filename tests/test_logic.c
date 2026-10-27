#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "subbruteguard_logic.h"

static int failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if(!(cond)) {                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while(0)

static void test_total_steps(void) {
    CHECK(sbg_total_steps(SbgProtoCame12) == 4096); /* 2^12 */
    CHECK(sbg_total_steps(SbgProtoChamberlain7) == 128); /* 2^7 */
    CHECK(sbg_total_steps(SbgProtoLinear10) == 1024); /* 2^10 */
    CHECK(sbg_total_steps(SbgProtoSmc5326) == 6561); /* 3^8 */
    CHECK(sbg_total_steps(SbgProtoUnilarm) == 6561);
    CHECK(sbg_total_steps(SbgProtoPt2260) == 6561);
    CHECK(sbg_total_steps(SbgProtoPt2262) == 6561);
}

static void test_binary_candidate_is_step_itself(void) {
    uint8_t bytes[SBG_CANDIDATE_BYTES];

    sbg_candidate_bytes(SbgProtoCame12, 0, bytes);
    for(int i = 0; i < 7; i++)
        CHECK(bytes[i] == 0);
    CHECK(bytes[7] == 0x00);

    sbg_candidate_bytes(SbgProtoCame12, 1, bytes);
    for(int i = 0; i < 7; i++)
        CHECK(bytes[i] == 0);
    CHECK(bytes[7] == 0x01);

    sbg_candidate_bytes(SbgProtoCame12, 0x0FFF, bytes); /* max 12-bit value */
    CHECK(bytes[6] == 0x0F);
    CHECK(bytes[7] == 0xFF);
}

/* Step 0 of a tri-state protocol packs all-zero trits, so the candidate is
 * just the fixed gate suffix, right-aligned in the 64-bit field. This mirrors
 * DarkFlippers/flipperzero-subbrute's subbrute_protocol_create_candidate_for_default. */
static void test_tristate_step_zero_is_bare_gate(void) {
    uint8_t bytes[SBG_CANDIDATE_BYTES];

    sbg_candidate_bytes(SbgProtoSmc5326, 0, bytes);
    for(int i = 0; i < 6; i++)
        CHECK(bytes[i] == 0);
    CHECK(bytes[6] == 0x01);
    CHECK(bytes[7] == 0xD5); /* gate_smsc = 0x1D5 */

    sbg_candidate_bytes(SbgProtoUnilarm, 0, bytes);
    for(int i = 0; i < 6; i++)
        CHECK(bytes[i] == 0);
    CHECK(bytes[6] == 0x01);
    CHECK(bytes[7] == 0x80); /* gate_uni_alarm = 3 << 7 = 0x180 */

    sbg_candidate_bytes(SbgProtoPt2260, 0, bytes);
    for(int i = 0; i < 7; i++)
        CHECK(bytes[i] == 0);
    CHECK(bytes[7] == 0x03); /* gate_pt2260 = 0x03 */
}

/* Step 1 should flip exactly the lowest trit (index 0) from 0 to 1 (symbol
 * value 2 for the SMC/UNILARM LUT), appearing as bit 9-10 above the gate. */
static void test_tristate_step_one_flips_lowest_trit(void) {
    uint8_t bytes[SBG_CANDIDATE_BYTES];

    sbg_candidate_bytes(SbgProtoSmc5326, 1, bytes);
    /* total = (0x2 << 0) << 9 | 0x1D5 = 0x400 | 0x1D5 = 0x5D5 */
    CHECK(bytes[6] == 0x05);
    CHECK(bytes[7] == 0xD5);

    sbg_candidate_bytes(SbgProtoPt2260, 1, bytes);
    /* total = (0x1 << 0) << 8 | 0x03 = 0x100 | 0x03 = 0x103 */
    CHECK(bytes[6] == 0x01);
    CHECK(bytes[7] == 0x03);
}

static void test_candidate_hex_formatting(void) {
    char buf[32];
    CHECK(sbg_candidate_hex(SbgProtoCame12, 1, buf, sizeof(buf)));
    CHECK(strcmp(buf, "00 00 00 00 00 00 00 01") == 0);

    char tiny[4];
    CHECK(!sbg_candidate_hex(SbgProtoCame12, 1, tiny, sizeof(tiny)));
}

static void test_progress_formatting(void) {
    char buf[32];
    CHECK(sbg_format_progress(0, 4096, buf, sizeof(buf)));
    CHECK(strcmp(buf, "0/4096") == 0);

    CHECK(sbg_format_progress(6560, 6561, buf, sizeof(buf)));
    CHECK(strcmp(buf, "6560/6561") == 0);

    char tiny[3];
    CHECK(!sbg_format_progress(123456789, 999999999, tiny, sizeof(tiny)));
}

static void test_protocol_table_sanity(void) {
    for(int i = 0; i < SbgProtoCount; i++) {
        const SbgProtocol* p = sbg_protocol((SbgProtocolId)i);
        CHECK(p->label != NULL);
        CHECK(p->sub_protocol != NULL);
        CHECK(p->frequency_count > 0 && p->frequency_count <= SBG_MAX_FREQUENCIES);
        CHECK(p->bits > 0 && p->bits <= 64);
        if(p->encoding == SbgEncodingTriState) {
            CHECK(p->tristate_family != SbgTristateFamilyNone);
            CHECK(p->gate_bits > 0 && p->gate_bits < 16);
        } else {
            CHECK(p->tristate_family == SbgTristateFamilyNone);
        }
    }
}

static void test_region_eu(void) {
    CHECK(sbg_region_allows(SbgRegionEu, 433920000)); /* inside 433.05-434.79 */
    CHECK(sbg_region_allows(SbgRegionEu, 868350000)); /* inside 868.15-868.55 */
    CHECK(!sbg_region_allows(SbgRegionEu, 315000000)); /* EU has no 300MHz-ish band */
    CHECK(!sbg_region_allows(SbgRegionEu, 915000000));
}

static void test_region_us_ca_au(void) {
    CHECK(sbg_region_allows(SbgRegionUsCaAu, 307800000)); /* inside 304.1-321.95 */
    CHECK(sbg_region_allows(SbgRegionUsCaAu, 433920000));
    CHECK(sbg_region_allows(SbgRegionUsCaAu, 915000000)); /* band edge, inclusive */
    CHECK(!sbg_region_allows(SbgRegionUsCaAu, 303875000)); /* just below 304.1 cutoff */
    CHECK(!sbg_region_allows(SbgRegionUsCaAu, 868350000));
}

static void test_region_indonesia(void) {
    CHECK(sbg_region_allows(SbgRegionIndonesia, 315000000)); /* inside 314-316 */
    CHECK(sbg_region_allows(SbgRegionIndonesia, 433920000)); /* inside 433-434.79 */
    CHECK(!sbg_region_allows(SbgRegionIndonesia, 303875000));
    CHECK(!sbg_region_allows(SbgRegionIndonesia, 868350000)); /* no EU-style 868 band */
    CHECK(!sbg_region_allows(SbgRegionIndonesia, 918000000)); /* just below 920 cutoff */
}

static void test_region_unrestricted_covers_everything(void) {
    for(int i = 0; i < SbgProtoCount; i++) {
        const SbgProtocol* p = sbg_protocol((SbgProtocolId)i);
        for(int f = 0; f < p->frequency_count; f++) {
            CHECK(sbg_region_allows(SbgRegionUnrestricted, p->frequencies_hz[f]));
        }
    }
}

static void test_region_table_sanity(void) {
    for(int i = 0; i < SbgRegionCount; i++) {
        const SbgRegion* r = sbg_region((SbgRegionId)i);
        CHECK(r->label != NULL);
        CHECK(r->band_count > 0 && r->band_count <= SBG_MAX_BANDS);
        for(uint8_t b = 0; b < r->band_count; b++) {
            CHECK(r->bands[b].start_hz < r->bands[b].end_hz);
        }
    }
}

int main(void) {
    test_total_steps();
    test_binary_candidate_is_step_itself();
    test_tristate_step_zero_is_bare_gate();
    test_tristate_step_one_flips_lowest_trit();
    test_candidate_hex_formatting();
    test_progress_formatting();
    test_protocol_table_sanity();
    test_region_eu();
    test_region_us_ca_au();
    test_region_indonesia();
    test_region_unrestricted_covers_everything();
    test_region_table_sanity();

    if(failures == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    printf("%d test(s) failed.\n", failures);
    return 1;
}
