#include "config.h"
#include "logger.h"
#include "mainwindow.h"
#include "packets.h"
#include "workers/periodic_10.h"
#include "workers/socket_reader.h"
#include "workers/socket_sender.h"
#include <QApplication>
#include <QThread>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <netinet/tcp.h>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

void startWorkers(int sock, MainWindow &mainwindow) {
    // Prepare periodic background thread
    QThread *thread = new QThread();
    Periodic_10 *updater = new Periodic_10();

    updater->moveToThread(thread);

    QObject::connect(thread, &QThread::started, [updater, sock]() {
        updater->init(sock);
    });

    QObject::connect(thread, &QThread::finished, updater, &QObject::deleteLater);

    // Prepare receiver background thread
    QThread *thread2 = new QThread();
    SocketReader *receiver = new SocketReader();

    receiver->moveToThread(thread2);

    QObject::connect(receiver, &SocketReader::channelsReady, &mainwindow, &MainWindow::populateChannels);
    QObject::connect(receiver, &SocketReader::usersReady, &mainwindow, &MainWindow::updateUsers);
    QObject::connect(receiver, &SocketReader::newMessage, &mainwindow, &MainWindow::addMessage);
    QObject::connect(receiver, &SocketReader::usersImgsReady, &mainwindow, &MainWindow::onUsersImgsReady);
    QObject::connect(thread2, &QThread::started, [receiver, sock]() {
        receiver->init(sock);
    });

    QObject::connect(thread2, &QThread::finished, receiver, &QObject::deleteLater);

    // Prepare sender thread
    QThread *thread3 = new QThread();
    SocketSender *sender = new SocketSender();

    sender->moveToThread(thread3);

    QObject::connect(&mainwindow, &MainWindow::sendPacket, sender, &SocketSender::enqueuePacket);
    QObject::connect(updater, &Periodic_10::sendPacket, sender, &SocketSender::enqueuePacket);

    QObject::connect(thread3, &QThread::started, [sender, sock]() {
        sender->init(sock);
    });

    QObject::connect(thread3, &QThread::finished, sender, &QObject::deleteLater);

    // Start all threads
    thread->start();
    thread2->start();
    thread3->start();
}

bool login(int sock) {
    std::string username = Config::username;
    std::string passwd = Config::password;
    uint8_t username_len = username.size();

    LOG_DEBUG("Sending username");
    send_string(sock, username);
    LOG_DEBUG("Sending passwd");
    send_string(sock, passwd);

    uint8_t result; // Allocate a receive buffer

    if (!recv_code(sock, result)) {
        LOG_ERROR("Error from server");
        return false;
    }
    return result;
}

int main(int argc, char **argv) {
    Logger::init("perry.log", LogLevel::DEBUG, true, false);
    if (!Config::init("configFile.yml")) {
        LOG_ERROR("Could not read config file");
        return 1;
    }

    int sock = 0;
    struct sockaddr_in serv_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(Config::server_port_text);

    inet_pton(AF_INET, Config::server_addr.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        LOG_ERROR("Could not connect to server");
        return EXIT_FAILURE;
    }

    LOG_DEBUG("Attempting login");
    if (!login(sock)) {
        LOG_ERROR("Could not login to server");
        return 1;
    }
    LOG_DEBUG("Login successful");

    // Send User Avatar to server
    if (fs::exists(Config::avatar_path)) {
        send_packet(sock, PacketType::USER_IMAGE, NULL, 0);
        send_image(sock, Config::avatar_path);
    }

    QApplication app(argc, argv);

    MainWindow window;
    startWorkers(sock, window);
    window.init();

    window.show();

    app.exec();

    close(sock);
    return 0;
}
