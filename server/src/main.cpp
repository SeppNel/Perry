#include "DbManager.h"
#include "audio_server.h"
#include "common_data.h"
#include "config.h"
#include "logger.h"
#include "packets.h"
#include "utils.h"
#include <arpa/inet.h>
#include <bcrypt.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nanodbc/nanodbc.h>
#include <string>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

#define BCRYPT_ROUNDS 12

std::string img_store_path;

std::vector<Client_t> clients;
std::mutex clients_mutex;

bool authenticate(const int sock, uint32_t &userId) {
    std::string username;
    if (!recv_string(sock, username)) {
        LOG_ERROR("Username not received");
        return false;
    }

    try {
        userId = DbManager::getUserId(username);
    } catch (...) {
        LOG_ERROR("Could not get id from DB");
        return false;
    }

    std::string password;
    if (!recv_string(sock, password)) {
        LOG_ERROR("Password not received");
        return false;
    }

    std::string saved_passwd;
    try {
        saved_passwd = DbManager::getUserPassword(userId);
    } catch (...) {
        LOG_ERROR("Could not get password from DB");
        return false;
    }

    uint8_t result;
    if (bcrypt::validatePassword(password, saved_passwd)) {
        result = 1;
    } else {
        result = 0;
    }

    send_packet(sock, PacketType::CODE, result);
    return result;
}

void broadcast(const MessageInfo &msg) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (Client_t client : clients) {
        send_message(client.socket, msg);
    }
}

void handle_client(int sock) {
    uint32_t userId;
    if (!authenticate(sock, userId)) {
        close(sock);
        LOG_WARNING("Could not authenticate user");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.push_back({userId, sock});
    }

    LOG_INFO("Waiting for messages");
    std::vector<char> buffer;
    PacketType pType;
    while (true) {
        buffer.clear();
        if (!recv_packet(sock, pType, buffer)) {
            LOG_INFO("Client Disconnected");
            break;
        }

        switch (pType) {
        case PacketType::MESSAGE: {
            uint32_t channelId;
            std::string msg;
            recv_uint(sock, channelId);
            recv_string(sock, msg);
            LOG_DEBUG(msg);
            DbManager::saveMessage(msg, channelId, userId);

            const auto p1 = std::chrono::system_clock::now();

            uint32_t sec = std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count();

            MessageInfo mi = {userId, sec, msg};
            broadcast(mi);
            break;
        }
        case PacketType::LIST_CHANNELS: {
            send_packet(sock, PacketType::LIST_CHANNELS, NULL, 0);
            std::vector<ChannelInfo> channels = DbManager::getChannels();

            uint32_t num = channels.size();
            send_packet(sock, PacketType::UINT, num);

            for (const ChannelInfo &c : channels) {
                send_channelInfo(sock, c);
            }
            break;
        }
        case PacketType::LIST_USERS: {
            send_packet(sock, PacketType::LIST_USERS, NULL, 0);
            std::vector<UserInfo> users = DbManager::getUsers();

            for (UserInfo &u : users) {
                if (findClientIndex(u.id, clients) != -1) {
                    u.is_online = true;
                }
            }

            uint32_t num = users.size();
            send_packet(sock, PacketType::UINT, num);

            for (const UserInfo &u : users) {
                send_userInfo(sock, u);
            }
            break;
        }
        case PacketType::LIST_MESSAGES: {
            uint32_t channelId;
            recv_uint(sock, channelId);
            std::vector<MessageInfo> messages = DbManager::getMessages(channelId);
            for (const auto &msg : messages) {
                send_message(sock, msg);
            }
            break;
        }
        case PacketType::LIST_USER_IMGS: {
            std::vector<std::filesystem::path> images = getFilesByExtension(img_store_path, ".png");

            send_packet(sock, PacketType::LIST_USER_IMGS, NULL, 0);
            send_packet(sock, PacketType::UINT, images.size());

            for (const std::filesystem::path &img : images) {
                uint32_t uid = std::stoi(img.filename());
                send_packet(sock, PacketType::UINT, uid);
                send_image(sock, img);
            }

            break;
        }
        case PacketType::USER_IMAGE: {
            uint64_t size;
            recv_uint64(sock, size);

            std::vector<char> buffer;
            buffer.reserve(size);

            PacketType p;
            recv_packet(sock, p, buffer);

            // Check if file is actually a png by file header
            char png_header[8] = {'\x89', 'P', 'N', 'G', '\x0D', '\x0A', '\x1A', '\x0A'};
            bool valid_png = true;
            for (uint i = 0; i < 8; i++) {
                if (buffer[i] != png_header[i]) {
                    valid_png = false;
                    LOG_DEBUG("Expected: " + std::string(png_header) + ", Got: " + std::string(buffer.data(), 8));
                    break;
                }
            }

            if (!valid_png) {
                LOG_ERROR("Not a valid PNG");
                break;
            }

            fs::path save_path = img_store_path + std::to_string(userId) + ".png";

            if (save_path.has_parent_path()) {
                try {
                    fs::create_directories(save_path.parent_path());
                } catch (const fs::filesystem_error &e) {
                    LOG_ERROR("Failed to create directories: " + std::string(e.what()));
                    break;
                }
            }

            std::ofstream outFile(save_path, std::ios::binary);
            if (!outFile.write(buffer.data(), size)) {
                LOG_ERROR("Failed to write file");
            }

            break;
        }
        default: {
            LOG_WARNING("Unrecognized packet type");
            break;
        }
        }
    }

    close(sock);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        ssize_t index = findClientIndex(userId, clients);
        clients.erase(clients.begin() + index);
    }
}

int main() {
    Logger::init("perry.log", LogLevel::DEBUG, true, false);
    Config::init("./configFile.yml");
    DbManager::init();
    img_store_path = Config::storage_path + "images/";

    int server_main_socket;
    int client_new_socket;
    sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    server_main_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_main_socket == -1) {
        LOG_CRITICAL("Socket creation error");
        return -1;
    }
    setsockopt(server_main_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(Config::port_text);

    if (bind(server_main_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        LOG_CRITICAL("Socket creation error");
        return -1;
    }

    if (listen(server_main_socket, 5) == -1) {
        LOG_CRITICAL("Socket creation error");
        return -1;
    }

    std::thread audio_server(AudioServer::run);

    LOG_INFO("Server started on port " + std::to_string(Config::port_text));
    while (true) {
        client_new_socket = accept(server_main_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        std::thread(handle_client, client_new_socket).detach();
    }

    AudioServer::running.store(false);
    audio_server.join();

    return 0;
}
