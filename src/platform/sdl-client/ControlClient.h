#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

class ControlClient {
public:
    ControlClient();
    ~ControlClient();

    // Start background control connection to host:port and register localUdpPort
    bool start(const std::string& host, int port, uint16_t localUdpPort);
    // Send an input frame (action, key)
    void sendInput(uint8_t action, uint8_t key);
    // Stop and join thread
    void stop();

    // Query last known player id (-1 if unknown)
    int lastPlayerId() const { return m_playerId.load(); }
    // Query whether control TCP is currently connected
    bool connected() const { return m_connected.load(); }

private:
    void run(const std::string host, int port, uint16_t localUdpPort);

    std::thread m_thread;
    std::atomic<bool> m_running;
    int m_sockfd;
    std::mutex m_sendMutex;
    std::atomic<int> m_playerId{ -1 };
    std::atomic<bool> m_connected{ false };
};
