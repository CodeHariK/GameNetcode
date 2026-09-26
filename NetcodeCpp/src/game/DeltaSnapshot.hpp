#pragma once

#include "game/GameTypes.hpp"
#include "net/BitStream.hpp"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace game {

/**
 * Delta snapshot codec (Milestone 6).
 *
 * Instead of memcpy-ing every EntityState into every packet, the server encodes
 * each world snapshot as a bit-packed delta against a *baseline* snapshot the
 * client has already acknowledged (Quake3 / Source-style delta compression):
 *
 *   - Fields that are unchanged from the baseline cost a single "changed" bit.
 *   - Changed fields are quantized and packed at their natural bit width, not as
 *     full 32-bit floats.
 *   - The full 24-bit color is almost always unchanged, so it collapses to 1 bit.
 *
 * The baseline_tick travels in the header so the client knows which stored
 * snapshot to reconstruct against; baseline_tick == 0 means an absolute
 * ("keyframe") encode with no baseline, which any client can always decode.
 */
struct SnapshotWireHeader {
    uint32_t server_tick{0};
    uint32_t last_client_input_tick{0};
    uint32_t baseline_tick{0};  // 0 == absolute encode (no baseline)
};

// --- Wire quantization budget -------------------------------------------------
// Positions span the world box; velocities are capped a little above MAX_SPEED.
// 16 bits over a 200-unit span gives ~0.003 unit resolution, far finer than the
// 0.05 reconciliation threshold, so quantization never triggers false rollbacks.
namespace delta {
inline constexpr float POS_MIN = -100.0f;
inline constexpr float POS_MAX = 100.0f;
inline constexpr int POS_BITS = 16;

inline constexpr float VEL_MIN = -25.0f;
inline constexpr float VEL_MAX = 25.0f;
inline constexpr int VEL_BITS = 16;

inline constexpr int COLOR_BITS = 24;

inline constexpr uint32_t MAX_ENTITIES = 1024;
inline constexpr uint32_t MAX_ENTITY_ID = 65535;
}  // namespace delta

/**
 * Encodes `current` as a delta against `baseline` and returns the packed bytes.
 * Pass an empty `baseline` (and baseline_tick 0 in the header) for an absolute
 * keyframe. Entities present in the baseline but absent from `current` are simply
 * omitted, which the decoder treats as removals.
 */
inline std::vector<uint8_t> encode_delta_snapshot(const SnapshotWireHeader& header,
                                                  const std::vector<EntityState>& current,
                                                  const std::vector<EntityState>& baseline) {
    std::unordered_map<uint32_t, const EntityState*> base_map;
    base_map.reserve(baseline.size());
    for (const auto& e : baseline) base_map[e.entity_id] = &e;

    netcode::BitWriter w;
    w.write_bits(header.server_tick, 32);
    w.write_bits(header.last_client_input_tick, 32);
    w.write_bits(header.baseline_tick, 32);
    w.write_ranged(static_cast<uint32_t>(current.size()), 0, delta::MAX_ENTITIES);

    for (const auto& cur : current) {
        w.write_ranged(cur.entity_id, 0, delta::MAX_ENTITY_ID);

        // Baseline defaults to zeroes for a brand-new entity.
        EntityState base{};
        auto it = base_map.find(cur.entity_id);
        const bool has_base = (it != base_map.end());
        if (has_base) base = *it->second;

        // Per-field changed bit, then the value only when it actually changed.
        const bool px = cur.position.x != base.position.x;
        const bool py = cur.position.y != base.position.y;
        const bool vx = cur.velocity.x != base.velocity.x;
        const bool vy = cur.velocity.y != base.velocity.y;
        const bool col = cur.color_rgb != base.color_rgb;

        w.write_bool(px);
        if (px) w.write_float(cur.position.x, delta::POS_MIN, delta::POS_MAX, delta::POS_BITS);
        w.write_bool(py);
        if (py) w.write_float(cur.position.y, delta::POS_MIN, delta::POS_MAX, delta::POS_BITS);
        w.write_bool(vx);
        if (vx) w.write_float(cur.velocity.x, delta::VEL_MIN, delta::VEL_MAX, delta::VEL_BITS);
        w.write_bool(vy);
        if (vy) w.write_float(cur.velocity.y, delta::VEL_MIN, delta::VEL_MAX, delta::VEL_BITS);
        w.write_bool(col);
        if (col) w.write_bits(cur.color_rgb, delta::COLOR_BITS);
    }

    w.flush();
    return w.buffer();
}

/**
 * Decodes a packed delta snapshot, reconstructing the full entity set by applying
 * changed fields on top of `baseline`. Returns false if the buffer is malformed
 * or truncated (the reader overflowed), in which case the client should drop the
 * snapshot and keep acknowledging its last good baseline.
 */
inline bool decode_delta_snapshot(const uint8_t* data,
                                  size_t size,
                                  const std::vector<EntityState>& baseline,
                                  SnapshotWireHeader& out_header,
                                  std::vector<EntityState>& out_current) {
    std::unordered_map<uint32_t, const EntityState*> base_map;
    base_map.reserve(baseline.size());
    for (const auto& e : baseline) base_map[e.entity_id] = &e;

    netcode::BitReader r(data, size);
    out_header.server_tick = r.read_bits(32);
    out_header.last_client_input_tick = r.read_bits(32);
    out_header.baseline_tick = r.read_bits(32);

    const uint32_t count = r.read_ranged(0, delta::MAX_ENTITIES);
    out_current.clear();
    out_current.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entity_id = r.read_ranged(0, delta::MAX_ENTITY_ID);

        EntityState e{};
        e.entity_id = entity_id;
        auto it = base_map.find(entity_id);
        if (it != base_map.end()) e = *it->second;
        e.entity_id = entity_id;

        if (r.read_bool())
            e.position.x = r.read_float(delta::POS_MIN, delta::POS_MAX, delta::POS_BITS);
        if (r.read_bool())
            e.position.y = r.read_float(delta::POS_MIN, delta::POS_MAX, delta::POS_BITS);
        if (r.read_bool())
            e.velocity.x = r.read_float(delta::VEL_MIN, delta::VEL_MAX, delta::VEL_BITS);
        if (r.read_bool())
            e.velocity.y = r.read_float(delta::VEL_MIN, delta::VEL_MAX, delta::VEL_BITS);
        if (r.read_bool()) e.color_rgb = r.read_bits(delta::COLOR_BITS);

        out_current.push_back(e);
    }

    return !r.overflowed();
}

}  // namespace game
