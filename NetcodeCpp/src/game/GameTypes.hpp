#pragma once

#include <cstdint>

namespace game {

// Reliable-channel message tags (1 byte prefix).
enum ReliableMsgType : uint8_t {
    MSG_FIRE = 1,  // Client -> Server FireCommand
    MSG_HIT = 2,   // Server -> Client HitNotification
};

// Static channel assignment (see net/ channels).
enum Channels : uint8_t {
    CH_UNRELIABLE = 0,
    CH_STATE = 1,
    CH_RELIABLE = 2,
};

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

struct PlayerInput {
    uint32_t tick{0};
    float move_x{0.0f};
    float move_y{0.0f};
    uint32_t buttons{0};
};

struct InputBatchHeader {
    uint32_t input_count{0};
    uint32_t ack_server_tick{0};  // newest snapshot tick the client decoded (delta baseline)
};

struct SnapshotHeader {
    uint32_t server_tick{0};
    uint32_t last_client_input_tick{0};
    uint32_t entity_count{0};
};

// Client -> Server: a hitscan shot. origin is the shooter's position; aim is the
// (un-normalized) aim direction; view_server_tick is the snapshot the shooter saw
// so the server can rewind the world to it (lag compensation).
struct FireCommand {
    uint32_t client_tick{0};
    uint32_t view_server_tick{0};
    float origin_x{0.0f};
    float origin_y{0.0f};
    float aim_x{0.0f};
    float aim_y{0.0f};
};

// Server -> Client: authoritative result of a FireCommand after rewinding.
struct HitNotification {
    uint32_t client_tick{0};
    uint8_t hit{0};
    uint32_t target_id{0};
    float point_x{0.0f};
    float point_y{0.0f};
};
#pragma pack(pop)

}  // namespace game
