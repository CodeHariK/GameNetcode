#include "core/Timer.hpp"
#include "game/GameTypes.hpp"
#include "game/InputHistory.hpp"
#include "game/Simulation.hpp"
#include "net/Connection.hpp"
#include "net/ReliableOrderedChannel.hpp"
#include "net/Socket.hpp"
#include "net/UnreliableSequencedChannel.hpp"
#include "net/UnreliableUnorderedChannel.hpp"

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// Milestone 5: predictive client. Applies input locally at once, records it, and
// reconciles against the server's authoritative state when they diverge.

namespace {
std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }

constexpr float SIM_DT = 1.0f / 60.0f;
constexpr size_t REDUNDANT_INPUTS = 5;

game::PlayerInput make_input(uint32_t tick) {
    const float t = static_cast<float>(tick) * SIM_DT;
    return {tick, std::cos(t), std::sin(t), 0};
}

std::vector<uint8_t> pack_input_batch(const std::vector<game::PlayerInput>& inputs) {
    game::InputBatchHeader header{static_cast<uint32_t>(inputs.size())};
    std::vector<uint8_t> payload(sizeof(header) + inputs.size() * sizeof(game::PlayerInput));
    std::memcpy(payload.data(), &header, sizeof(header));
    if (!inputs.empty()) {
        std::memcpy(payload.data() + sizeof(header), inputs.data(),
                    inputs.size() * sizeof(game::PlayerInput));
    }
    return payload;
}

const game::EntityState* find_local_entity(const std::vector<game::EntityState>& entities,
                                           uint32_t& local_id) {
    const game::EntityState* found = nullptr;
    for (const auto& e : entities) {
        if (local_id == 0) {
            if (!found || e.entity_id < found->entity_id) found = &e;
        } else if (e.entity_id == local_id) {
            return &e;
        }
    }
    if (found) local_id = found->entity_id;
    return found;
}
}  // namespace

int main(int argc, char* argv[]) {
    uint16_t port = 40000;
    std::string ip = "127.0.0.1";
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc > 2) ip = argv[2];

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    netcode::Socket socket;
    if (!socket.open(0)) return 1;

    netcode::Address server_addr(ip, port);
    netcode::Connection conn(socket, 5.0, 0.25);
    conn.create_channel<netcode::UnreliableUnorderedChannel>(game::CH_UNRELIABLE);
    conn.create_channel<netcode::UnreliableSequencedChannel>(game::CH_STATE);
    conn.create_channel<netcode::ReliableOrderedChannel>(game::CH_RELIABLE);

    std::cout << "=== Game Netcode Client (Milestone 5: prediction) ===\n";
    std::cout << "Connecting to " << server_addr.to_string() << "\n";

    double current_time = netcode::Timer::now_seconds();
    conn.connect(server_addr, current_time);

    uint32_t client_tick = 0;
    uint32_t local_entity_id = 0;
    game::EntityState local_player{0, {0.0f, 0.0f}, {0.0f, 0.0f}, 0x00FF00};
    game::InputHistory input_history;
    uint32_t total_reconciliations = 0;
    uint64_t total_replayed = 0;

    double accumulator = 0.0;
    double last_stat = current_time;
    uint8_t buffer[2048];

    while (g_running) {
        const double new_time = netcode::Timer::now_seconds();
        accumulator += (new_time - current_time);
        current_time = new_time;

        netcode::Address sender;
        int bytes = 0;
        while ((bytes = socket.receive(sender, buffer, sizeof(buffer))) > 0) {
            const uint8_t* raw = nullptr;
            size_t raw_bytes = 0;
            if (!conn.process_packet(sender, buffer, bytes, raw, raw_bytes, current_time)) continue;
            netcode::Message msg;
            while (conn.receive_message(msg)) {
                if (msg.channel_id != game::CH_STATE) continue;
                if (msg.payload.size() < sizeof(game::SnapshotHeader)) continue;
                game::SnapshotHeader hdr{};
                std::memcpy(&hdr, msg.payload.data(), sizeof(hdr));
                const size_t expected =
                    sizeof(hdr) + hdr.entity_count * sizeof(game::EntityState);
                if (msg.payload.size() != expected) continue;
                std::vector<game::EntityState> entities(hdr.entity_count);
                if (hdr.entity_count > 0) {
                    std::memcpy(entities.data(), msg.payload.data() + sizeof(hdr),
                                hdr.entity_count * sizeof(game::EntityState));
                }
                const game::EntityState* server_player =
                    find_local_entity(entities, local_entity_id);
                if (!server_player) continue;
                const uint32_t ack = hdr.last_client_input_tick;
                if (input_history.has_diverged(ack, *server_player)) {
                    game::EntityState reconciled;
                    total_replayed += input_history.reconcile(ack, *server_player, SIM_DT, reconciled);
                    local_player = reconciled;
                    ++total_reconciliations;
                } else {
                    input_history.discard_acknowledged(ack);
                }
            }
        }

        while (accumulator >= SIM_DT) {
            ++client_tick;
            accumulator -= SIM_DT;
            const game::PlayerInput input = make_input(client_tick);
            game::simulate_player(local_player, input, SIM_DT);
            input_history.record_input(input, local_player);
            const auto payload =
                pack_input_batch(input_history.get_recent_inputs(REDUNDANT_INPUTS));
            conn.send_message(game::CH_STATE, payload.data(), payload.size(), current_time);
        }

        conn.update(current_time);
        if (conn.state() == netcode::ConnectionState::Disconnected) {
            std::cout << "[Client] Disconnected.\n";
            break;
        }

        if (current_time - last_stat >= 1.0) {
            std::cout << "[Client] Tick: " << client_tick << " | Ping: "
                      << static_cast<int>(conn.rtt_ms()) << "ms | Predicted: (" << std::fixed
                      << std::setprecision(2) << local_player.position.x << ", "
                      << local_player.position.y << ") | Reconciliations: " << total_reconciliations
                      << " (replayed " << total_replayed << ")\n";
            last_stat = current_time;
        }
        netcode::Timer::sleep_ms(1.0);
    }

    conn.disconnect(current_time);
    socket.close();
    return 0;
}
