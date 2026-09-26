#include "Connection.hpp"

#include <cstring>
#include <iostream>
#include <vector>

namespace netcode {

Connection::Connection(Socket& socket, double timeout_sec, double heartbeat_interval_sec)
    : socket_(socket),
      timeout_sec_(timeout_sec),
      heartbeat_interval_sec_(heartbeat_interval_sec),
      reliability_(1024) {
    // No channels yet (Milestone 3); keep the ack callback a harmless no-op.
    reliability_.set_ack_callback([](uint16_t) {});
}

void Connection::connect(const Address& address, double current_time) {
    remote_address_ = address;
    state_ = ConnectionState::Connecting;
    last_packet_sent_time_ = 0.0;
    last_packet_received_time_ = current_time;
    reliability_.reset();
}

void Connection::accept(const Address& address, double current_time) {
    remote_address_ = address;
    state_ = ConnectionState::Connected;
    last_packet_sent_time_ = current_time;
    last_packet_received_time_ = current_time;
    reliability_.reset();
}

void Connection::disconnect(double /*current_time*/) {
    state_ = ConnectionState::Disconnected;
    reliability_.reset();
}

bool Connection::send_packet(const void* payload, size_t size, double current_time) {
    if (state_ == ConnectionState::Disconnected) return false;

    std::vector<uint8_t> buffer(PacketHeader::HEADER_SIZE + size);
    PacketHeader header;
    reliability_.generate_packet_header(header, current_time);
    header.serialize(buffer.data());
    if (payload && size > 0) {
        std::memcpy(buffer.data() + PacketHeader::HEADER_SIZE, payload, size);
    }

    const bool sent = socket_.send(remote_address_, buffer.data(), buffer.size());
    if (sent) last_packet_sent_time_ = current_time;
    return sent;
}

bool Connection::process_packet(const Address& sender,
                                const uint8_t* data,
                                size_t size,
                                const uint8_t*& out_raw_payload,
                                size_t& out_raw_payload_bytes,
                                double current_time) {
    if (size < PacketHeader::HEADER_SIZE) return false;
    if (state_ != ConnectionState::Disconnected && sender != remote_address_) return false;

    PacketHeader header;
    if (!header.deserialize(data, size)) return false;

    reliability_.packet_received(header, current_time);
    last_packet_received_time_ = current_time;
    if (state_ == ConnectionState::Connecting) state_ = ConnectionState::Connected;

    out_raw_payload = data + PacketHeader::HEADER_SIZE;
    out_raw_payload_bytes = size - PacketHeader::HEADER_SIZE;
    return true;
}

void Connection::send_heartbeat(double current_time) {
    send_packet(nullptr, 0, current_time);
}

void Connection::update(double current_time) {
    if (state_ == ConnectionState::Disconnected) return;

    if (current_time - last_packet_received_time_ > timeout_sec_) {
        std::cout << "[Connection] Remote " << remote_address_.to_string() << " timed out after "
                  << timeout_sec_ << "s of silence. Disconnecting.\n";
        disconnect(current_time);
        return;
    }

    if (current_time - last_packet_sent_time_ >= heartbeat_interval_sec_) {
        send_heartbeat(current_time);
    }

    reliability_.update(current_time);
}

}  // namespace netcode
