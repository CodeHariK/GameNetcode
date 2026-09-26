#include "net/BitStream.hpp"

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using netcode::BitReader;
using netcode::bits_required;
using netcode::BitWriter;

void test_bits_required() {
    assert(bits_required(0) == 1);
    assert(bits_required(1) == 1);
    assert(bits_required(2) == 2);
    assert(bits_required(3) == 2);
    assert(bits_required(255) == 8);
    assert(bits_required(256) == 9);
    assert(bits_required(1000) == 10);
    std::cout << "[PASS] bits_required computes minimal widths.\n";
}

void test_raw_bit_roundtrip() {
    BitWriter w;
    w.write_bits(0x1, 1);
    w.write_bits(0x2, 2);
    w.write_bits(0xA, 4);
    w.write_bits(0xDEADBEEF, 32);
    w.write_bits(0x7F, 7);
    w.flush();

    BitReader r(w.data(), w.size());
    assert(r.read_bits(1) == 0x1u);
    assert(r.read_bits(2) == 0x2u);
    assert(r.read_bits(4) == 0xAu);
    assert(r.read_bits(32) == 0xDEADBEEFu);
    assert(r.read_bits(7) == 0x7Fu);
    assert(!r.overflowed());
    std::cout << "[PASS] Arbitrary-width bit round-trip (incl. full 32-bit word).\n";
}

void test_bool_and_ranged() {
    BitWriter w;
    w.write_bool(true);
    w.write_bool(false);
    w.write_ranged(1000, 0, 1023);  // 10 bits
    w.write_ranged(42, 40, 50);     // range 10 -> 4 bits
    w.write_ranged(9999, 0, 8000);  // clamped to 8000
    w.flush();

    BitReader r(w.data(), w.size());
    assert(r.read_bool() == true);
    assert(r.read_bool() == false);
    assert(r.read_ranged(0, 1023) == 1000u);
    assert(r.read_ranged(40, 50) == 42u);
    assert(r.read_ranged(0, 8000) == 8000u);
    std::cout << "[PASS] Bool + ranged-int packing (with clamping) verified.\n";
}

void test_float_quantization() {
    BitWriter w;
    w.write_float(0.0f, -100.0f, 100.0f, 16);
    w.write_float(37.5f, -100.0f, 100.0f, 16);
    w.write_float(-99.9f, -100.0f, 100.0f, 16);
    w.write_float(200.0f, -100.0f, 100.0f, 16);  // clamped to 100
    w.flush();

    BitReader r(w.data(), w.size());
    const float eps = 200.0f / 65535.0f + 1e-4f;  // one quantization step
    assert(std::fabs(r.read_float(-100.0f, 100.0f, 16) - 0.0f) < eps);
    assert(std::fabs(r.read_float(-100.0f, 100.0f, 16) - 37.5f) < eps);
    assert(std::fabs(r.read_float(-100.0f, 100.0f, 16) - (-99.9f)) < eps);
    assert(std::fabs(r.read_float(-100.0f, 100.0f, 16) - 100.0f) < eps);
    std::cout << "[PASS] Quantized float round-trip within one step of precision.\n";
}

void test_compactness_and_overflow() {
    // 100 booleans must fit in ~13 bytes, not 100.
    BitWriter w;
    for (int i = 0; i < 100; ++i) w.write_bool(i % 3 == 0);
    w.flush();
    assert(w.size() <= 13);

    BitReader r(w.data(), w.size());
    for (int i = 0; i < 100; ++i) assert(r.read_bool() == (i % 3 == 0));
    assert(!r.overflowed());

    // Reading past the end flags overflow rather than crashing.
    r.read_bits(32);
    assert(r.overflowed());
    std::cout << "[PASS] 100 bools packed into <=13 bytes; over-read flags overflow.\n";
}

int main() {
    std::cout << ">>> Running Milestone 6 BitStream Unit Tests <<<\n";
    test_bits_required();
    test_raw_bit_roundtrip();
    test_bool_and_ranged();
    test_float_quantization();
    test_compactness_and_overflow();
    std::cout << ">>> ALL BITSTREAM TESTS PASSED! <<<\n";
    return 0;
}
