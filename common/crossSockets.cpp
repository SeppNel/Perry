#include "crossSockets.h"
#include "logger.h"

namespace crossSockets {

void setSocketOptions(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
#ifdef _WIN32
    setsockopt(sockfd, level, optname, (const char *)optval, optlen);
#else
    setsockopt(sockfd, level, optname, optval, optlen);
#endif
}

void initializeSockets() {
#ifdef _WIN32
    WSADATA wsaData;

    // Initialize Winsock (Windows-specific).
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_CRITICAL("Server: Winsock initialization failed. Error: " + std::to_string(WSAGetLastError()));
        return;
    }
#endif

    return;
}

void closeSocket(int s) {
    shutdown(s, SHUT_RDWR);

#ifdef _WIN32
    closesocket(s);
    WSACleanup();
#else
    close(s);
#endif

    return;
}

} // namespace crossSockets