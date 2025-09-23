#include "DbManager.h"
#include "common_data.h"
#include <cstdint>
#include <iostream>
#include <nanodbc/nanodbc.h>
#include <string>
#include <vector>

const char *connstr = NANODBC_TEXT("Driver={MySQL};Server=127.0.0.1;Database=perrydb;Uid=perryuser;Pwd=perrypass;big_packets=1");
nanodbc::connection conn(connstr);

namespace DbManager {
uint32_t getUserId(const std::string &username) {
    try {
        nanodbc::statement statement(conn);

        nanodbc::prepare(statement, "SELECT id FROM users WHERE username = ?;");
        statement.bind(0, username.c_str());
        auto result = nanodbc::execute(statement);
        if (result.next()) {
            return result.get<uint32_t>(0);
        }

        throw;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        throw e;
    }
}

std::string getUserPassword(const uint32_t id) {
    try {
        nanodbc::statement statement(conn);

        nanodbc::prepare(statement, "SELECT password FROM users WHERE id = ?;");
        statement.bind(0, &id);
        auto result = nanodbc::execute(statement);
        if (result.next()) {
            return result.get<std::string>(0);
        }

        throw;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        throw e;
    }
}

std::vector<ChannelInfo> getChannels() {
    try {
        std::vector<ChannelInfo> output;
        auto result = nanodbc::execute(conn, "SELECT id, name, is_voice FROM channels;");
        while (result.next()) {
            uint id = result.get<uint>(0);
            bool is_voice = result.get<uint>(2);
            std::string name = result.get<std::string>(1);

            output.emplace_back(id, is_voice, name);
        }

        return output;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        throw e;
    }
}

std::vector<UserInfo> getUsers() {
    try {
        std::vector<UserInfo> output;
        auto result = nanodbc::execute(conn, "SELECT id, username FROM users WHERE disabled = 0;");
        while (result.next()) {
            uint32_t id = result.get<uint32_t>(0);
            std::string name = result.get<std::string>(1);

            output.emplace_back(id, false, name);
        }

        return output;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        throw e;
    }
}

bool saveMessage(const std::string &msg, const uint32_t channelId, const uint32_t userId) {
    try {
        nanodbc::statement statement(conn);

        nanodbc::prepare(statement, "INSERT INTO messages (text, user_id, channel_id, date) VALUES (?, ?, ?, current_timestamp);");
        statement.bind(0, msg.c_str());
        statement.bind(1, &userId);
        statement.bind(2, &channelId);
        nanodbc::execute(statement);
        return true;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
}

std::vector<MessageInfo> getMessages(const uint32_t channelId) {
    try {
        std::vector<MessageInfo> output;

        nanodbc::statement statement(conn);

        nanodbc::prepare(statement, "SELECT text, user_id, UNIX_TIMESTAMP(date) FROM messages WHERE channel_id = ?;");
        statement.bind(0, &channelId);
        auto result = nanodbc::execute(statement);
        while (result.next()) {
            std::string msg = result.get<std::string>(0);
            uint32_t userId = result.get<uint32_t>(1);
            uint32_t timestamp = result.get<uint32_t>(2);
            output.emplace_back(userId, timestamp, msg);
        }

        return output;
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        throw e;
    }
}
} // namespace DbManager
