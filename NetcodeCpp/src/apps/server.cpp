#include "core/Timer.hpp"
#include "game/DeltaSnapshot.hpp"
#include "game/GameTypes.hpp"
#include "game/InterestManagement.hpp"
#include "game/LagCompensation.hpp"
#include "game/SimConfig.hpp"
#include "game/Simulation.hpp"
#include "net/Connection.hpp"
#include "net/NetworkSimulator.hpp"
#include "net/ReliableOrderedChannel.hpp"
#include "net/Socket.hpp"
#include "net/UnreliableSequencedChannel.hpp"
#include "net/UnreliableUnorderedChannel.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

// Milestone 8: area-of-interest culling. Several bots roam; each client is sent
// only the entities near its player, and its delta baseline becomes the exact
// visible subset it last acknowledged (stored per client). The full-world rewind
// buffer for lag comp is kept separately, so hit tests stay fair.

namespace {
std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }

constexpr size_t MAX_SNAPSHOT_HISTORY = 128;
constexpr uint32_t BOT_BASE_ID = 1000;
constexpr int NUM_BOTS = 6;
constexpr float HIT_RADIUS = 3.0f;
constexpr float AOI_RADIUS = 35.0f;
constexpr size_t AOI_MAX_ENTITIES = 16;
constexpr double INTERP_DELAY = 0.100;

struct BotSpec {
    float amplitude;
    float frequency;
    float phase;
    float lane_y;
};
constexpr std::array<BotSpec, NUM_BOTS> kBots = {{
    {60.0f, 2.0f, 0.0f, 0.0f},
    {50.0f, 1.5f, 1.0f, 22.0f},
    {40.0f, 2.5f, 2.0f, -22.0f},
    {55.0f, 1.2f, 3.0f, 45.0f},
    {45.0f, 1.8f, 4.0f, -45.0f},
    {35.0f, 2.2f, 5.0f, 15.0f},
}};

struct ClientSession {
    std::unique_ptr<netcode::Connection> connection;
    uint32_t entity_id{0};
    uint32_t last_processed_input_tick{0};
    uint32_t baseline_tick{0};
    std::map<uint32_t, std::vector<game::EntityState>> sent_history;  // per-client baselines
    std::unique_ptr<netcode::NetworkSimulator> sim;  // optional outgoing impairment
};
}  // namespace

int main(int argc, char* argv[]) {
    uint16_t port = 40000;
    if (argc > 1) port = static_cast<uint16_t>(std::atoi(argv[1]));

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    netcode::Socket socket;
    if (!socket.open(port)) return 1;

    std::cout << "=== Game Netcode Server (Milestone 9: network simulator) ===\n";
    std::cout << "Listening on port " << port << "\n";

    const netcode::NetworkSimulatorConfig sim_cfg = game::read_sim_config_from_env();
    const bool use_sim = game::sim_enabled(sim_cfg);
    if (use_sim) {
        std::cout << "[Server] Network simulation ON (server->client): " << sim_cfg.latency_ms
                  << "ms +/-" << sim_cfg.jitter_ms << "ms, loss " << (sim_cfg.packet_loss_rate * 100.0f)
                  << "%, dup " << (sim_cfg.duplicate_rate * 100.0f) << "%\n";
    }

    std::unordered_map<netcode::Address, ClientSession> clients;
    std::unordered_map<uint32_t, game::EntityState> world;
    game::WorldHistory world_history(128);
    uint32_t next_entity_id = 1;

    for (int i = 0; i < NUM_BOTS; ++i) {
        const uint32_t id = BOT_BASE_ID + static_cast<uint32_t>(i);
        world[id] = game::EntityState{id, {0.0f, kBots[i].lane_y}, {0.0f, 0.0f}, 0xFF00FF};
    }

    const double tick_dt = 1.0 / 60.0;
    const uint32_t SNAPSHOT_RATE = 3;
    uint32_t server_tick = 0;
    uint64_t vis_sum = 0, vis_count = 0;

    double current_time = netcode::Timer::now_seconds();
    const double start_time = current_time;
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
                if (use_sim) {
                    s.sim = std::make_unique<netcode::NetworkSimulator>(socket, sim_cfg);
                    s.connection->set_network_simulator(s.sim.get());
                }
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
                if (msg.channel_id == game::CH_STATE) {
                    if (msg.payload.size() < sizeof(game::InputBatchHeader)) continue;
                    const auto* hdr =
                        reinterpret_cast<const game::InputBatchHeader*>(msg.payload.data());
                    const size_t expected = sizeof(game::InputBatchHeader) +
                                            hdr->input_count * sizeof(game::PlayerInput);
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
                } else if (msg.channel_id == game::CH_RELIABLE) {
                    if (msg.payload.size() != 1 + sizeof(game::FireCommand) ||
                        msg.payload[0] != game::MSG_FIRE) {
                        continue;
                    }
                    game::FireCommand fire{};
                    std::memcpy(&fire, msg.payload.data() + 1, sizeof(fire));
                    std::vector<game::EntityState> interp;
                    const std::vector<game::EntityState>* rewound =
                        world_history.get_at_tick(fire.view_server_tick);
                    if (!rewound) {
                        const float rtt_s = it->second.connection->rtt_ms() / 1000.0f;
                        if (world_history.sample(current_time - rtt_s * 0.5 - INTERP_DELAY, interp)) {
                            rewound = &interp;
                        }
                    }
                    game::HitResult result{};
                    if (rewound) {
                        result = game::hitscan({fire.origin_x, fire.origin_y},
                                               {fire.aim_x, fire.aim_y}, *rewound,
                                               it->second.entity_id, HIT_RADIUS);
                    }
                    game::HitNotification note{fire.client_tick, result.hit ? uint8_t{1} : uint8_t{0},
                                               result.target_id, result.point.x, result.point.y};
                    std::vector<uint8_t> reply(1 + sizeof(note));
                    reply[0] = game::MSG_HIT;
                    std::memcpy(reply.data() + 1, &note, sizeof(note));
                    it->second.connection->send_message(game::CH_RELIABLE, reply.data(),
                                                        reply.size(), current_time);
                }
            }
        }

        while (accumulator >= tick_dt) {
            ++server_tick;
            accumulator -= tick_dt;

            const float t = static_cast<float>(current_time - start_time);
            for (int i = 0; i < NUM_BOTS; ++i) {
                const uint32_t id = BOT_BASE_ID + static_cast<uint32_t>(i);
                world[id].position = {kBots[i].amplitude * std::sin(t * kBots[i].frequency + kBots[i].phase),
                                      kBots[i].lane_y};
            }

            std::vector<game::EntityState> current;
            current.reserve(world.size());
            for (const auto& [id, e] : world) current.push_back(e);
            world_history.record(current_time, server_tick, current);

            if (server_tick % SNAPSHOT_RATE == 0) {
                static const std::vector<game::EntityState> kEmpty;
                for (auto& [addr, s] : clients) {
                    if (!s.connection->is_connected()) continue;
                    std::vector<game::EntityState> visible = game::compute_visible_set(
                        s.entity_id, current, AOI_RADIUS, AOI_MAX_ENTITIES);
                    const std::vector<game::EntityState>* baseline = nullptr;
                    uint32_t baseline_tick = 0;
                    if (s.baseline_tick != 0) {
                        auto hit = s.sent_history.find(s.baseline_tick);
                        if (hit != s.sent_history.end()) {
                            baseline = &hit->second;
                            baseline_tick = s.baseline_tick;
                        }
                    }
                    game::SnapshotWireHeader hdr{server_tick, s.last_processed_input_tick,
                                                 baseline_tick};
                    std::vector<uint8_t> snap =
                        game::encode_delta_snapshot(hdr, visible, baseline ? *baseline : kEmpty);
                    s.connection->send_message(game::CH_STATE, snap.data(), snap.size(),
                                               current_time);
                    vis_sum += visible.size();
                    ++vis_count;
                    s.sent_history[server_tick] = std::move(visible);
                    while (s.sent_history.size() > MAX_SNAPSHOT_HISTORY) {
                        s.sent_history.erase(s.sent_history.begin());
                    }
                }
            }
        }

        for (auto it = clients.begin(); it != clients.end();) {
            it->second.connection->update(current_time);
            if (it->second.sim) it->second.sim->update(current_time);
            if (it->second.connection->state() == netcode::ConnectionState::Disconnected) {
                world.erase(it->second.entity_id);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        if (current_time - last_stat >= 2.0) {
            const double avg = vis_count ? double(vis_sum) / double(vis_count) : 0.0;
            std::cout << "[Server] Tick: " << server_tick << " | World entities: " << world.size()
                      << " | Avg visible/client: " << std::fixed << std::setprecision(1) << avg
                      << "\n";
            vis_sum = vis_count = 0;
            last_stat = current_time;
        }
        netcode::Timer::sleep_ms(1.0);
    }
    socket.close();
    return 0;
}
