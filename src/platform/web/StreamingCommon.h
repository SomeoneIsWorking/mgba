#pragma once

#include <cstdint>
#include <arpa/inet.h>
// Expose a minimal set of FFmpeg enums for shared stream metadata
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
}


namespace mgba {
// Discovery packet magic and default streaming parameters
constexpr uint32_t DISCOVERY_MAGIC = 0x4D474241; // 'MGBA'
constexpr int STREAM_WIDTH = 240;
constexpr int STREAM_HEIGHT = 160;
constexpr int STREAM_FPS = 60;

// Audio streaming parameters
constexpr int STREAM_AUDIO_SAMPLE_RATE_CODEC = 44100; // Fixed encoding sample rate (what both sides use)
constexpr int STREAM_AUDIO_CHANNELS = 2; // Stereo

// Color metadata: use full-range (JPEG/full) and BT.601 / SMPTE170M primaries/space
static const AVColorRange STREAM_COLOR_RANGE = AVCOL_RANGE_JPEG; // full range 0-255
static const AVColorSpace STREAM_COLORSPACE = AVCOL_SPC_SMPTE170M;
static const AVColorPrimaries STREAM_COLOR_PRIMARIES = AVCOL_PRI_SMPTE170M;
static const AVColorTransferCharacteristic STREAM_COLOR_TRC = AVCOL_TRC_IEC61966_2_1; // sRGB-ish transfer

// Discovery packet layout (network byte order for multi-byte fields)
struct DiscoveryPacket {
    uint32_t magic;       // htonl(DISCOVERY_MAGIC)
    uint8_t version;      // 1
    char host[64];        // NUL-terminated UTF-8
    uint16_t udpPort;     // htons
    uint16_t tcpPort;     // htons
    uint64_t timestamp_ms; // custom hton64
} __attribute__((packed));

// Simple binary framing for TCP control messages between client and server.
// Header: 1 byte type, 2 bytes payload length (network byte order), followed by payload.
enum ControlMessageType : uint8_t {
    CM_REGISTER = 1, // payload: uint16_t udpPort (network order)
    CM_INPUT = 2,    // reserved for future (could be structured)
    CM_CONNECTION = 3, // reserved
    CM_PLAYERINFO = 4,
    CM_JSON = 0xFF   // indicates following payload is JSON text
};

struct ControlHeader {
    uint8_t type;
    uint16_t len; // network byte order
} __attribute__((packed));

// CM_INPUT payload layout: 1 byte action, 1 byte keycode
enum InputAction : uint8_t {
    ACTION_PRESS = 1,
    ACTION_RELEASE = 2
};

// Portable client keycodes (protocol-level). Keep small values.
enum InputKey : uint8_t {
    KEY_UP = 1,
    KEY_DOWN = 2,
    KEY_LEFT = 3,
    KEY_RIGHT = 4,
    KEY_Z = 10,
    KEY_X = 11,
    KEY_A = 12,
    KEY_S = 13,
    KEY_ENTER = 20,
    KEY_TAB = 21
};

// Portable 64-bit host/network conversions (works by swapping 32-bit halves)
static inline uint64_t hton64(uint64_t v) {
    uint32_t hi = htonl((uint32_t)(v >> 32));
    uint32_t lo = htonl((uint32_t)(v & 0xFFFFFFFF));
    return ((uint64_t)lo << 32) | hi;
}

static inline uint64_t ntoh64(uint64_t v) {
    uint32_t hi = ntohl((uint32_t)(v & 0xFFFFFFFF));
    uint32_t lo = ntohl((uint32_t)(v >> 32));
    return ((uint64_t)lo << 32) | hi;
}

}