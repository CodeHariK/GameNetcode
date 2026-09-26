#pragma once

#include <cstdint>

namespace game {

// Static channel assignment (see net/ channels).
enum Channels : uint8_t {
    CH_UNRELIABLE = 0,  // unordered (effects, audio)
    CH_STATE = 1,       // sequenced (world snapshots, later also input)
    CH_RELIABLE = 2,    // ordered (chat, spawn/despawn, RPCs)
};

// 2D vector for basic movement / interpolation.
struct Vec2 {
    float x{0.0f};
    float y{0.0f};

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }

    static Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
};

#pragma pack(push, 1)
// One entity (e.g. a player or a scripted object) in the world.
struct EntityState {
    uint32_t entity_id{0};
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    uint32_t color_rgb{0xFFFFFF};
};

// Server -> Client: header preceding an array of EntityState in a snapshot.
struct SnapshotHeader {
    uint32_t server_tick{0};
    uint32_t entity_count{0};
};
#pragma pack(pop)

}  // namespace game
