#include "Connection.hpp"
#include "NetworkSimulator.hpp"
#include <iostream>
#include <vector>
#include <arpa/inet.h>

namespace netcode {

Connection::Connection(Socket& socket, double timeout_sec, double heartbeat_interval_sec)
    : socket_(socket),
      timeout_sec_(timeout_sec),
      heartbeat_interval_sec_(heartbeat_interval_sec),
      reliability_(1024) {
    setup_callbacks();
}

void Connection::setup_callbacks() {
    // When underlying UDP packet sequence is acknowledged, inform all channels
    reliability_.set_ack_callback([this](uint16_t packet_seq) {
        for (auto& [ch_id, channel] : channels_) {
            channel->on_packet_acked(packet_seq);
        }
    });
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

bool Connection::send_message(uint8_t channel_id,
                              const void* data,
                              size_t size,
                              double current_time) {
    auto* ch = get_channel(channel_id);
    if (!ch) return false;
    ch->send_message(data, size, current_time);
    return true;
}

bool Connection::receive_message(Message& out_message) {
    for (auto& [ch_id, channel] : channels_) {
        if (channel->receive_message(out_message)) {
            return true;
        }
    }
    return false;
}

bool Connection::send_packet(const void* payload, size_t size, double current_time) {
    if (state_ == ConnectionState::Disconnected) return false;

    // Buffer: PacketHeader (12 bytes) + Payload
    std::vector<uint8_t> buffer(PacketHeader::HEADER_SIZE + size);

    PacketHeader header;
    reliability_.generate_packet_header(header, current_time);
    header.serialize(buffer.data());

    if (payload && size > 0) {
        std::memcpy(buffer.data() + PacketHeader::HEADER_SIZE, payload, size);
    }

    bool sent = sim_
                    ? sim_->send_packet(remote_address_, buffer.data(), buffer.size(), current_time)
                    : socket_.send(remote_address_, buffer.data(), buffer.size());
    if (sent) {
        last_packet_sent_time_ = current_time;
    }
    return sent;
}

bool Connection::flush_channels(double current_time) {
    if (state_ == ConnectionState::Disconnected) return false;

    // Check if any channel has pending outgoing data
    bool has_data = false;
    for (const auto& [ch_id, ch] : channels_) {
        if (ch->has_outgoing_messages()) {
            has_data = true;
            break;
        }
    }
    if (!has_data) return false;

    constexpr size_t MAX_PACKET_SIZE = 1200;  // Standard safe game MTU
    std::vector<uint8_t> buffer(MAX_PACKET_SIZE);

    PacketHeader header;
    reliability_.generate_packet_header(header, current_time);
    header.serialize(buffer.data());

    size_t payload_offset = PacketHeader::HEADER_SIZE;
    size_t max_payload = MAX_PACKET_SIZE - payload_offset;

    // Context tag for reliable channels
    for (auto& [ch_id, ch] : channels_) {
        if (ch->type() == ChannelType::ReliableOrdered) {
            auto* rel_ch = static_cast<ReliableOrderedChannel*>(ch.get());
            rel_ch->set_current_packet_sequence(header.sequence);
        }
    }

    size_t total_payload_written = 0;
    for (auto& [ch_id, ch] : channels_) {
        if (total_payload_written >= max_payload) break;

        size_t written =
            ch->write_outgoing_messages(buffer.data() + payload_offset + total_payload_written,
                                        max_payload - total_payload_written,
                                        current_time);
        total_payload_written += written;
    }

    if (total_payload_written == 0) {
        return false;
    }

    size_t packet_size = PacketHeader::HEADER_SIZE + total_payload_written;
    bool sent = sim_ ? sim_->send_packet(remote_address_, buffer.data(), packet_size, current_time)
                     : socket_.send(remote_address_, buffer.data(), packet_size);
    if (sent) {
        last_packet_sent_time_ = current_time;
    }
    return sent;
}

bool Connection::process_packet(const Address& sender,
                                const uint8_t* data,
                                size_t size,
                                const uint8_t*& out_raw_payload,
                                size_t& out_raw_payload_bytes,
                                double current_time) {
    if (size < PacketHeader::HEADER_SIZE) return false;

    if (state_ != ConnectionState::Disconnected && sender != remote_address_) {
        return false;
    }

    PacketHeader header;
    if (!header.deserialize(data, size)) {
        return false;
    }

    reliability_.packet_received(header, current_time);
    last_packet_received_time_ = current_time;

    if (state_ == ConnectionState::Connecting) {
        state_ = ConnectionState::Connected;
    }

    const uint8_t* payload = data + PacketHeader::HEADER_SIZE;
    size_t payload_bytes = size - PacketHeader::HEADER_SIZE;

    // Multiplexed channel parsing
    size_t offset = 0;
    bool parsed_any_channel_message = false;

    while (offset + MessageHeader::HEADER_SIZE <= payload_bytes) {
        uint8_t ch_id = payload[offset];
        uint16_t msg_id;
        uint16_t msg_len;
        std::memcpy(&msg_id, payload + offset + 1, 2);
        std::memcpy(&msg_len, payload + offset + 3, 2);
        msg_id = ntohs(msg_id);
        msg_len = ntohs(msg_len);

        if (offset + MessageHeader::HEADER_SIZE + msg_len > payload_bytes) {
            break;  // Malformed slice, stop unpacking
        }

        auto* ch = get_channel(ch_id);
        if (ch) {
            const uint8_t* msg_payload = payload + offset + MessageHeader::HEADER_SIZE;
            ch->process_incoming_message(msg_id, msg_payload, msg_len);
            parsed_any_channel_message = true;
        }

        offset += MessageHeader::HEADER_SIZE + msg_len;
    }

    if (!parsed_any_channel_message) {
        // Expose raw payload to caller if not packed channel messages
        out_raw_payload = payload;
        out_raw_payload_bytes = payload_bytes;
    } else {
        out_raw_payload = nullptr;
        out_raw_payload_bytes = 0;
    }

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

    // Flush any pending messages across all channels
    flush_channels(current_time);

    // Keep-alive heartbeat
    if (current_time - last_packet_sent_time_ >= heartbeat_interval_sec_) {
        send_heartbeat(current_time);
    }

    // Update channels (retransmission timers & RTT propagation)
    for (auto& [ch_id, ch] : channels_) {
        ch->update(current_time, reliability_.rtt_ms());
    }

    reliability_.update(current_time);
}

}  // namespace netcode
