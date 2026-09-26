#include "game/GameTypes.hpp"
#include "game/DeltaSnapshot.hpp"

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using game::EntityState;
using game::SnapshotWireHeader;

static bool approx(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) < eps;
}

static bool entities_match(const EntityState& a, const EntityState& b) {
    return a.entity_id == b.entity_id && approx(a.position.x, b.position.x) &&
           approx(a.position.y, b.position.y) && approx(a.velocity.x, b.velocity.x) &&
           approx(a.velocity.y, b.velocity.y) && a.color_rgb == b.color_rgb;
}

void test_absolute_roundtrip() {
    std::vector<EntityState> current = {
        {1, {12.5f, -7.25f}, {1.0f, 0.5f}, 0x00FF00},
        {2, {-40.0f, 88.0f}, {-3.0f, 2.0f}, 0xFF0000},
    };

    SnapshotWireHeader hdr{100, 95, 0};
    auto packed = game::encode_delta_snapshot(hdr, current, {});

    SnapshotWireHeader out_hdr{};
    std::vector<EntityState> decoded;
    assert(game::decode_delta_snapshot(packed.data(), packed.size(), {}, out_hdr, decoded));

    assert(out_hdr.server_tick == 100);
    assert(out_hdr.last_client_input_tick == 95);
    assert(out_hdr.baseline_tick == 0);
    assert(decoded.size() == 2);
    assert(entities_match(decoded[0], current[0]));
    assert(entities_match(decoded[1], current[1]));
    std::cout << "[PASS] Absolute (keyframe) snapshot round-trip.\n";
}

void test_delta_roundtrip() {
    std::vector<EntityState> baseline = {
        {1, {10.0f, 10.0f}, {0.0f, 0.0f}, 0x00FF00},
        {2, {20.0f, 20.0f}, {0.0f, 0.0f}, 0xFF0000},
    };
    // Only entity 1 moves; colors and entity 2 unchanged.
    std::vector<EntityState> current = {
        {1, {10.5f, 10.0f}, {5.0f, 0.0f}, 0x00FF00},
        {2, {20.0f, 20.0f}, {0.0f, 0.0f}, 0xFF0000},
    };

    SnapshotWireHeader hdr{103, 100, 100};
    auto packed = game::encode_delta_snapshot(hdr, current, baseline);

    SnapshotWireHeader out_hdr{};
    std::vector<EntityState> decoded;
    assert(game::decode_delta_snapshot(packed.data(), packed.size(), baseline, out_hdr, decoded));

    assert(out_hdr.baseline_tick == 100);
    assert(decoded.size() == 2);
    assert(entities_match(decoded[0], current[0]));
    assert(entities_match(decoded[1], current[1]));
    std::cout << "[PASS] Baseline-relative delta round-trip.\n";
}

void test_new_and_removed_entities() {
    std::vector<EntityState> baseline = {
        {1, {1.0f, 1.0f}, {0.0f, 0.0f}, 0x00FF00},
        {2, {2.0f, 2.0f}, {0.0f, 0.0f}, 0xFF0000},
    };
    // Entity 2 removed (absent), entity 3 is brand new.
    std::vector<EntityState> current = {
        {1, {1.0f, 1.0f}, {0.0f, 0.0f}, 0x00FF00},
        {3, {5.0f, 6.0f}, {1.0f, 1.0f}, 0x0000FF},
    };

    SnapshotWireHeader hdr{106, 100, 100};
    auto packed = game::encode_delta_snapshot(hdr, current, baseline);

    SnapshotWireHeader out_hdr{};
    std::vector<EntityState> decoded;
    assert(game::decode_delta_snapshot(packed.data(), packed.size(), baseline, out_hdr, decoded));

    assert(decoded.size() == 2);
    assert(decoded[0].entity_id == 1);
    assert(decoded[1].entity_id == 3);
    assert(entities_match(decoded[1], current[1]));
    std::cout << "[PASS] New entities encoded fully; removed entities dropped.\n";
}

void test_compression_ratio() {
    // 32 entities, only a few moving, all sharing the baseline color.
    std::vector<EntityState> baseline;
    for (uint32_t i = 1; i <= 32; ++i) {
        baseline.push_back({i, {float(i), float(i)}, {0.0f, 0.0f}, 0x00FF00});
    }
    std::vector<EntityState> current = baseline;
    for (int i = 0; i < 4; ++i) {
        current[i].position.x += 0.5f;
        current[i].velocity.x = 5.0f;
    }

    SnapshotWireHeader hdr{200, 190, 190};
    auto delta_bytes = game::encode_delta_snapshot(hdr, current, baseline).size();

    // Raw = 12-byte header + N * sizeof(EntityState).
    const size_t raw_bytes = 12 + current.size() * sizeof(EntityState);
    assert(delta_bytes < raw_bytes);

    SnapshotWireHeader out_hdr{};
    std::vector<EntityState> decoded;
    assert(game::decode_delta_snapshot(game::encode_delta_snapshot(hdr, current, baseline).data(),
                                       delta_bytes,
                                       baseline,
                                       out_hdr,
                                       decoded));
    assert(decoded.size() == 32);

    std::cout << "[PASS] Delta " << delta_bytes << "B vs raw " << raw_bytes << "B ("
              << (100 - (delta_bytes * 100 / raw_bytes)) << "% smaller).\n";
}

void test_truncated_buffer_fails_safely() {
    std::vector<EntityState> current = {
        {1, {12.5f, -7.25f}, {1.0f, 0.5f}, 0x00FF00},
    };
    SnapshotWireHeader hdr{100, 95, 0};
    auto packed = game::encode_delta_snapshot(hdr, current, {});

    SnapshotWireHeader out_hdr{};
    std::vector<EntityState> decoded;
    // Chop the buffer in half: decode must report failure, not crash.
    bool ok = game::decode_delta_snapshot(packed.data(), packed.size() / 2, {}, out_hdr, decoded);
    assert(!ok);
    std::cout << "[PASS] Truncated buffer is rejected safely (overflow detected).\n";
}

int main() {
    std::cout << ">>> Running Milestone 6 Delta Compression Unit Tests <<<\n";
    test_absolute_roundtrip();
    test_delta_roundtrip();
    test_new_and_removed_entities();
    test_compression_ratio();
    test_truncated_buffer_fails_safely();
    std::cout << ">>> ALL DELTA TESTS PASSED! <<<\n";
    return 0;
}
