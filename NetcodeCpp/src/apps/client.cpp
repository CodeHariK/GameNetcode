#include "core/Timer.hpp"
#include "game/DeltaSnapshot.hpp"
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
#include <map>
#include <string>
#include <vector>

// Milestone 8: with AOI, the visible set changes as entities enter/leave range,
// so the client re-targets the nearest visible entity each snapshot and reports
// how many entities it can currently see.

namespace {
std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }

constexpr float SIM_DT = 1.0f / 60.0f;
constexpr size_t REDUNDANT_INPUTS = 5;
constexpr size_t MAX_SNAPSHOT_HISTORY = 128;
constexpr double FIRE_INTERVAL = 0.4;

game::PlayerInput make_input(uint32_t tick) {
    const float t = static_cast<float>(tick) * SIM_DT;
    return {tick, std::cos(t), std::sin(t), 0};
}

std::vector<uint8_t> pack_input_batch(const std::vector<game::PlayerInput>& inputs, uint32_t ack) {
    game::InputBatchHeader header{static_cast<uint32_t>(inputs.size()), ack};
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

    std::cout << "=== Game Netcode Client (Milestone 8: interest management) ===\n";
    std::cout << "Connecting to " << server_addr.to_string() << "\n";

    double current_time = netcode::Timer::now_seconds();
    conn.connect(server_addr, current_time);

    uint32_t client_tick = 0;
    uint32_t local_entity_id = 0;
    game::EntityState local_player{0, {0.0f, 0.0f}, {0.0f, 0.0f}, 0x00FF00};
    game::InputHistory input_history;
    std::map<uint32_t, std::vector<game::EntityState>> snapshot_history;
    uint32_t last_decoded_server_tick = 0;
    uint32_t total_reconciliations = 0;

    bool target_known = false;
    game::Vec2 target_seen{0.0f, 0.0f};
    size_t visible_count = 0;
    uint32_t shots_fired = 0, shots_hit = 0;

    double accumulator = 0.0, last_stat = current_time, last_fire = current_time;
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
                if (msg.channel_id == game::CH_STATE) {
                    const std::vector<game::EntityState>* baseline = nullptr;
                    if (msg.payload.size() >= 12) {
                        uint32_t baseline_tick = 0;
                        std::memcpy(&baseline_tick, msg.payload.data() + 8, sizeof(baseline_tick));
                        if (baseline_tick != 0) {
                            auto hit = snapshot_history.find(baseline_tick);
                            if (hit == snapshot_history.end()) continue;
                            baseline = &hit->second;
                        }
                    }
                    static const std::vector<game::EntityState> kEmpty;
                    game::SnapshotWireHeader hdr{};
                    std::vector<game::EntityState> entities;
                    if (!game::decode_delta_snapshot(msg.payload.data(), msg.payload.size(),
                                                     baseline ? *baseline : kEmpty, hdr, entities)) {
                        continue;
                    }
                    snapshot_history[hdr.server_tick] = entities;
                    while (snapshot_history.size() > MAX_SNAPSHOT_HISTORY) {
                        snapshot_history.erase(snapshot_history.begin());
                    }
                    if (hdr.server_tick > last_decoded_server_tick) {
                        last_decoded_server_tick = hdr.server_tick;
                    }

                    const game::EntityState* server_player =
                        find_local_entity(entities, local_entity_id);
                    visible_count = entities.size();

                    // Nearest visible entity that isn't us (re-evaluated per snapshot).
                    target_known = false;
                    if (server_player) {
                        float best_d2 = 0.0f;
                        for (const auto& e : entities) {
                            if (e.entity_id == local_entity_id) continue;
                            const float dx = e.position.x - server_player->position.x;
                            const float dy = e.position.y - server_player->position.y;
                            const float d2 = dx * dx + dy * dy;
                            if (!target_known || d2 < best_d2) {
                                target_known = true;
                                best_d2 = d2;
                                target_seen = e.position;
                            }
                        }
                    }
                    if (!server_player) continue;
                    const uint32_t ack = hdr.last_client_input_tick;
                    if (input_history.has_diverged(ack, *server_player)) {
                        game::EntityState reconciled;
                        input_history.reconcile(ack, *server_player, SIM_DT, reconciled);
                        local_player = reconciled;
                        ++total_reconciliations;
                    } else {
                        input_history.discard_acknowledged(ack);
                    }
                } else if (msg.channel_id == game::CH_RELIABLE) {
                    if (msg.payload.size() != 1 + sizeof(game::HitNotification) ||
                        msg.payload[0] != game::MSG_HIT) {
                        continue;
                    }
                    game::HitNotification note{};
                    std::memcpy(&note, msg.payload.data() + 1, sizeof(note));
                    if (note.hit) ++shots_hit;
                }
            }
        }

        while (accumulator >= SIM_DT) {
            ++client_tick;
            accumulator -= SIM_DT;
            const game::PlayerInput input = make_input(client_tick);
            game::simulate_player(local_player, input, SIM_DT);
            input_history.record_input(input, local_player);
            const auto payload = pack_input_batch(
                input_history.get_recent_inputs(REDUNDANT_INPUTS), last_decoded_server_tick);
            conn.send_message(game::CH_STATE, payload.data(), payload.size(), current_time);
        }

        if (target_known && conn.is_connected() && current_time - last_fire >= FIRE_INTERVAL) {
            last_fire = current_time;
            game::FireCommand fire{client_tick, last_decoded_server_tick, local_player.position.x,
                                   local_player.position.y, target_seen.x - local_player.position.x,
                                   target_seen.y - local_player.position.y};
            std::vector<uint8_t> shot(1 + sizeof(fire));
            shot[0] = game::MSG_FIRE;
            std::memcpy(shot.data() + 1, &fire, sizeof(fire));
            conn.send_message(game::CH_RELIABLE, shot.data(), shot.size(), current_time);
            ++shots_fired;
        }

        conn.update(current_time);
        if (conn.state() == netcode::ConnectionState::Disconnected) {
            std::cout << "[Client] Disconnected.\n";
            break;
        }

        if (current_time - last_stat >= 1.0) {
            const uint32_t pct = shots_fired ? shots_hit * 100 / shots_fired : 0;
            std::cout << "[Client] Tick: " << client_tick
                      << " | Ping: " << static_cast<int>(conn.rtt_ms())
                      << "ms | Reconciliations: " << total_reconciliations << " | Shots: "
                      << shots_fired << " Hits: " << shots_hit << " (" << pct << "%) | Visible: "
                      << visible_count << "\n";
            last_stat = current_time;
        }
        netcode::Timer::sleep_ms(1.0);
    }

    conn.disconnect(current_time);
    socket.close();
    return 0;
}
