#include "game/GameTypes.hpp"
#include "game/InterestManagement.hpp"

#include <iostream>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <vector>

using game::EntityState;

static bool contains(const std::vector<EntityState>& v, uint32_t id) {
    return std::any_of(v.begin(), v.end(), [id](const EntityState& e) {
        return e.entity_id == id;
    });
}

void test_radius_filtering() {
    std::vector<EntityState> world = {
        {1, {0, 0}, {0, 0}, 0},    // viewer
        {2, {10, 0}, {0, 0}, 0},   // inside r=15
        {3, {14, 0}, {0, 0}, 0},   // inside r=15
        {4, {100, 0}, {0, 0}, 0},  // far outside
        {5, {0, -12}, {0, 0}, 0},  // inside r=15
    };
    auto vis = game::compute_visible_set(/*viewer*/ 1, world, 15.0f);
    assert(contains(vis, 1));  // viewer always present
    assert(contains(vis, 2));
    assert(contains(vis, 3));
    assert(contains(vis, 5));
    assert(!contains(vis, 4));  // culled
    assert(vis.size() == 4);
    std::cout << "[PASS] Only in-range entities (plus the viewer) are visible.\n";
}

void test_viewer_always_included() {
    std::vector<EntityState> world = {
        {1, {0, 0}, {0, 0}, 0},      // viewer, alone
        {2, {500, 500}, {0, 0}, 0},  // far away
    };
    auto vis = game::compute_visible_set(1, world, 20.0f);
    assert(vis.size() == 1);
    assert(vis[0].entity_id == 1);  // viewer present even with nobody nearby
    std::cout << "[PASS] Viewer is always included, even when isolated.\n";
}

void test_missing_viewer_is_empty() {
    std::vector<EntityState> world = {
        {2, {0, 0}, {0, 0}, 0},
        {3, {1, 1}, {0, 0}, 0},
    };
    auto vis = game::compute_visible_set(/*viewer*/ 99, world, 50.0f);
    assert(vis.empty());
    std::cout << "[PASS] No viewpoint -> empty visible set.\n";
}

void test_nearest_n_cap() {
    std::vector<EntityState> world = {
        {1, {0, 0}, {0, 0}, 0},  // viewer
        {2, {5, 0}, {0, 0}, 0},  // nearest
        {3, {8, 0}, {0, 0}, 0},
        {4, {9, 0}, {0, 0}, 0},
        {5, {10, 0}, {0, 0}, 0},  // farthest (still in radius)
    };
    // Radius admits all 4, but cap to viewer + 2 nearest = 3 total.
    auto vis = game::compute_visible_set(1, world, 50.0f, /*max_count*/ 3);
    assert(vis.size() == 3);
    assert(contains(vis, 1));  // viewer
    assert(contains(vis, 2));  // nearest
    assert(contains(vis, 3));  // 2nd nearest
    assert(!contains(vis, 4));
    assert(!contains(vis, 5));
    std::cout << "[PASS] max_count keeps the viewer plus the nearest N-1 others.\n";
}

void test_boundary_inclusive() {
    std::vector<EntityState> world = {
        {1, {0, 0}, {0, 0}, 0},
        {2, {10, 0}, {0, 0}, 0},  // exactly on the radius
    };
    auto vis = game::compute_visible_set(1, world, 10.0f);
    assert(contains(vis, 2));  // radius is inclusive (<=)
    std::cout << "[PASS] Entity exactly on the radius is visible (inclusive bound).\n";
}

int main() {
    std::cout << ">>> Running Milestone 8 Interest Management Unit Tests <<<\n";
    test_radius_filtering();
    test_viewer_always_included();
    test_missing_viewer_is_empty();
    test_nearest_n_cap();
    test_boundary_inclusive();
    std::cout << ">>> ALL INTEREST MANAGEMENT TESTS PASSED! <<<\n";
    return 0;
}
