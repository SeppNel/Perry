#include "mainwindow.h"
#include "packets.h"
#include "workers/periodic_10.h"
#include "workers/socket_reader.h"
#include "workers/socket_sender.h"
#include <QApplication>
#include <QThread>
#include <arpa/inet.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <unistd.h>

#define PORT 9020

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

void receive_messages(int sock) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int valread = read(sock, buffer, 1024);
        if (valread <= 0)
            break;
        std::cout << buffer << std::endl;
    }
}

bool login(int sock) {
    std::string username = "test";
    std::string passwd = "test";
    uint8_t username_len = username.size();

    std::cout << "Sending username" << std::endl;
    send_string(sock, username);
    std::cout << "Sending passwd" << std::endl;
    send_string(sock, passwd);

    uint8_t result; // Allocate a receive buffer

    if (!recv_code(sock, result)) {
        std::cout << "Error from server" << std::endl;
        return false;
    }
    return result;
}

int main(int argc, char **argv) {
    int sock = 0;
    struct sockaddr_in serv_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        std::cout << "Could not connect to server" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Attempting login" << std::endl;
    if (!login(sock)) {
        std::cout << "Could not login" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "Login succesfull" << std::endl;

    QApplication app(argc, argv);

    MainWindow window;
    startWorkers(sock, window);
    window.init(sock);

    window.show();

    app.exec();

    close(sock);
    return 0;
}
