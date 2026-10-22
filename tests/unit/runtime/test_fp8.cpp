#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>

// the runtime's FP8 conversions, which are C functions under their own names
extern "C" {
float cheri_e4m3_widen(uint8_t byte);
uint8_t cheri_e4m3_narrow(double value);
float cheri_e5m2_widen(uint8_t byte);
uint8_t cheri_e5m2_narrow(double value);
}
// one FP8 encoding: its APFloat semantics, which are the reference, and the runtime's two conversions for it
struct Encoding {
    std::string name;
    const llvm::fltSemantics& semantics;
    float (*widen)(uint8_t);
    uint8_t (*narrow)(double);
};

static std::vector<Encoding> encodings() {
    return {
        {"E4M3", llvm::APFloat::Float8E4M3FN(), cheri_e4m3_widen, cheri_e4m3_narrow},
        {"E5M2", llvm::APFloat::Float8E5M2(), cheri_e5m2_widen, cheri_e5m2_narrow},
    };
}

// the value a byte stands for in an encoding, according to APFloat
static llvm::APFloat reference(const Encoding& encoding, unsigned byte) {
    return llvm::APFloat(encoding.semantics, llvm::APInt(8, byte));
}

// the byte APFloat rounds a double to in an encoding, to nearest even
static uint8_t referenceNarrow(const Encoding& encoding, double value) {
    llvm::APFloat rounded(value);
    bool losesInfo;
    rounded.convert(encoding.semantics, llvm::APFloat::rmNearestTiesToEven, &losesInfo);
    return static_cast<uint8_t>(rounded.bitcastToAPInt().getZExtValue());
}

// checks that narrowing one double gives APFloat's byte, where any NaN is as good as any other
static void checkNarrow(const Encoding& encoding, double value) {
    uint8_t expected = referenceNarrow(encoding, value);
    uint8_t actual = encoding.narrow(value);
    INFO(encoding.name << " narrowing " << value << " gave " << int(actual) << ", expected " << int(expected));
    if (reference(encoding, expected).isNaN()) {
        CHECK(reference(encoding, actual).isNaN());
    } else {
        CHECK(actual == expected);
    }
}

// checks that every byte widens to exactly the float APFloat says it stands for, signed zeros included, and every NaN to a NaN
TEST_CASE("Runtime: FP8 widening matches APFloat for every byte", "[runtime]") {
    for (const Encoding& encoding : encodings()) {
        for (unsigned byte = 0; byte < 256; byte++) {
            llvm::APFloat expected = reference(encoding, byte);
            float actual = encoding.widen(static_cast<uint8_t>(byte));
            INFO(encoding.name << " widening byte " << byte);
            if (expected.isNaN()) {
                CHECK(std::isnan(actual));
                continue;
            }
            float wanted = expected.convertToFloat();
            CHECK(std::memcmp(&actual, &wanted, sizeof(float)) == 0);
        }
    }
}

// checks that narrowing rounds to nearest even everywhere it matters: every value itself, every midpoint between neighbors and just either side of it, past the largest value, and across many exponents
TEST_CASE("Runtime: FP8 narrowing matches APFloat", "[runtime]") {
    for (const Encoding& encoding : encodings()) {
        // every finite non-negative value the encoding holds, in order
        std::vector<double> values;
        for (unsigned byte = 0; byte < 128; byte++) {
            llvm::APFloat value = reference(encoding, byte);
            if (value.isFinite()) values.push_back(value.convertToDouble());
        }
        std::sort(values.begin(), values.end());
        // the midpoint past the largest value, where E4M3 ties back to its largest value and E5M2 ties up to infinity
        double largest = values.back();
        values.push_back(largest + (largest - values[values.size() - 2]) / 2);
        std::vector<double> candidates;
        for (size_t i = 0; i < values.size(); i++) {
            candidates.push_back(values[i]);
            if (i + 1 == values.size()) continue;
            double midpoint = (values[i] + values[i + 1]) / 2;
            candidates.push_back(midpoint);
            candidates.push_back(std::nextafter(midpoint, 0.0));
            candidates.push_back(std::nextafter(midpoint, INFINITY));
        }
        candidates.push_back(1e-30);
        candidates.push_back(1e30);
        candidates.push_back(INFINITY);
        candidates.push_back(NAN);
        // pseudo-random values with exponents from far below the smallest subnormal to far past the largest value
        uint64_t state = 0x9E3779B97F4A7C15u;
        for (int i = 0; i < 2000; i++) {
            state = state * 6364136223846793005u + 1442695040888963407u;
            double fraction = static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
            candidates.push_back(std::ldexp(1 + fraction, static_cast<int>(state % 60) - 30));
        }
        for (double candidate : candidates) {
            checkNarrow(encoding, candidate);
            checkNarrow(encoding, -candidate);
        }
    }
}
