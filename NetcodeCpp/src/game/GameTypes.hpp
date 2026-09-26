#pragma once

#include <cstdint>

namespace game {

// Static channel assignment (see net/ channels).
enum Channels : uint8_t {
    CH_UNRELIABLE = 0,  // unordered (effects, audio)
    CH_STATE = 1,       // sequenced (snapshots down, input up)
    CH_RELIABLE = 2,    // ordered (chat, spawn/despawn, RPCs)
};

// 2D vector for movement / interpolation.
struct Vec2 {
    float x{0.0f};
    float y{0.0f};

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }

    static Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
};

#pragma pack(push, 1)
struct EntityState {
    uint32_t entity_id{0};
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    uint32_t color_rgb{0xFFFFFF};
};

// Client -> Server: one player input for a given client tick.
struct PlayerInput {
    uint32_t tick{0};
    float move_x{0.0f};
    float move_y{0.0f};
    uint32_t buttons{0};
};

// Header preceding an array of redundant PlayerInputs in a datagram.
struct InputBatchHeader {
    uint32_t input_count{0};
    uint32_t ack_server_tick{0};  // newest snapshot tick the client decoded (delta baseline)
};

// Server -> Client: header before an array of EntityState. last_client_input_tick
// tells the client which of its inputs the server has processed (for reconciliation).
struct SnapshotHeader {
    uint32_t server_tick{0};
    uint32_t last_client_input_tick{0};
    uint32_t entity_count{0};
};
#pragma pack(pop)

}  // namespace game
