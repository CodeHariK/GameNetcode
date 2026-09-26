#include "core/Timer.hpp"
#include "game/DeltaSnapshot.hpp"
#include "game/GameTypes.hpp"
#include "game/Simulation.hpp"
#include "net/Connection.hpp"
#include "net/ReliableOrderedChannel.hpp"
#include "net/Socket.hpp"
#include "net/UnreliableSequencedChannel.hpp"
#include "net/UnreliableUnorderedChannel.hpp"

#include <atomic>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

// Milestone 6: authoritative server that broadcasts delta-compressed snapshots.
// It keeps a per-tick history of what it sent; each client acks the newest tick
// it decoded, and the server deltas the next snapshot against that baseline.

namespace {
std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }

constexpr size_t MAX_SNAPSHOT_HISTORY = 128;

struct ClientSession {
    std::unique_ptr<netcode::Connection> connection;
    uint32_t entity_id{0};
    uint32_t last_processed_input_tick{0};
    uint32_t baseline_tick{0};
};
}  // namespace

int main(int argc, char* argv[]) {
    uint16_t port = 40000;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    netcode::Socket socket;
    if (!socket.open(port)) return 1;

    std::cout << "=== Game Netcode Server (Milestone 6: delta compression) ===\n";
    std::cout << "Listening on port " << port << " | 60Hz sim, 20Hz snapshots\n";

    std::unordered_map<netcode::Address, ClientSession> clients;
    std::unordered_map<uint32_t, game::EntityState> world;
    std::map<uint32_t, std::vector<game::EntityState>> snapshot_history;
    uint32_t next_entity_id = 1;

    const double tick_dt = 1.0 / 60.0;
    const uint32_t SNAPSHOT_RATE = 3;
    uint32_t server_tick = 0;
    uint64_t snap_bytes = 0, snap_count = 0;

    double current_time = netcode::Timer::now_seconds();
    double accumulator = 0.0, last_stat = current_time;

    while (g_running) {
        const double new_time = netcode::Timer::now_seconds();
        accumulator += (new_time - current_time);
        current_time = new_time;

        netcode::Address sender;
        uint8_t buffer[2048];
        int bytes = 0;
        while ((bytes = socket.receive(sender, buffer, sizeof(buffer))) > 0) {
            auto it = clients.find(sender);
            if (it == clients.end()) {
                std::cout << "[Server] Client connected: " << sender.to_string() << "\n";
                auto conn = std::make_unique<netcode::Connection>(socket, 5.0, 0.25);
                conn->create_channel<netcode::UnreliableUnorderedChannel>(game::CH_UNRELIABLE);
                conn->create_channel<netcode::UnreliableSequencedChannel>(game::CH_STATE);
                conn->create_channel<netcode::ReliableOrderedChannel>(game::CH_RELIABLE);
                conn->accept(sender, current_time);
                const uint32_t eid = next_entity_id++;
                world[eid] = game::EntityState{eid, {0.0f, 0.0f}, {0.0f, 0.0f}, 0x00FF00};
                ClientSession s;
                s.connection = std::move(conn);
                s.entity_id = eid;
                it = clients.emplace(sender, std::move(s)).first;
            }

            const uint8_t* raw = nullptr;
            size_t raw_bytes = 0;
            if (!it->second.connection->process_packet(sender, buffer, bytes, raw, raw_bytes,
                                                       current_time)) {
                continue;
            }
            netcode::Message msg;
            while (it->second.connection->receive_message(msg)) {
                if (msg.channel_id != game::CH_STATE) continue;
                if (msg.payload.size() < sizeof(game::InputBatchHeader)) continue;
                const auto* hdr =
                    reinterpret_cast<const game::InputBatchHeader*>(msg.payload.data());
                const size_t expected =
                    sizeof(game::InputBatchHeader) + hdr->input_count * sizeof(game::PlayerInput);
                if (msg.payload.size() != expected) continue;
                if (hdr->ack_server_tick > it->second.baseline_tick) {
                    it->second.baseline_tick = hdr->ack_server_tick;
                }
                const auto* inputs = reinterpret_cast<const game::PlayerInput*>(
                    msg.payload.data() + sizeof(game::InputBatchHeader));
                auto& entity = world[it->second.entity_id];
                for (uint32_t i = 0; i < hdr->input_count; ++i) {
                    if (inputs[i].tick > it->second.last_processed_input_tick) {
                        game::simulate_player(entity, inputs[i], static_cast<float>(tick_dt));
                        it->second.last_processed_input_tick = inputs[i].tick;
                    }
                }
            }
        }

        while (accumulator >= tick_dt) {
            ++server_tick;
            accumulator -= tick_dt;
            if (server_tick % SNAPSHOT_RATE == 0) {
                std::vector<game::EntityState> current;
                current.reserve(world.size());
                for (const auto& [id, e] : world) current.push_back(e);
                snapshot_history[server_tick] = current;
                while (snapshot_history.size() > MAX_SNAPSHOT_HISTORY) {
                    snapshot_history.erase(snapshot_history.begin());
                }
                static const std::vector<game::EntityState> kEmpty;
                for (auto& [addr, s] : clients) {
                    if (!s.connection->is_connected()) continue;
                    const std::vector<game::EntityState>* baseline = nullptr;
                    uint32_t baseline_tick = 0;
                    if (s.baseline_tick != 0) {
                        auto hit = snapshot_history.find(s.baseline_tick);
                        if (hit != snapshot_history.end()) {
                            baseline = &hit->second;
                            baseline_tick = s.baseline_tick;
                        }
                    }
                    game::SnapshotWireHeader hdr{server_tick, s.last_processed_input_tick,
                                                 baseline_tick};
                    std::vector<uint8_t> snap = game::encode_delta_snapshot(
                        hdr, current, baseline ? *baseline : kEmpty);
                    s.connection->send_message(game::CH_STATE, snap.data(), snap.size(),
                                               current_time);
                    snap_bytes += snap.size();
                    ++snap_count;
                }
            }
        }

        for (auto it = clients.begin(); it != clients.end();) {
            it->second.connection->update(current_time);
            if (it->second.connection->state() == netcode::ConnectionState::Disconnected) {
                world.erase(it->second.entity_id);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        if (current_time - last_stat >= 2.0) {
            const double avg = snap_count ? double(snap_bytes) / double(snap_count) : 0.0;
            std::cout << "[Server] Tick: " << server_tick << " | Entities: " << world.size()
                      << " | Avg snapshot: " << std::fixed << std::setprecision(1) << avg << "B\n";
            snap_bytes = snap_count = 0;
            last_stat = current_time;
        }
        netcode::Timer::sleep_ms(1.0);
    }
    socket.close();
    return 0;
}
