#pragma once

#include "game/GameTypes.hpp"
#include <vector>
#include <map>
#include <cassert>
#include <cmath>
#include <algorithm>

namespace game {

struct WorldSnapshot {
    uint32_t server_tick;
    double received_time;
    std::vector<EntityState> entities;
};

/**
 * SnapshotBuffer:
 * Stores recent incoming world snapshots from the server.
 * Handles jitter buffering and provides the older/newer snapshots
 * required to interpolate rendering smoothly.
 */
class SnapshotBuffer {
public:
    explicit SnapshotBuffer(
        double interpolation_delay = 0.100)  // 100ms default interpolation delay
        : interpolation_delay_(interpolation_delay) {}

    // Adds a newly received snapshot to the buffer, dropping old data
    void add_snapshot(const SnapshotHeader& header,
                      const EntityState* states,
                      double current_time) {
        // Discard old or highly out-of-order snapshots (though Channel 1 Sequenced inherently
        // protects against this)
        if (!snapshots_.empty() && header.server_tick <= snapshots_.back().server_tick) {
            return;
        }

        WorldSnapshot snap;
        snap.server_tick = header.server_tick;
        snap.received_time = current_time;
        snap.entities.assign(states, states + header.entity_count);

        snapshots_.push_back(std::move(snap));

        // Keep buffer size bounded (e.g. at 20Hz, 64 is ~3.2 seconds of history)
        if (snapshots_.size() > 64) {
            snapshots_.erase(snapshots_.begin());
        }
    }

    // Calculates interpolated position of all entities at (current_time - interpolation_delay)
    // Returns true if interpolation was possible, false if not enough data or stalled
    bool get_interpolated_state(double current_time, std::map<uint32_t, EntityState>& out_state) {
        if (snapshots_.size() < 2) return false;

        // Target rendering time is safely in the past
        double render_time = current_time - interpolation_delay_;

        // Find the two surrounding snapshots in history: older (snap1) and newer (snap2)
        const WorldSnapshot* s1 = nullptr;
        const WorldSnapshot* s2 = nullptr;

        for (size_t i = 1; i < snapshots_.size(); ++i) {
            if (snapshots_[i].received_time >= render_time) {
                s1 = &snapshots_[i - 1];
                s2 = &snapshots_[i];
                break;
            }
        }

        // Extrapolate if render_time is newer than our newest received packet (network stall / lag
        // spike)
        if (!s1 || !s2) {
            s1 = &snapshots_[snapshots_.size() - 2];
            s2 = &snapshots_[snapshots_.size() - 1];
            // We could extrapolate here, but for safety in robust systems, we just clamp to latest
            render_time = std::min(render_time, s2->received_time);
        }

        // Calculate interpolation factor (0.0 to 1.0)
        double range = s2->received_time - s1->received_time;
        double t = 0.0;
        if (range > 0.0001) {
            t = (render_time - s1->received_time) / range;
            t = std::max(0.0, std::min(1.0, t));
        }

        // Map s2 entities for fast matching
        std::map<uint32_t, const EntityState*> s2_map;
        for (const auto& e2 : s2->entities) {
            s2_map[e2.entity_id] = &e2;
        }

        // Perform linear interpolation (LERP) on positions
        out_state.clear();
        for (const auto& e1 : s1->entities) {
            EntityState interp = e1;
            auto it = s2_map.find(e1.entity_id);
            if (it != s2_map.end()) {
                const EntityState* e2 = it->second;
                interp.position = Vec2::lerp(e1.position, e2->position, static_cast<float>(t));
                interp.velocity = Vec2::lerp(e1.velocity, e2->velocity, static_cast<float>(t));
            }
            out_state[interp.entity_id] = interp;
        }

        return true;
    }

private:
    double interpolation_delay_{0.100};  // seconds to look in the past
    std::vector<WorldSnapshot> snapshots_;
};

}  // namespace game
