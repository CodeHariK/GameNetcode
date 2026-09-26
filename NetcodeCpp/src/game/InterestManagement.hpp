#pragma once

#include "game/GameTypes.hpp"

#include <cstdint>
#include <algorithm>
#include <vector>

namespace game {

/**
 * Interest Management / Area of Interest (Milestone 8).
 *
 * A server does not need to tell a client about entities it cannot perceive. By
 * sending each client only the entities inside its Area of Interest (a radius
 * around the player), snapshot bandwidth scales with what a player can actually
 * see rather than with the total size of the world. This is the standard way MMO
 * and large-session shooters stay within budget, and it stacks on top of delta
 * compression: fewer entities per snapshot, each one delta-encoded.
 *
 * The viewer's own entity is always included so client prediction/reconciliation
 * keeps working. An optional max_count caps the set to the nearest N entities
 * (a hard bandwidth ceiling regardless of how crowded a spot gets).
 */
inline float distance_sq(const Vec2& a, const Vec2& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/**
 * Returns the subset of `entities` visible to `viewer_id`: the viewer itself plus
 * every other entity whose center is within `radius` of the viewer. When
 * max_count > 0 the result is limited to the viewer plus the nearest (max_count-1)
 * others. If the viewer is not present, an empty set is returned (nothing to see
 * from a viewpoint that does not exist).
 */
inline std::vector<EntityState> compute_visible_set(uint32_t viewer_id,
                                                    const std::vector<EntityState>& entities,
                                                    float radius,
                                                    size_t max_count = 0) {
    const EntityState* viewer = nullptr;
    for (const auto& e : entities) {
        if (e.entity_id == viewer_id) {
            viewer = &e;
            break;
        }
    }

    std::vector<EntityState> visible;
    if (!viewer) return visible;

    const float radius_sq = radius * radius;

    // Collect in-range others with their distance so we can rank if capped.
    std::vector<std::pair<float, const EntityState*>> candidates;
    for (const auto& e : entities) {
        if (e.entity_id == viewer_id) continue;
        const float d2 = distance_sq(viewer->position, e.position);
        if (d2 <= radius_sq) candidates.emplace_back(d2, &e);
    }

    if (max_count > 0 && candidates.size() + 1 > max_count) {
        const size_t keep = max_count - 1;  // reserve one slot for the viewer
        std::partial_sort(candidates.begin(),
                          candidates.begin() + keep,
                          candidates.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
        candidates.resize(keep);
    }

    visible.reserve(candidates.size() + 1);
    visible.push_back(*viewer);  // viewer first, always present
    for (const auto& [d2, e] : candidates) {
        (void)d2;
        visible.push_back(*e);
    }
    return visible;
}

}  // namespace game
