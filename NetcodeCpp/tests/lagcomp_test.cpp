#include "game/GameTypes.hpp"
#include "game/LagCompensation.hpp"

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using game::EntityState;
using game::HitResult;
using game::Vec2;
using game::WorldHistory;

void test_ray_circle_basic() {
    float t = 0.0f;
    // Ray along +X from origin hits a circle centered at (10,0) r=1.
    assert(game::ray_circle_intersect({0, 0}, {1, 0}, {10, 0}, 1.0f, t));
    assert(std::fabs(t - 9.0f) < 1e-3f);

    // Parallel miss: same ray, circle offset in Y by 5.
    assert(!game::ray_circle_intersect({0, 0}, {1, 0}, {10, 5}, 1.0f, t));

    // Pointing away from the circle -> miss.
    assert(!game::ray_circle_intersect({0, 0}, {-1, 0}, {10, 0}, 1.0f, t));

    // Non-normalized direction still works (function normalizes internally).
    assert(game::ray_circle_intersect({0, 0}, {5, 0}, {10, 0}, 1.0f, t));
    assert(std::fabs(t - 9.0f) < 1e-3f);
    std::cout << "[PASS] Ray-circle intersection (hit distance, parallel & backward misses).\n";
}

void test_hitscan_nearest() {
    std::vector<EntityState> entities = {
        {1, {0, 0}, {0, 0}, 0x00FF00},   // shooter
        {2, {20, 0}, {0, 0}, 0xFF0000},  // far target
        {3, {10, 0}, {0, 0}, 0x0000FF},  // near target
    };
    HitResult r = game::hitscan({0, 0}, {1, 0}, entities, /*shooter*/ 1, 1.5f);
    assert(r.hit);
    assert(r.target_id == 3);  // nearest along the ray, not the far one
    std::cout << "[PASS] Hitscan returns the nearest target and skips the shooter.\n";
}

void test_history_interpolation() {
    WorldHistory hist;
    // Entity 1 moves +10 X per second, sampled at t = 0,1,2.
    hist.record(0.0, 0, {{1, {0, 0}, {10, 0}, 0}});
    hist.record(1.0, 60, {{1, {10, 0}, {10, 0}, 0}});
    hist.record(2.0, 120, {{1, {20, 0}, {10, 0}, 0}});

    std::vector<EntityState> out;
    assert(hist.sample(0.5, out));  // halfway between frame 0 and 1
    assert(std::fabs(out[0].position.x - 5.0f) < 1e-3f);

    assert(hist.sample(1.75, out));  // 3/4 between frame 1 and 2
    assert(std::fabs(out[0].position.x - 17.5f) < 1e-3f);

    // Out-of-range times clamp to the ends.
    assert(hist.sample(-5.0, out));
    assert(std::fabs(out[0].position.x - 0.0f) < 1e-3f);
    assert(hist.sample(99.0, out));
    assert(std::fabs(out[0].position.x - 20.0f) < 1e-3f);

    // Exact tick lookup.
    const auto* frame = hist.get_at_tick(60);
    assert(frame && std::fabs((*frame)[0].position.x - 10.0f) < 1e-3f);
    assert(hist.get_at_tick(999) == nullptr);
    std::cout << "[PASS] World history interpolation, clamping, and exact tick lookup.\n";
}

void test_rewind_makes_the_hit() {
    // A target sprints along +X. The shooter (at y=-10) fires straight up (+Y)
    // through x=0 at the moment the target was AT x=0 in the past. By the time the
    // shot reaches the server the target has moved far to the right.
    WorldHistory hist;
    for (uint32_t i = 0; i <= 20; ++i) {
        const double time = i * 0.1;        // 100ms per frame
        const float x = -10.0f + i * 1.0f;  // crosses x=0 at frame 10 (t=1.0)
        hist.record(time, i, {{7, {x, 0.0f}, {10.0f, 0.0f}, 0xFF00FF}});
    }

    const Vec2 shooter{0.0f, -10.0f};
    const Vec2 aim{0.0f, 1.0f};  // straight up the y-axis (the line x=0)
    const float radius = 1.0f;

    // PRESENT world: target is at x=+10, far from the x=0 ray -> MISS.
    std::vector<EntityState> present;
    assert(hist.sample(hist.newest_time(), present));
    assert(std::fabs(present[0].position.x - 10.0f) < 1e-3f);
    HitResult without_rewind = game::hitscan(shooter, aim, present, /*shooter*/ 999, radius);
    assert(!without_rewind.hit);

    // REWOUND to when the shooter saw the target (t = 1.0, target at x=0) -> HIT.
    std::vector<EntityState> rewound;
    assert(hist.sample(1.0, rewound));
    assert(std::fabs(rewound[0].position.x - 0.0f) < 1e-3f);
    HitResult with_rewind = game::hitscan(shooter, aim, rewound, /*shooter*/ 999, radius);
    assert(with_rewind.hit);
    assert(with_rewind.target_id == 7);

    std::cout << "[PASS] Shot MISSES against present world but HITS after rewind (lag comp).\n";
}

int main() {
    std::cout << ">>> Running Milestone 7 Lag Compensation Unit Tests <<<\n";
    test_ray_circle_basic();
    test_hitscan_nearest();
    test_history_interpolation();
    test_rewind_makes_the_hit();
    std::cout << ">>> ALL LAG COMPENSATION TESTS PASSED! <<<\n";
    return 0;
}
