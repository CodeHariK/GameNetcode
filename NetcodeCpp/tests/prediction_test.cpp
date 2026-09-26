#include "game/GameTypes.hpp"
#include "game/Simulation.hpp"
#include "game/InputHistory.hpp"

#include <iostream>
#include <cassert>
#include <cmath>

void test_simulation_determinism() {
    game::EntityState e1{1, {0.0f, 0.0f}, {0.0f, 0.0f}, 0xFF0000};
    game::EntityState e2{1, {0.0f, 0.0f}, {0.0f, 0.0f}, 0xFF0000};

    const float dt = 1.0f / 60.0f;

    for (uint32_t tick = 1; tick <= 100; ++tick) {
        game::PlayerInput in{tick, 1.0f, 0.5f, 0};
        game::simulate_player(e1, in, dt);
        game::simulate_player(e2, in, dt);
    }

    assert(e1.position.x == e2.position.x);
    assert(e1.position.y == e2.position.y);
    assert(e1.velocity.x == e2.velocity.x);
    assert(e1.velocity.y == e2.velocity.y);

    std::cout << "[PASS] Deterministic player physics simulation verified.\n";
}

void test_prediction_and_reconciliation() {
    const float dt = 1.0f / 60.0f;

    // Server authoritative entity
    game::EntityState server_entity{1, {0.0f, 0.0f}, {0.0f, 0.0f}, 0xFF0000};

    // Client predicted entity & history
    game::EntityState client_entity{1, {0.0f, 0.0f}, {0.0f, 0.0f}, 0xFF0000};
    game::InputHistory history;

    // Simulate 30 client ticks (Client predicts ahead of server)
    for (uint32_t tick = 1; tick <= 30; ++tick) {
        game::PlayerInput in{tick, 1.0f, 0.0f, 0};
        game::simulate_player(client_entity, in, dt);
        history.record_input(in, client_entity);
    }

    // Now server processes up to tick 20 (Server is 10 ticks behind due to latency)
    for (uint32_t tick = 1; tick <= 20; ++tick) {
        game::PlayerInput in{tick, 1.0f, 0.0f, 0};
        game::simulate_player(server_entity, in, dt);
    }

    // Client checks if server state at tick 20 diverged
    assert(!history.has_diverged(20, server_entity));
    std::cout << "[PASS] Client prediction matched server ground truth (no divergence).\n";

    // Now introduce an artificial server divergence at tick 20 (e.g. server bumped player or
    // corrected velocity)
    game::EntityState perturbed_server_entity = server_entity;
    perturbed_server_entity.position.x += 5.0f;  // Server pushed player 5 units to the right

    // Client detects divergence
    assert(history.has_diverged(20, perturbed_server_entity));
    std::cout << "[PASS] Client successfully detected server state divergence at tick 20.\n";

    // Client performs RECONCILIATION:
    // Snaps to tick 20 perturbed state and replays remaining ticks 21 through 30 (10 inputs)
    game::EntityState reconciled_state;
    size_t replayed = history.reconcile(20, perturbed_server_entity, dt, reconciled_state);
    assert(replayed == 10);

    // Verify ground truth: simulate server from perturbed tick 20 forward 10 more ticks
    game::EntityState ground_truth = perturbed_server_entity;
    for (uint32_t tick = 21; tick <= 30; ++tick) {
        game::PlayerInput in{tick, 1.0f, 0.0f, 0};
        game::simulate_player(ground_truth, in, dt);
    }

    // Client reconciled state must now match ground truth with floating point precision
    float diff_x = std::abs(reconciled_state.position.x - ground_truth.position.x);
    float diff_y = std::abs(reconciled_state.position.y - ground_truth.position.y);
    assert(diff_x < 0.001f);
    assert(diff_y < 0.001f);

    std::cout
        << "[PASS] Server reconciliation replayed 10 inputs and converged with ground truth.\n";
}

void test_redundant_input_batching() {
    game::InputHistory history;
    for (uint32_t i = 1; i <= 10; ++i) {
        game::EntityState s;
        history.record_input({i, 0.5f, 0.0f, 0}, s);
    }

    auto batch = history.get_recent_inputs(5);
    assert(batch.size() == 5);
    assert(batch[0].tick == 6);
    assert(batch[4].tick == 10);

    std::cout << "[PASS] Redundant input batching verified.\n";
}

int main() {
    std::cout << ">>> Running Milestone 5 Prediction & Reconciliation Unit Tests <<<\n";
    test_simulation_determinism();
    test_prediction_and_reconciliation();
    test_redundant_input_batching();
    std::cout << ">>> ALL PREDICTION TESTS PASSED! <<<\n";
    return 0;
}
