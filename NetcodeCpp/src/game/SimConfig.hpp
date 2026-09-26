#pragma once

#include "net/NetworkSimulator.hpp"

#include <cstdlib>
#include <string>

namespace game {

/**
 * Builds a NetworkSimulatorConfig from environment variables so the server and
 * client can be launched under simulated network conditions without changing
 * their positional arguments (Milestone 9):
 *
 *   NETSIM_LATENCY_MS  one-way added delay, ms   (e.g. 80)
 *   NETSIM_JITTER_MS   +/- delay variance, ms    (e.g. 20)
 *   NETSIM_LOSS        drop probability [0,1]    (e.g. 0.05)
 *   NETSIM_DUP         duplicate probability [0,1](e.g. 0.02)
 */
inline float env_float(const char* name, float fallback) {
    const char* v = std::getenv(name);
    if (!v || *v == '\0') return fallback;
    try {
        return std::stof(v);
    } catch (...) {
        return fallback;
    }
}

inline netcode::NetworkSimulatorConfig read_sim_config_from_env() {
    netcode::NetworkSimulatorConfig cfg{};
    cfg.latency_ms = env_float("NETSIM_LATENCY_MS", 0.0f);
    cfg.jitter_ms = env_float("NETSIM_JITTER_MS", 0.0f);
    cfg.packet_loss_rate = env_float("NETSIM_LOSS", 0.0f);
    cfg.duplicate_rate = env_float("NETSIM_DUP", 0.0f);
    return cfg;
}

// True when any impairment is configured (otherwise the simulator is pointless).
inline bool sim_enabled(const netcode::NetworkSimulatorConfig& cfg) {
    return cfg.latency_ms > 0.0f || cfg.jitter_ms > 0.0f || cfg.packet_loss_rate > 0.0f ||
           cfg.duplicate_rate > 0.0f;
}

}  // namespace game
