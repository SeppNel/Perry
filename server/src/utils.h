#pragma once
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

struct Client_t {
    uint32_t userId;
    int socket;
};

ssize_t findClientIndex(const uint32_t userId, const std::vector<Client_t> &clients);
std::vector<fs::path> getFilesByExtension(const std::string &folderPath, const std::string &extension);