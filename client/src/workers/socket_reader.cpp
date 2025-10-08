// socket_reader.cpp
#include "socket_reader.h"
#include "common_data.h"
#include "logger.h"
#include "packets.h"
#include <cstdint>
#include <qpixmap.h>
#include <unordered_map>

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
            LOG_INFO("Socket closed or error");
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
        case PacketType::LIST_USER_IMGS: {
            handler_ListUserImgs();
            break;
        }
        default:
            LOG_WARNING("Unknown packet type");
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

void SocketReader::handler_ListUserImgs() {
    std::unordered_map<uint32_t, QPixmap> userImageMap;
    uint32_t n_users = 0;
    recv_uint(sock, n_users);
    for (uint32_t i = 0; i < n_users; i++) {
        uint32_t uid;
        recv_uint(sock, uid);

        uint64_t size;
        recv_uint64(sock, size);

        std::vector<char> buffer;
        buffer.reserve(size);

        PacketType p;
        recv_packet(sock, p, buffer);

        QPixmap pixmap;

        pixmap.loadFromData(reinterpret_cast<const uchar *>(buffer.data()), buffer.size(), "PNG");
        userImageMap[uid] = pixmap;
    }

    emit usersImgsReady(userImageMap);
}