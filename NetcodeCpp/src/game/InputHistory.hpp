#pragma once

#include "game/GameTypes.hpp"
#include "game/Simulation.hpp"
#include <deque>
#include <vector>
#include <cmath>
#include <cstdint>

namespace game {

struct SavedInput {
    PlayerInput input;
    EntityState predicted_state;
};

/**
 * InputHistory:
 * Used on the client to store past inputs and corresponding predicted states.
 * Enables:
 *   1. Zero-latency local client prediction.
 *   2. Redundant input transmission to server (survives packet loss).
 *   3. Rollback & Reconciliation when server state diverges.
 */
class InputHistory {
public:
    explicit InputHistory(size_t max_history = 256) : max_history_(max_history) {}

    // Record an input and the resulting predicted state
    void record_input(const PlayerInput& input, const EntityState& predicted_state) {
        history_.push_back(SavedInput{input, predicted_state});
        if (history_.size() > max_history_) {
            history_.pop_front();
        }
    }

    // Discard inputs that have been acknowledged by the server (tick <= ack_tick)
    void discard_acknowledged(uint32_t ack_tick) {
        while (!history_.empty() && history_.front().input.tick <= ack_tick) {
            history_.pop_front();
        }
    }

    // Get the predicted state that was recorded for a given tick, if still in history
    [[nodiscard]] const SavedInput* find_input(uint32_t tick) const {
        for (const auto& item : history_) {
            if (item.input.tick == tick) {
                return &item;
            }
        }
        return nullptr;
    }

    // Check if the server authoritative state at ack_tick diverges from what client predicted
    [[nodiscard]] bool has_diverged(uint32_t ack_tick,
                                    const EntityState& server_state,
                                    float epsilon = 0.05f) const {
        const SavedInput* saved = find_input(ack_tick);
        if (!saved) {
            // Not in history (too old or already discarded), cannot verify
            return false;
        }

        float dx = saved->predicted_state.position.x - server_state.position.x;
        float dy = saved->predicted_state.position.y - server_state.position.y;
        float dist_sq = dx * dx + dy * dy;

        return dist_sq > (epsilon * epsilon);
    }

    /**
     * Performs Server Reconciliation:
     * 1. Snaps starting state to server_state at ack_tick.
     * 2. Re-simulates all pending local inputs where input.tick > ack_tick.
     * 3. Updates all stored predicted states and writes final reconciled state.
     * Returns the number of inputs replayed.
     */
    size_t reconcile(uint32_t ack_tick,
                     const EntityState& server_state,
                     float dt,
                     EntityState& out_reconciled_state) {
        // Discard acknowledged inputs
        discard_acknowledged(ack_tick);

        // Start from server ground truth
        EntityState current = server_state;

        size_t replayed_count = 0;
        for (auto& item : history_) {
            simulate_player(current, item.input, dt);
            item.predicted_state = current;
            replayed_count++;
        }

        out_reconciled_state = current;
        return replayed_count;
    }

    // Extract the most recent N inputs to send redundantly over UDP
    std::vector<PlayerInput> get_recent_inputs(size_t max_count = 5) const {
        std::vector<PlayerInput> result;
        if (history_.empty()) return result;

        size_t count = std::min(max_count, history_.size());
        size_t start_idx = history_.size() - count;

        for (size_t i = start_idx; i < history_.size(); ++i) {
            result.push_back(history_[i].input);
        }
        return result;
    }

    [[nodiscard]] size_t pending_count() const { return history_.size(); }
    [[nodiscard]] bool empty() const { return history_.empty(); }

    void clear() { history_.clear(); }

private:
    size_t max_history_{256};
    std::deque<SavedInput> history_;
};

}  // namespace game
