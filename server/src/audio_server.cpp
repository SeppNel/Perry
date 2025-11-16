#include "audio_server.h"
#include "config.h"
#include "logger.h"
#include "packets.h"
#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// Configuration
#define CHUNK_SIZE 240 // 5ms at 48kHz

std::atomic<bool> AudioServer::running{true};

static std::unordered_map<uint32_t, std::vector<int>> clients_per_channel;
static std::mutex clients_mutex;

ssize_t findClientIndex(uint32_t channel, int socket) {
    ssize_t index = 0;
    for (const int c : clients_per_channel[channel]) {
        if (c == socket) {
            return index;
        }
        index++;
    }

    return -1;
}

// Network receiving thread
void network_thread(int client_socket) {

    uint32_t channel;
    recv_uint(client_socket, channel);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients_per_channel[channel].push_back(client_socket);
    }

    std::vector<float> chunk(CHUNK_SIZE);

    LOG_INFO("Client connected, receiving audio...");

    while (AudioServer::running.load()) {
        bool got = recv_all(client_socket, chunk.data(), CHUNK_SIZE * sizeof(float));
        if (!got) {
            LOG_INFO("Client disconnected from voice");
            break;
        }

        for (const int client : clients_per_channel[channel]) {
            /*
            if (client == client_socket) {
                continue;
            }
            */

            send_all(client, chunk.data(), CHUNK_SIZE * sizeof(float));
        }
    }

    close(client_socket);

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        ssize_t index = findClientIndex(channel, client_socket);
        clients_per_channel[channel].erase(clients_per_channel[channel].begin() + index);
    }
}

void AudioServer::run() {
    // Set up TCP server
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(Config::port_voice);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return;
    }

    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        return;
    }

    LOG_INFO("Audio server listening on port " + std::to_string(Config::port_voice));
    LOG_INFO("Waiting for client connections...");

    while (running.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        LOG_INFO("Client connected from " + std::string(inet_ntoa(client_addr.sin_addr)));

        // Configure client socket for low latency
        int flag = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        // Increase recv buffer
        int rcvbuf = CHUNK_SIZE * sizeof(float) * 16;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        // Handle client in separate thread
        std::thread client_thread(network_thread, client_socket);
        client_thread.detach();
    }

    // Cleanup
    running = false;

    close(server_socket);

    return;
}
