#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// an FP8 encoding: how many bits its mantissa has after the sign and exponent, its exponent bias, and whether its all-ones exponent means infinity and NaN, like IEEE, or whether it's an ordinary exponent and only all-ones bits are NaN
struct Fp8 {
    int mantissaBits;
    int bias;
    bool hasInfinity;
};

// E4M3 is s-eeee-mmm with a bias of 7, where exponent 1111 is an ordinary exponent, so there's no infinity and only s1111111 is NaN, its largest value is 1.110 x 2^8 = 448, and its smallest is the subnormal 0.001 x 2^-6 = 2^-9
static const struct Fp8 e4m3 = {3, 7, false};
// E5M2 is s-eeeee-mm with a bias of 15, laid out exactly like a tiny IEEE float, where exponent 11111 is infinity with a zero mantissa and NaN otherwise, its largest value is 1.11 x 2^15 = 57344, and its smallest is the subnormal 0.01 x 2^-14 = 2^-16
static const struct Fp8 e5m2 = {2, 15, true};

// turns an FP8 byte into the float it stands for, which always holds it exactly
static float decode(uint8_t byte, struct Fp8 format) {
    int sign = (uint8_t)(byte >> 7) & 0x1;
    int exponent = (uint8_t)(byte << 1) >> (1 + format.mantissaBits);
    int mantissa = (uint8_t)(byte << (8 - format.mantissaBits)) >> (8 - format.mantissaBits);
    // the all-ones exponent field, 1111 in E4M3 and 11111 in E5M2
    int allOnes = (1 << (7 - format.mantissaBits)) - 1;
    float magnitude;
    if (format.hasInfinity && exponent == allOnes) {
        magnitude = mantissa == 0 ? INFINITY : NAN;
    } else if (!format.hasInfinity && exponent == allOnes && mantissa == (1 << format.mantissaBits) - 1) {
        magnitude = NAN;
    } else if (exponent == 0) {
        // a zero exponent is subnormal, with no implicit leading one and the smallest normal's exponent
        magnitude = ldexpf((float)mantissa, 1 - format.bias - format.mantissaBits);
    } else {
        magnitude = ldexpf((float)(mantissa | (1 << format.mantissaBits)), exponent - format.bias - format.mantissaBits);
    }
    return sign ? -magnitude : magnitude;
}

// turns a double into the nearest FP8 byte, ties to even, where a value past the largest one overflows into infinity, or into NaN in an encoding without infinity
static uint8_t encode(double value, struct Fp8 format) {
    uint8_t sign = signbit(value) ? 0x80 : 0;
    // the byte past the largest value without its sign, which is E5M2's infinity, 0-11111-00, or E4M3's NaN, 0-1111-111
    uint8_t overflow = format.hasInfinity ? 0x7C : 0x7F;
    if (isnan(value)) return sign | 0x7F;
    double magnitude = fabs(value);
    if (isinf(magnitude)) return sign | overflow;
    if (magnitude == 0) return sign;
    // the exponent in 1.m x 2^exponent, which is one below what frexp gives, since frexp gives 0.1m x 2^e, and never below the smallest normal's, which subnormals share
    int exponent;
    frexp(magnitude, &exponent);
    exponent -= 1;
    if (exponent < 1 - format.bias) exponent = 1 - format.bias;
    // scaled so one unit is the last mantissa bit, then rounded to nearest even, so 17.0 in E4M3 scales to 8.5 and rounds to 8
    double significand = nearbyint(ldexp(magnitude, format.mantissaBits - exponent));
    // rounding up can reach 2^(mantissaBits + 1), like 31.9 scaling to 15.95 and rounding to 16, which is the next exponent's 1.000
    if (significand >= ldexp(1, format.mantissaBits + 1)) {
        significand /= 2;
        exponent += 1;
    }
    unsigned whole = (unsigned)significand;
    // a normal significand has its implicit one set and gets the biased exponent as its field, and a subnormal one doesn't and gets 0
    unsigned field = (whole >> format.mantissaBits) ? (unsigned)(exponent + format.bias) : 0;
    unsigned bits = (field << format.mantissaBits) | (whole & ((1u << format.mantissaBits) - 1));
    // anything at or past the overflow byte didn't fit, which for E4M3 includes landing exactly on its NaN pattern
    if (bits >= overflow) return sign | overflow;
    return sign | (uint8_t)bits;
}

// codegen computes with FP8 in f32 and narrows the result back, so these are the only four conversions it needs, two per encoding, under plain C names, since every symbol a program defines has a dot in it
float cheri_e4m3_widen(uint8_t byte) {
    return decode(byte, e4m3);
}

uint8_t cheri_e4m3_narrow(double value) {
    return encode(value, e4m3);
}

float cheri_e5m2_widen(uint8_t byte) {
    return decode(byte, e5m2);
}

uint8_t cheri_e5m2_narrow(double value) {
    return encode(value, e5m2);
}
