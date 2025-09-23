// socket_reader.cpp
#include "socket_reader.h"
#include "common_data.h"
#include "packets.h"
#include <iostream>

void SocketReader::init(int s) {
    sock = s;
    run();
}

void SocketReader::run() {
    std::vector<char> buffer;
    while (true) {
        buffer.clear();
        PacketType pType;
        if (!recv_packet(sock, pType, buffer)) {
            std::cout << "Socket closed or error\n";
            break;
        }

        switch (pType) {
        case PacketType::LIST_CHANNELS: {
            handler_ListChannels();
            break;
        }
        case PacketType::LIST_USERS: {
            handler_ListUsers();
            break;
        }
        case PacketType::MESSAGE: {
            handler_Message();
            break;
        }
        default:
            std::cout << "Unknown packet type\n";
            break;
        }
    }
}

void SocketReader::handler_ListChannels() {
    uint32_t n_channels = 0;
    recv_uint(sock, n_channels);

    std::vector<ChannelInfo> ch;
    for (uint32_t i = 0; i < n_channels; i++) {
        ChannelInfo c;
        recv_channelInfo(sock, c);
        ch.push_back(c);
    }
    emit channelsReady(ch);
}

void SocketReader::handler_ListUsers() {
    uint32_t n_users = 0;
    recv_uint(sock, n_users);

    std::vector<UserInfo> users;
    for (uint32_t i = 0; i < n_users; i++) {
        UserInfo u;
        recv_userInfo(sock, u);
        users.push_back(u);
    }
    emit usersReady(users);
}

void SocketReader::handler_Message() {
    MessageInfo msg;
    recv_message(sock, msg);
    emit newMessage(msg);
}