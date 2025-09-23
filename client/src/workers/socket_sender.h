#pragma once
#include "packets.h"
#include <QObject>
#include <queue>
#include <vector>

class SocketSender : public QObject {
    Q_OBJECT

  public:
    void init(int s);

  public slots:
    void enqueuePacket(const PacketHeader &header, const std::vector<char> &payload);

  private:
    int sock;
    std::queue<PacketHeader> header_fifo;
    std::deque<char> payload_fifo;

    void handleMessage(const PacketHeader &header);
    void handleListMessages(const PacketHeader &header);

  private slots:
    void run();
};
