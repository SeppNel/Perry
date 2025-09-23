#pragma once
#include <cstdint>
#include <string>

static_assert(true); // Dummy statement because of https://github.com/clangd/clangd/issues/1167
#pragma pack(push, 1)

// Channel data
struct ChannelInfo {
    uint32_t id;
    bool is_voice;
    std::string name;
};

struct UserInfo {
    uint32_t id;
    bool is_online;
    std::string name;
};

struct MessageInfo {
    uint32_t userId;
    uint32_t timestamp;
    std::string msg;
};

#pragma pack(pop)