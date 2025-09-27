#pragma once

#include <string>

struct DiscoveryResult {
    std::string host;
    int udpPort;
    int tcpPort;
};

// Blocks until a discovery packet is received or timeoutSeconds elapses (0 = block forever).
// Returns true if a server was discovered and fills out 'out'.
bool udp_discover_server(int listenPort, int timeoutSeconds, DiscoveryResult& out);
