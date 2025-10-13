#ifdef __unix__

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#endif

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h> // for inet_pton, inet_ntop, etc.

#define ssize_t int
#define socklen_t int
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
// #define INET6_ADDRSTRLEN 46

#endif

namespace crossSockets {

void initializeSockets();
void setSocketOptions(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

} // namespace crossSockets