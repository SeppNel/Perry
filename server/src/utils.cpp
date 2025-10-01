#include "utils.h"
#include <iostream>

ssize_t findClientIndex(const uint32_t userId, const std::vector<Client_t> &clients) {
    ssize_t index = 0;
    for (Client_t c : clients) {
        if (c.userId == userId) {
            return index;
        }
        index++;
    }

    return -1;
}

std::vector<fs::path> getFilesByExtension(const std::string &folderPath, const std::string &extension) {
    std::vector<fs::path> files;

    try {
        for (const auto &entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file() && entry.path().extension() == extension) {
                files.push_back(entry.path());
            }
        }
    } catch (const fs::filesystem_error &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return files;
}