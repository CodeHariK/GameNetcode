#pragma once

#include "game/GameTypes.hpp"

#include <cstdint>
#include <cmath>
#include <deque>
#include <vector>

namespace game {

/**
 * Lag compensation (Milestone 7).
 *
 * A client sees remote entities in the past: its view is delayed by the
 * interpolation buffer plus network latency. If the server hit-tested against the
 * *present* world, players would have to lead their shots and "obvious" hits would
 * miss. Instead the server records a short history of every entity's position and,
 * when a shot arrives, rewinds the world to what the shooter actually saw before
 * running the hit test (the Valve / Source "what you see is what you get" model).
 */

/**
 * Ray vs. circle (2D hitbox) intersection. `dir` need not be normalized. On a hit
 * returns true and writes the forward distance along the ray to the first
 * intersection into out_t; intersections strictly behind the origin are ignored.
 */
inline bool ray_circle_intersect(const Vec2& origin,
                                 const Vec2& dir,
                                 const Vec2& center,
                                 float radius,
                                 float& out_t) {
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 1e-6f) return false;
    const Vec2 d{dir.x / len, dir.y / len};

    const Vec2 m{origin.x - center.x, origin.y - center.y};
    const float b = m.x * d.x + m.y * d.y;
    const float c = (m.x * m.x + m.y * m.y) - radius * radius;

    // Origin outside the circle and pointing away -> no hit.
    if (c > 0.0f && b > 0.0f) return false;

    const float disc = b * b - c;
    if (disc < 0.0f) return false;

    const float sqrt_disc = std::sqrt(disc);
    float t = -b - sqrt_disc;          // near intersection
    if (t < 0.0f) t = -b + sqrt_disc;  // origin inside the circle
    if (t < 0.0f) return false;

    out_t = t;
    return true;
}

/**
 * Fires a hitscan ray through `entities`, returning the nearest entity struck
 * (excluding shooter_id). A miss leaves hit == false and target_id == 0.
 */
struct HitResult {
    bool hit{false};
    uint32_t target_id{0};
    Vec2 point{0.0f, 0.0f};
    float distance{0.0f};
};

inline HitResult hitscan(const Vec2& origin,
                         const Vec2& aim,
                         const std::vector<EntityState>& entities,
                         uint32_t shooter_id,
                         float hit_radius) {
    HitResult best{};
    float best_t = 0.0f;
    for (const auto& e : entities) {
        if (e.entity_id == shooter_id) continue;
        float t = 0.0f;
        if (ray_circle_intersect(origin, aim, e.position, hit_radius, t)) {
            if (!best.hit || t < best_t) {
                const float len = std::sqrt(aim.x * aim.x + aim.y * aim.y);
                const Vec2 d = (len > 1e-6f) ? Vec2{aim.x / len, aim.y / len} : Vec2{0.0f, 0.0f};
                best.hit = true;
                best.target_id = e.entity_id;
                best.point = {origin.x + d.x * t, origin.y + d.y * t};
                best.distance = t;
                best_t = t;
            }
        }
    }
    return best;
}

/**
 * WorldHistory: a bounded, time-stamped ring of world states used as the rewind
 * buffer. The server records one frame per simulation tick; a shot then samples
 * the world at the moment the shooter saw it, either interpolated by wall-clock
 * time (sample) or fetched exactly by tick (get_at_tick).
 */
class WorldHistory {
public:
    explicit WorldHistory(size_t max_frames = 128) : max_frames_(max_frames) {}

    void record(double time, uint32_t tick, const std::vector<EntityState>& entities) {
        frames_.push_back(Frame{time, tick, entities});
        while (frames_.size() > max_frames_) frames_.pop_front();
    }

    [[nodiscard]] bool empty() const { return frames_.empty(); }
    [[nodiscard]] double oldest_time() const { return frames_.front().time; }
    [[nodiscard]] double newest_time() const { return frames_.back().time; }

    // Exact world recorded at a given tick, or nullptr if it has aged out.
    [[nodiscard]] const std::vector<EntityState>* get_at_tick(uint32_t tick) const {
        for (const auto& f : frames_) {
            if (f.tick == tick) return &f.entities;
        }
        return nullptr;
    }

    /**
     * Reconstructs the world at wall-clock `time` by linearly interpolating entity
     * positions/velocities between the two bracketing frames (clamped to the ends
     * of the buffer). Returns false only when the history is empty.
     */
    bool sample(double time, std::vector<EntityState>& out) const {
        if (frames_.empty()) return false;

        if (time <= frames_.front().time) {
            out = frames_.front().entities;
            return true;
        }
        if (time >= frames_.back().time) {
            out = frames_.back().entities;
            return true;
        }

        size_t hi = 0;
        while (hi < frames_.size() && frames_[hi].time < time) ++hi;
        const Frame& f1 = frames_[hi];
        const Frame& f0 = frames_[hi - 1];

        const double span = f1.time - f0.time;
        const float t = (span > 1e-9) ? static_cast<float>((time - f0.time) / span) : 0.0f;

        out.clear();
        out.reserve(f0.entities.size());
        for (const auto& a : f0.entities) {
            EntityState e = a;
            for (const auto& b : f1.entities) {
                if (b.entity_id == a.entity_id) {
                    e.position = Vec2::lerp(a.position, b.position, t);
                    e.velocity = Vec2::lerp(a.velocity, b.velocity, t);
                    break;
                }
            }
            out.push_back(e);
        }
        return true;
    }

private:
    struct Frame {
        double time{0.0};
        uint32_t tick{0};
        std::vector<EntityState> entities;
    };

    size_t max_frames_{128};
    std::deque<Frame> frames_;
};

}  // namespace game
