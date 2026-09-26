#pragma once

#include "game/GameTypes.hpp"
#include <cmath>
#include <algorithm>

namespace game {

/**
 * Shared deterministic physics simulation logic.
 * MUST be executed identically on both Client (prediction & replay)
 * and Server (authoritative execution).
 */
inline void simulate_player(EntityState& state, const PlayerInput& input, float dt) {
    constexpr float MOVE_ACCELERATION = 80.0f;  // units / sec^2
    constexpr float MAX_SPEED = 20.0f;          // max units / sec
    constexpr float FRICTION = 8.0f;            // linear damping coefficient

    // 1. Normalize input direction vector to prevent diagonal speed boost
    float len = std::sqrt(input.move_x * input.move_x + input.move_y * input.move_y);
    float norm_x = 0.0f;
    float norm_y = 0.0f;
    if (len > 0.0001f) {
        norm_x = input.move_x / len;
        norm_y = input.move_y / len;
    }

    // 2. Apply acceleration
    state.velocity.x += norm_x * MOVE_ACCELERATION * dt;
    state.velocity.y += norm_y * MOVE_ACCELERATION * dt;

    // 3. Apply friction / damping
    state.velocity.x -= state.velocity.x * FRICTION * dt;
    state.velocity.y -= state.velocity.y * FRICTION * dt;

    // 4. Clamp max velocity
    float current_speed =
        std::sqrt(state.velocity.x * state.velocity.x + state.velocity.y * state.velocity.y);
    if (current_speed > MAX_SPEED) {
        state.velocity.x = (state.velocity.x / current_speed) * MAX_SPEED;
        state.velocity.y = (state.velocity.y / current_speed) * MAX_SPEED;
    }

    // 5. Integrate position: P = P + V * dt
    state.position.x += state.velocity.x * dt;
    state.position.y += state.velocity.y * dt;

    // 6. World boundary constraint (e.g. bounding box [-100, 100])
    constexpr float WORLD_BOUND = 100.0f;
    state.position.x = std::clamp(state.position.x, -WORLD_BOUND, WORLD_BOUND);
    state.position.y = std::clamp(state.position.y, -WORLD_BOUND, WORLD_BOUND);
}

}  // namespace game
