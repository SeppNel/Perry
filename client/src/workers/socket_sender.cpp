#include "socket_sender.h"
#include <QTimer>
#include <cstdint>
#include <iostream>
#include <string>

#define FIFO_FREQ_MS 10

void SocketSender::init(int s) {
    sock = s;
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SocketSender::run);
    timer->start(FIFO_FREQ_MS); // 1000ms = 1 second
}

void SocketSender::run() {
    if (header_fifo.empty()) {
        return;
    }

    PacketHeader header = header_fifo.front();
    PacketType t = (PacketType)header.type;
    switch (t) {
    case PacketType::LIST_CHANNELS: {
        send_packet(sock, PacketType::LIST_CHANNELS, NULL, 0);
        break;
    }
    case PacketType::LIST_USERS: {
        send_packet(sock, PacketType::LIST_USERS, NULL, 0);
        break;
    }
    case PacketType::LIST_MESSAGES: {
        handleListMessages(header);
        send_packet(sock, PacketType::LIST_USERS, NULL, 0);
        break;
    }
    case PacketType::MESSAGE: {
        handleMessage(header);
        break;
    }
    default:
        std::cout << "Packet Type not recognized" << std::endl;
    }

    header_fifo.pop();
}

void SocketSender::enqueuePacket(const PacketHeader &header, const std::vector<char> &payload) {
    header_fifo.push(header);
    if (!payload.empty()) {
        payload_fifo.insert(payload_fifo.end(), payload.begin(), payload.end());
    }
}

void SocketSender::handleMessage(const PacketHeader &header) {
    if (header.length < sizeof(uint32_t)) {
        std::cerr << "invalid header.length < sizeof(uint32_t)\n";
        return;
    }
    if (payload_fifo.size() < header.length) {
        std::cerr << "not enough payload bytes yet\n";
        return;
    }

    auto it = payload_fifo.begin();

    // Read channelId
    uint32_t channelId;
    char tmp[sizeof(channelId)];
    std::copy(it, it + sizeof(channelId), tmp);
    std::memcpy(&channelId, tmp, sizeof(channelId));
    it += sizeof(channelId);

    // Read string
    uint32_t str_length = header.length - sizeof(uint32_t);
    std::string str(it, it + str_length);

    // Erase all bytes for this packet
    payload_fifo.erase(payload_fifo.begin(), payload_fifo.begin() + header.length);

    if (str_length == 0) {
        return;
    }

    // Send packet
    send_packet(sock, PacketType::MESSAGE, NULL, 0);
    send_packet(sock, PacketType::UINT, channelId);
    send_string(sock, str);
}

void SocketSender::handleListMessages(const PacketHeader &header) {
    if (header.length < sizeof(uint32_t)) {
        std::cerr << "invalid header.length < sizeof(uint32_t)\n";
        return;
    }
    if (payload_fifo.size() < header.length) {
        std::cerr << "not enough payload bytes yet\n";
        return;
    }

    auto it = payload_fifo.begin();

    // Read channelId
    uint32_t channelId;
    char tmp[sizeof(channelId)];
    std::copy(it, it + sizeof(channelId), tmp);
    std::memcpy(&channelId, tmp, sizeof(channelId));
    it += sizeof(channelId);

    // Erase all bytes for this packet
    payload_fifo.erase(payload_fifo.begin(), payload_fifo.begin() + header.length);

    // Send packet
    send_packet(sock, PacketType::LIST_MESSAGES, NULL, 0);
    send_packet(sock, PacketType::UINT, channelId);
}