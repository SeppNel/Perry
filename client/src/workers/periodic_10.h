#pragma once
#include "packets.h"
#include <QObject>
#include <vector>

class Periodic_10 : public QObject {
    Q_OBJECT

  public:
    void init(int s);

  private:
    int sock;

  private slots:
    void update();

  signals:
    void sendPacket(const PacketHeader &header, const std::vector<char> &payload);
};