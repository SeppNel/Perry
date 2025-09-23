#include "periodic_10.h"
#include <QThread>
#include <QTimer>
#include <cstdint>

#define UPDATE_FREQ_S 10

std::vector<char> empty;

void Periodic_10::init(int s) {
    sock = s;
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Periodic_10::update);
    timer->start(UPDATE_FREQ_S * 1000);
    update();
}

void Periodic_10::update() {
    PacketHeader ch_h = {(uint8_t)PacketType::LIST_CHANNELS, 0};
    PacketHeader us_h = {(uint8_t)PacketType::LIST_USERS, 0};

    emit sendPacket(ch_h, empty);
    emit sendPacket(us_h, empty);
}
