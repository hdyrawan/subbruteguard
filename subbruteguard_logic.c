#include "subbruteguard_logic.h"

#include <stdio.h>

static const uint8_t SBG_LUT_SMC[3] = {0x0, 0x2, 0x3};
static const uint8_t SBG_LUT_PT[3] = {0x0, 0x1, 0x3};

static const SbgProtocol SBG_PROTOCOLS[SbgProtoCount] = {
    [SbgProtoCame12] =
        {.label = "CAME 12-bit",
         .sub_protocol = "CAME",
         .bits = 12,
         .encoding = SbgEncodingBinary,
         .tristate_family = SbgTristateFamilyNone,
         .te_us = 0,
         .repeat = 3,
         .frequencies_hz = {303875000, 307800000, 315000000, 330000000, 433920000},
         .frequency_count = 5},
    [SbgProtoNice12] =
        {.label = "Nice FLO 12-bit",
         .sub_protocol = "Nice FLO",
         .bits = 12,
         .encoding = SbgEncodingBinary,
         .tristate_family = SbgTristateFamilyNone,
         .te_us = 0,
         .repeat = 3,
         .frequencies_hz = {433920000, 868350000},
         .frequency_count = 2},
    [SbgProtoChamberlain9] =
        {.label = "Chamberlain 9-bit",
         .sub_protocol = "Cham_Code",
         .bits = 9,
         .encoding = SbgEncodingBinary,
         .tristate_family = SbgTristateFamilyNone,
         .te_us = 0,
         .repeat = 3,
         .frequencies_hz = {300000000, 315000000, 318000000, 390000000, 433920000},
         .frequency_count = 5},
    [SbgProtoChamberlain8] =
        {.label = "Chamberlain 8-bit",
         .sub_protocol = "Cham_Code",
         .bits = 8,
         .encoding = SbgEncodingBinary,
         .tristate_family = SbgTristateFamilyNone,
         .te_us = 0,
         .repeat = 3,
         .frequencies_hz = {300000000, 315000000, 390000000},
         .frequency_count = 3},
    [SbgProtoChamberlain7] =
        {.label = "Chamberlain 7-bit",
         .sub_protocol = "Cham_Code",
         .bits = 7,
         .encoding = SbgEncodingBinary,
         .tristate_family = SbgTristateFamilyNone,
         .te_us = 0,
         .repeat = 3,
         .frequencies_hz = {300000000, 315000000, 390000000},
         .frequency_count = 3},
    [SbgProtoLinear10] =
        {.label = "Linear 10-bit",
         .sub_protocol = "Linear",
         .bits = 10,
         .encoding = SbgEncodingBinary,
         .tristate_family = SbgTristateFamilyNone,
         .te_us = 0,
         .repeat = 5,
         .frequencies_hz = {300000000, 310000000},
         .frequency_count = 2},
    [SbgProtoSmc5326] =
        {.label = "SMC5326",
         .sub_protocol = "SMC5326",
         .bits = 25,
         .encoding = SbgEncodingTriState,
         .tristate_family = SbgTristateFamilySmc,
         .te_us = 320,
         .repeat = 4,
         .frequencies_hz = {330000000, 433920000},
         .frequency_count = 2,
         .gate_bits = 9,
         .gate_value = 0x1D5},
    [SbgProtoUnilarm] =
        {.label = "UNILARM",
         .sub_protocol = "SMC5326",
         .bits = 25,
         .encoding = SbgEncodingTriState,
         .tristate_family = SbgTristateFamilySmc,
         .te_us = 209,
         .repeat = 4,
         .frequencies_hz = {330000000, 433920000},
         .frequency_count = 2,
         .gate_bits = 9,
         .gate_value = 0x180},
    [SbgProtoPt2260] =
        {.label = "PT2260",
         .sub_protocol = "Princeton",
         .bits = 24,
         .encoding = SbgEncodingTriState,
         .tristate_family = SbgTristateFamilyPt,
         .te_us = 286,
         .repeat = 4,
         .frequencies_hz = {315000000, 330000000, 390000000, 433920000},
         .frequency_count = 4,
         .gate_bits = 8,
         .gate_value = 0x03},
    [SbgProtoPt2262] =
        {.label = "PT2262",
         .sub_protocol = "Princeton",
         .bits = 24,
         .encoding = SbgEncodingTriState,
         .tristate_family = SbgTristateFamilyPt,
         .te_us = 350,
         .repeat = 4,
         .frequencies_hz = {315000000, 418000000, 430000000, 430500000, 433920000},
         .frequency_count = 5,
         .gate_bits = 8,
         .gate_value = 0x03},
};

const SbgProtocol* sbg_protocol(SbgProtocolId id) {
    return &SBG_PROTOCOLS[id];
}

uint64_t sbg_total_steps(SbgProtocolId id) {
    const SbgProtocol* p = &SBG_PROTOCOLS[id];
    if(p->encoding == SbgEncodingTriState) {
        return 6561; /* 3^8: only the 8 trits vary, the gate is fixed */
    }
    return (uint64_t)1 << p->bits;
}

void sbg_candidate_bytes(SbgProtocolId id, uint64_t step, uint8_t* out) {
    const SbgProtocol* p = &SBG_PROTOCOLS[id];
    uint64_t total = 0;

    if(p->encoding == SbgEncodingTriState) {
        const uint8_t* lut = (p->tristate_family == SbgTristateFamilySmc) ? SBG_LUT_SMC :
                                                                            SBG_LUT_PT;
        uint64_t s = step;
        for(int j = 0; j < 8; j++) {
            total |= (uint64_t)lut[s % 3] << (2 * j);
            s /= 3;
        }
        total <<= p->gate_bits;
        total |= p->gate_value;
    } else {
        total = step;
    }

    for(int i = 0; i < SBG_CANDIDATE_BYTES; i++) {
        out[i] = (uint8_t)((total >> (8 * (SBG_CANDIDATE_BYTES - 1 - i))) & 0xFF);
    }
}

bool sbg_candidate_hex(SbgProtocolId id, uint64_t step, char* out, size_t out_len) {
    uint8_t bytes[SBG_CANDIDATE_BYTES];
    sbg_candidate_bytes(id, step, bytes);

    /* 8 bytes as "XX XX XX XX XX XX XX XX": 23 chars + nul = 24 */
    if(out_len < 24) return false;

    int pos = 0;
    for(int i = 0; i < SBG_CANDIDATE_BYTES; i++) {
        pos += snprintf(out + pos, out_len - (size_t)pos, i == 0 ? "%02X" : " %02X", bytes[i]);
    }
    return true;
}

bool sbg_format_progress(uint64_t step, uint64_t total, char* out, size_t out_len) {
    int written =
        snprintf(out, out_len, "%llu/%llu", (unsigned long long)step, (unsigned long long)total);
    return written > 0 && (size_t)written < out_len;
}
