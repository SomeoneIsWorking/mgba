#include "UdpDiscovery.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <chrono>
#include <thread>
#include "../web/StreamingCommon.h"

bool udp_discover_server(int listenPort, int timeoutSeconds, DiscoveryResult& out) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listenPort);
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return false; }

    // set recv timeout if requested
    if (timeoutSeconds > 0) {
        struct timeval tv{};
        tv.tv_sec = timeoutSeconds;
        tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    const size_t BUF_SZ = 1024;
    char buf[BUF_SZ];
    struct sockaddr_in src{};
    socklen_t srclen = sizeof(src);
    ssize_t n = recvfrom(s, buf, BUF_SZ, 0, (struct sockaddr*)&src, &srclen);
    if (n <= 0) { close(s); return false; }
    // expect our compact Packet
    if ((size_t)n < sizeof(uint32_t) + 1 + 64 + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint64_t)) { close(s); return false; }

    mgba::DiscoveryPacket pkt;
    if (sizeof(pkt) > (size_t)n) { close(s); return false; }
    memcpy(&pkt, buf, sizeof(pkt));
    if (ntohl(pkt.magic) != mgba::DISCOVERY_MAGIC) { close(s); return false; }
    std::string host(pkt.host, strnlen(pkt.host, sizeof(pkt.host)));
    uint16_t udpPort = ntohs(pkt.udpPort);
    uint16_t tcpPort = ntohs(pkt.tcpPort);

    if (host.empty()) host = inet_ntoa(src.sin_addr);

    out.host = host;
    out.udpPort = udpPort;
    out.tcpPort = tcpPort;

    close(s);
    return true;
}
