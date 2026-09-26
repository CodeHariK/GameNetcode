#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cmath>
#include <algorithm>

namespace netcode {

/**
 * Returns the number of bits required to represent every value in the inclusive
 * range [0, max_value]. bits_required(0) == 1 so a single value still costs a
 * bit (keeps encode/decode symmetric and avoids zero-width fields).
 */
inline int bits_required(uint32_t max_value) {
    int bits = 1;
    while (max_value >>= 1) ++bits;
    return bits;
}

/**
 * BitWriter: packs values at arbitrary bit widths into a growable byte buffer.
 *
 * Bits are emitted least-significant-first into a 64-bit scratch accumulator and
 * flushed one byte at a time, so the wire layout is little-endian at the bit
 * level. A matching BitReader pulls the same widths back out in the same order.
 * Writing fewer bits than a full byte is the whole point: a boolean costs 1 bit,
 * a value in [0, 1000] costs 10 bits instead of a 32-bit int, etc.
 */
class BitWriter {
public:
    // Writes the low `bits` (1..32) of `value`. Higher bits of value are masked off.
    void write_bits(uint32_t value, int bits) {
        if (bits <= 0) return;
        if (bits > 32) bits = 32;

        const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1ull);
        scratch_ |= (static_cast<uint64_t>(value) & mask) << scratch_bits_;
        scratch_bits_ += bits;

        while (scratch_bits_ >= 8) {
            buffer_.push_back(static_cast<uint8_t>(scratch_ & 0xFF));
            scratch_ >>= 8;
            scratch_bits_ -= 8;
        }
    }

    void write_bool(bool value) { write_bits(value ? 1u : 0u, 1); }

    // Writes `value` (clamped to [min, max]) using only as many bits as the range needs.
    void write_ranged(uint32_t value, uint32_t min, uint32_t max) {
        if (max < min) return;
        value = std::clamp(value, min, max);
        write_bits(value - min, bits_required(max - min));
    }

    // Quantizes a float in [min, max] to `bits` of precision and writes it.
    void write_float(float value, float min, float max, int bits) {
        const float clamped = std::clamp(value, min, max);
        const uint32_t levels = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
        const float normalized = (max > min) ? (clamped - min) / (max - min) : 0.0f;
        const uint32_t quantized =
            static_cast<uint32_t>(std::lround(normalized * static_cast<double>(levels)));
        write_bits(quantized, bits);
    }

    // Emits any buffered partial byte. Call once when the message is complete.
    void flush() {
        if (scratch_bits_ > 0) {
            buffer_.push_back(static_cast<uint8_t>(scratch_ & 0xFF));
            scratch_ = 0;
            scratch_bits_ = 0;
        }
    }

    [[nodiscard]] const std::vector<uint8_t>& buffer() const { return buffer_; }
    [[nodiscard]] const uint8_t* data() const { return buffer_.data(); }
    [[nodiscard]] size_t size() const { return buffer_.size(); }

private:
    std::vector<uint8_t> buffer_;
    uint64_t scratch_{0};
    int scratch_bits_{0};
};

/**
 * BitReader: the inverse of BitWriter. Reads values back in the exact order and
 * bit widths they were written. If a read runs past the end of the buffer the
 * reader is marked overflowed() and subsequent reads return 0, so a truncated or
 * malformed datagram fails safely instead of reading garbage.
 */
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    uint32_t read_bits(int bits) {
        if (bits <= 0) return 0;
        if (bits > 32) bits = 32;

        while (scratch_bits_ < bits) {
            uint8_t next = 0;
            if (byte_pos_ < size_) {
                next = data_[byte_pos_];
            } else {
                overflowed_ = true;
            }
            scratch_ |= static_cast<uint64_t>(next) << scratch_bits_;
            ++byte_pos_;
            scratch_bits_ += 8;
        }

        const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1ull);
        const uint32_t result = static_cast<uint32_t>(scratch_ & mask);
        scratch_ >>= bits;
        scratch_bits_ -= bits;
        return result;
    }

    bool read_bool() { return read_bits(1) != 0; }

    uint32_t read_ranged(uint32_t min, uint32_t max) {
        if (max < min) return min;
        return min + read_bits(bits_required(max - min));
    }

    float read_float(float min, float max, int bits) {
        const uint32_t levels = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
        const uint32_t quantized = read_bits(bits);
        const float normalized =
            (levels > 0) ? static_cast<float>(quantized) / static_cast<float>(levels) : 0.0f;
        return min + normalized * (max - min);
    }

    [[nodiscard]] bool overflowed() const { return overflowed_; }

private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t byte_pos_{0};
    uint64_t scratch_{0};
    int scratch_bits_{0};
    bool overflowed_{false};
};

}  // namespace netcode
