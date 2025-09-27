#include "ControlClient.h"
#include "../web/StreamingCommon.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <vector>
#include <chrono>
#include <cstdarg>

static void log_info(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); fprintf(stdout, "[control] "); vfprintf(stdout, fmt, ap); fprintf(stdout, "\n"); va_end(ap);
}

ControlClient::ControlClient()
    : m_running(false), m_sockfd(-1) {}


ControlClient::~ControlClient() {
    stop();
}

bool ControlClient::start(const std::string& host, int port, uint16_t localUdpPort) {
    if (m_running.load()) return false;
    m_running.store(true);
    m_playerId.store(-1);
    m_thread = std::thread(&ControlClient::run, this, host, port, localUdpPort);
    return true;
}

void ControlClient::stop() {
    m_running.store(false);
    if (m_sockfd >= 0) {
        close(m_sockfd);
        m_sockfd = -1;
    }
    if (m_thread.joinable()) m_thread.join();
}

void ControlClient::run(const std::string host, int port, uint16_t localUdpPort) {
    // Connect with simple reconnect/backoff loop to be robust to server restarts
    int backoffMs = 200;
    std::vector<uint8_t> recvBuf;
    while (m_running.load()) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            log_info("socket() failed: %s", strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
            backoffMs = std::min(5000, backoffMs * 2);
            continue;
        }
        struct sockaddr_in serv{}; serv.sin_family = AF_INET; serv.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &serv.sin_addr);
        if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) != 0) {
            close(sock);
            log_info("connect failed: %s", strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
            backoffMs = std::min(5000, backoffMs * 2);
            continue;
        }

    // Reset backoff and register our UDP port
        backoffMs = 200;
        log_info("connected to control %s:%d", host.c_str(), port);
    m_connected.store(true);

        auto sendAll = [&](const void* data, size_t len) {
            const uint8_t* p = (const uint8_t*)data;
            while (len > 0) {
                ssize_t w = write(sock, p, len);
                if (w <= 0) return false;
                p += w; len -= w;
            }
            return true;
        };

        mgba::ControlHeader hdr;
        hdr.type = (uint8_t)mgba::CM_REGISTER;
        hdr.len = htons(2);
        uint16_t port_net = htons(localUdpPort);
        if (!sendAll(&hdr, sizeof(hdr)) || !sendAll(&port_net, sizeof(port_net))) {
            close(sock); m_sockfd = -1; log_info("failed to send register");
            continue;
        }

        m_sockfd = sock;
        recvBuf.clear();
        // Receive loop
        char tmp[512];
        while (m_running.load()) {
            ssize_t r = recv(sock, tmp, sizeof(tmp), 0);
            if (r <= 0) break;
            recvBuf.insert(recvBuf.end(), tmp, tmp + r);
            // try to parse frames
            while (recvBuf.size() >= sizeof(mgba::ControlHeader)) {
                mgba::ControlHeader rh;
                memcpy(&rh, recvBuf.data(), sizeof(rh));
                uint16_t plen = ntohs(rh.len);
                if (recvBuf.size() < sizeof(rh) + plen) break;
                std::vector<uint8_t> payload(recvBuf.begin() + sizeof(rh), recvBuf.begin() + sizeof(rh) + plen);
                recvBuf.erase(recvBuf.begin(), recvBuf.begin() + sizeof(rh) + plen);
                if (rh.type == mgba::CM_PLAYERINFO) {
                    if (payload.size() >= 2) {
                        int playerId = payload[0];
                        int total = payload[1];
                        m_playerId.store(playerId);
                        log_info("player info: id=%d total=%d", playerId, total);
                    }
                } else if (rh.type == mgba::CM_CONNECTION) {
                    log_info("received CM_CONNECTION from server");
                } else if (rh.type == mgba::CM_JSON) {
                    // ignore for now, could parse JSON messages later
                }
            }
        }

        // cleanup on disconnect
        close(sock);
        m_sockfd = -1;
        m_connected.store(false);
        if (m_running.load()) log_info("control connection lost, retrying...");
    }
}

void ControlClient::sendInput(uint8_t action, uint8_t key) {
    std::lock_guard<std::mutex> lk(m_sendMutex);
    if (m_sockfd < 0) return;
    mgba::ControlHeader hdr;
    hdr.type = (uint8_t)mgba::CM_INPUT;
    hdr.len = htons(2);
    uint8_t payload[2] = { action, key };
    // best-effort; ignore partial write handling for simplicity
    write(m_sockfd, &hdr, sizeof(hdr));
    write(m_sockfd, payload, sizeof(payload));
}
