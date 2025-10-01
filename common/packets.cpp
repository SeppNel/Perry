#include "packets.h"
#include "common_data.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

bool send_all(int sock, const void *buf, size_t len) {
    const char *ptr = static_cast<const char *>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, ptr + sent, len - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += n;
    }
    return true;
}

bool recv_all(int sock, void *buf, size_t len) {
    char *ptr = static_cast<char *>(buf);
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t n = recv(sock, ptr + recvd, len - recvd, 0);
        if (n <= 0) {
            return false;
        }
        recvd += n;
    }
    return true;
}

bool send_packet(int sock, PacketType type, const std::vector<char> &data) {
    PacketHeader header;
    header.type = static_cast<uint8_t>(type);
    header.length = htonl(static_cast<uint32_t>(data.size()));

    if (!send_all(sock, &header, sizeof(header))) {
        return false;
    }
    if (!data.empty() && !send_all(sock, data.data(), data.size())) {
        return false;
    }

    return true;
}

bool send_packet(int sock, PacketType type, const void *data, size_t size) {
    PacketHeader header;
    header.type = static_cast<uint8_t>(type);
    header.length = htonl(static_cast<uint32_t>(size));

    if (!send_all(sock, &header, sizeof(header))) {
        return false;
    }

    if (size > 0) {
        if (!send_all(sock, data, size)) {
            return false;
        }
    }

    return true;
}

bool send_string(int sock, const std::string &str) {
    return send_packet(sock, PacketType::TEXT, str.data(), str.size());
}

bool send_channelInfo(int sock, const ChannelInfo &c) {
    std::vector<char> buffer;

    // Serialize id (network order)
    uint32_t id_net = htonl(c.id);
    buffer.insert(buffer.end(), reinterpret_cast<char *>(&id_net),
                  reinterpret_cast<char *>(&id_net) + sizeof(id_net));

    // Serialize is_voice (just 1 byte)
    buffer.push_back(static_cast<char>(c.is_voice));

    // Serialize string length + content
    uint32_t name_len = htonl(static_cast<uint32_t>(c.name.size()));
    buffer.insert(buffer.end(), reinterpret_cast<char *>(&name_len),
                  reinterpret_cast<char *>(&name_len) + sizeof(name_len));

    buffer.insert(buffer.end(), c.name.begin(), c.name.end());

    // Send packet
    return send_packet(sock, PacketType::CHANNEL_INFO, buffer.data(), buffer.size());
}

bool send_userInfo(int sock, const UserInfo &u) {
    std::vector<char> buffer;

    // Serialize id (network order)
    uint32_t id_net = htonl(u.id);
    buffer.insert(buffer.end(), reinterpret_cast<char *>(&id_net),
                  reinterpret_cast<char *>(&id_net) + sizeof(id_net));

    // Serialize is_voice (just 1 byte)
    buffer.push_back(static_cast<char>(u.is_online));

    // Serialize string length + content
    uint32_t name_len = htonl(static_cast<uint32_t>(u.name.size()));
    buffer.insert(buffer.end(), reinterpret_cast<char *>(&name_len),
                  reinterpret_cast<char *>(&name_len) + sizeof(name_len));

    buffer.insert(buffer.end(), u.name.begin(), u.name.end());

    // Send packet
    return send_packet(sock, PacketType::USER_INFO, buffer.data(), buffer.size());
}

bool send_message(int sock, const MessageInfo &m) {
    send_packet(sock, PacketType::MESSAGE, NULL, 0);

    send_packet(sock, PacketType::UINT, m.userId);
    send_packet(sock, PacketType::UINT, m.timestamp);
    send_string(sock, m.msg);

    return true;
}

bool send_image(int socket, const std::string &filename) {
    // Open file in binary mode and at the end
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return false;
    }

    // Get file size
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    uint64_t fileSize = static_cast<uint64_t>(size);

    // Read file into buffer
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        std::cerr << "Failed to read file\n";
        return false;
    }

    send_packet(socket, PacketType::UINT64, fileSize); // Send file size first (as 64-bit integer)
    send_packet(socket, PacketType::BUFFER, buffer);   // Send image

    return true;
}

bool recv_packet(int sock, PacketType &type, std::vector<char> &data) {
    PacketHeader header;
    if (!recv_all(sock, &header, sizeof(header))) {
        return false;
    }

    type = static_cast<PacketType>(header.type);
    uint32_t length = ntohl(header.length);

    data.resize(length);
    if (length > 0) {
        if (!recv_all(sock, data.data(), length)) {
            return false;
        }
    }

    return true;
}

bool recv_code(int sock, uint8_t &code) {
    PacketType type;
    std::vector<char> buffer;
    if (!recv_packet(sock, type, buffer)) {
        return false;
    }

    if (type != PacketType::CODE || buffer.empty()) {
        return false;
    }

    code = static_cast<uint8_t>(buffer[0]);
    return true;
}

bool recv_string(int sock, std::string &str) {
    PacketType type;
    std::vector<char> buffer;
    if (!recv_packet(sock, type, buffer)) {
        return false;
    }

    if (type != PacketType::TEXT) {
        return false;
    }

    str.assign(buffer.data(), buffer.size());
    return true;
}

bool recv_int(int sock, int32_t &out) {
    PacketType type;
    std::vector<char> buffer;
    if (!recv_packet(sock, type, buffer)) {
        return false;
    }

    if (type != PacketType::INT || buffer.empty()) {
        return false;
    }

    std::memcpy(&out, buffer.data(), sizeof(int32_t));
    return true;
}

bool recv_uint(int sock, uint32_t &out) {
    PacketType type;
    std::vector<char> buffer;
    if (!recv_packet(sock, type, buffer)) {
        return false;
    }

    if (type != PacketType::UINT || buffer.empty()) {
        return false;
    }

    std::memcpy(&out, buffer.data(), sizeof(uint32_t));
    return true;
}

bool recv_uint64(int sock, uint64_t &out) {
    PacketType type;
    std::vector<char> buffer;
    if (!recv_packet(sock, type, buffer)) {
        return false;
    }

    if (type != PacketType::UINT64 || buffer.empty()) {
        return false;
    }

    std::memcpy(&out, buffer.data(), sizeof(uint64_t));
    return true;
}

bool recv_channelInfo(int sock, ChannelInfo &out) {
    PacketType type;
    std::vector<char> buffer;
    if (!recv_packet(sock, type, buffer))
        return false;
    if (type != PacketType::CHANNEL_INFO)
        return false;

    const char *ptr = buffer.data();
    size_t offset = 0;

    // id
    uint32_t id_net;
    std::memcpy(&id_net, ptr, sizeof(id_net));
    out.id = ntohl(id_net);
    offset += sizeof(id_net);

    // is_voice
    out.is_voice = static_cast<bool>(ptr[offset]);
    offset += 1;

    // string length
    uint32_t name_len_net;
    std::memcpy(&name_len_net, ptr + offset, sizeof(name_len_net));
    uint32_t name_len = ntohl(name_len_net);
    offset += sizeof(name_len_net);

    // string
    out.name.assign(ptr + offset, name_len);

    return true;
}

bool recv_userInfo(int sock, UserInfo &out) {
    PacketType type;
    std::vector<char> buffer;
    if (!recv_packet(sock, type, buffer))
        return false;
    if (type != PacketType::USER_INFO)
        return false;

    const char *ptr = buffer.data();
    size_t offset = 0;

    // id
    uint32_t id_net;
    std::memcpy(&id_net, ptr, sizeof(id_net));
    out.id = ntohl(id_net);
    offset += sizeof(id_net);

    // is_online
    out.is_online = static_cast<bool>(ptr[offset]);
    offset += 1;

    // string length
    uint32_t name_len_net;
    std::memcpy(&name_len_net, ptr + offset, sizeof(name_len_net));
    uint32_t name_len = ntohl(name_len_net);
    offset += sizeof(name_len_net);

    // string
    out.name.assign(ptr + offset, name_len);

    return true;
}

bool recv_message(int sock, MessageInfo &m) {
    recv_uint(sock, m.userId);
    recv_uint(sock, m.timestamp);
    recv_string(sock, m.msg);

    return true;
}