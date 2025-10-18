#include "config.h"
#include "crossSockets.h"
#include "logger.h"
#include "mainwindow.h"
#include "packets.h"
#include "workers/periodic_10.h"
#include "workers/socket_reader.h"
#include "workers/socket_sender.h"
#include <QApplication>
#include <QStyleFactory>
#include <QThread>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

std::vector<QThread *> qThreads;

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

    qThreads.push_back(thread);
    qThreads.push_back(thread2);
    qThreads.push_back(thread3);
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

    crossSockets::initializeSockets();

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

    // Force Fusion style for consistent modern UI
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Optional: dark palette
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(32, 35, 38));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(20, 22, 24));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Highlight, QColor(142, 45, 197).lighter());
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);

    app.setStyleSheet(R"(
    QMainWindow > QWidget {
 background-color: rgb(32, 35, 38);
 border-radius: 0px;
}


#membersList, #mainArea > *, #widget > * {
    border: 1px solid rgba(75, 75, 75, 1);
}

#membersList:focus, #mainArea > *:focus, #widget > *:focus {
    border: 1px solid rgb(169, 0, 211);
}

QWidget {
	background-color: rgb(20, 22, 24);
    border-radius: 5px;
}

QWidget:focus {
    border-radius: 5px;
    border: 1px solid rgb(169, 0, 211);
}

#userBar{
background-color: rgb(20, 22, 24);
border-radius: 5px;
border: 1px solid #444444;
}


QScrollBar:vertical {
    background-color: rgba(75, 75, 75, 1); /* track color */
    width: 8px;               /* scrollbar width */
    margin: 0px;               /* space at top/bottom for arrows */
    border-radius: 5px;
}

/* Handle (the draggable part) */
QScrollBar::handle:vertical {
    background-color: rgba(112, 0, 139, 1); 
    min-height: 10px;
    border-radius: 5px;
}

/* Hover effect */
QScrollBar::handle:vertical:hover {
    background-color: rgb(169, 0, 211);
}

/* Top/Bottom buttons (optional) */
QScrollBar::sub-line:vertical,
QScrollBar::add-line:vertical {
    height: 0px; /* hide buttons */
    subcontrol-origin: margin;
}

/* Arrows (optional, hidden here) */
QScrollBar::up-arrow:vertical,
QScrollBar::down-arrow:vertical {
    width: 0; 
    height: 0;
}


)");

    MainWindow window;
    startWorkers(sock, window);
    window.init();
    window.show();
    app.exec();

    crossSockets::closeSocket(sock);

    LOG_DEBUG("Waiting for threads to finish...");
    for (const auto t : qThreads) {
        if (t->isRunning()) {
            t->quit();
        }
    }

    for (const auto t : qThreads) {
        t->wait();
        delete t;
    }
    LOG_DEBUG("All stopped");

    Logger::shutdown();
    exit(EXIT_SUCCESS); // Exit here beacuse Windows gets stuck after the return call (Maybe in QApplication destructor?)
    return 0;
}
