#pragma once
#include "common_data.h"
#include <arpa/inet.h>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <vector>

// packet type identifiers
enum class PacketType : uint8_t {
    // Generic Types
    CODE, // uint8_t
    INT,  // int32_t
    UINT, // uint32_t
    TEXT, // std::string
    // Requests
    LIST_CHANNELS,
    LIST_USERS,
    LIST_MESSAGES,
    MESSAGE,
    // Responses
    CHANNEL_INFO,
    USER_INFO
};

// header format (packed to avoid padding)
#pragma pack(push, 1)
struct PacketHeader {
    uint8_t type;    // network order
    uint32_t length; // payload length in bytes, network order
};
#pragma pack(pop)

// send a packet
bool send_packet(int sock, PacketType type, const std::vector<char> &data);

bool send_string(int sock, const std::string &str);

bool send_packet(int sock, PacketType type, const void *data, size_t size);

template <typename T>
bool send_packet(int sock, PacketType type, const T &value) {
    return send_packet(sock, type, &value, sizeof(T));
}

bool send_channelInfo(int sock, const ChannelInfo &c);
bool send_userInfo(int sock, const UserInfo &u);
bool send_message(int sock, const MessageInfo &msg);

// receive a packet
bool recv_packet(int sock, PacketType &type, std::vector<char> &data);

bool recv_code(int sock, uint8_t &code);

bool recv_string(int sock, std::string &str);

bool recv_int(int sock, int32_t &out);

bool recv_uint(int sock, uint32_t &out);

bool recv_channelInfo(int sock, ChannelInfo &c);
bool recv_userInfo(int sock, UserInfo &c);
bool recv_message(int sock, MessageInfo &m);