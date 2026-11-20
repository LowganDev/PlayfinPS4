#include <cstddef>  // orbis headers use size_t without including it
#include <cstdint>

#include <orbis/Pad.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/log.hpp"
#include "platform/remote.hpp"

// The TV's remote control (HDMI Device Link, CEC) reaches an application as a pad of the pad library: the
// remote-control port (16) of the system user (0xff), opened and read like a controller. Userland only: the same
// library and calls as for the controller. Seen 2026-11-20 on a PS4 with a TV's remote: the arrows are the
// direction bits of `buttons`; OK and Back set no bit but a key code in the byte at kKeyOffset (13 and 15), 0 when
// released.
namespace playfin::platform::backend {

namespace {

constexpr int kSystemUser = 0xff;
constexpr int kRemoteControlPort = 16;
// the first bytes of a sample, logged when it holds a key Playfin has not seen yet (media keys: which code each
// gives on a PS4 is still to learn); the keys seen say nothing
constexpr std::size_t kLoggedBytes = 0x70;
constexpr std::size_t kKeyOffset = 0x6c;

int gHandle = 0;
bool gTried = false;
unsigned char gLast[kLoggedBytes] = {};
bool gSeen = false;

// OK, Back, the arrows and the flag the console sets on a sample it also took for itself
bool seenBefore(const unsigned char* bytes, std::uint32_t buttons) {
    const std::uint8_t key = bytes[kKeyOffset];
    return (key == 0 || key == RemoteButtons::kKeyOk || key == RemoteButtons::kKeyBack) &&
           (buttons & ~(0xF0u | 0x80000000u)) == 0;
}

void logSample(const unsigned char* bytes, std::uint32_t buttons) {
    if (seenBefore(bytes, buttons)) {
        return;
    }
    // the timestamp changes at every sample: not compared
    unsigned char now[kLoggedBytes];
    std::memcpy(now, bytes, kLoggedBytes);
    std::memset(now + offsetof(OrbisPadData, timestamp), 0, sizeof(std::uint64_t));
    if (gSeen && std::memcmp(now, gLast, kLoggedBytes) == 0) {
        return;
    }
    gSeen = true;
    std::memcpy(gLast, now, kLoggedBytes);
    std::string hex;
    char two[4];
    for (std::size_t i = 0; i < kLoggedBytes; ++i) {
        std::snprintf(two, sizeof(two), "%02x", now[i]);
        hex += two;
        if (i % 4 == 3) {
            hex += ' ';
        }
    }
    PF_LOG_INFO("remote: buttons %08x | %s", static_cast<unsigned>(buttons), hex.c_str());
}

}  // namespace

bool readRemote(std::uint32_t& buttons, std::uint8_t& key) {
    if (!gTried) {
        gTried = true;
        gHandle = scePadOpen(kSystemUser, kRemoteControlPort, 0, nullptr);
        PF_LOG_INFO("remote: the remote-control port opened: %#x", static_cast<unsigned>(gHandle));
    }
    if (gHandle <= 0) {
        return false;
    }
    union {
        OrbisPadData data;
        unsigned char bytes[256];  // room should the system's structure be larger than the header's
    } sample;
    std::memset(&sample, 0, sizeof(sample));
    const int result = scePadReadState(gHandle, &sample.data);
    if (result != 0) {
        static int lastResult = 0;
        if (result != lastResult) {
            lastResult = result;
            PF_LOG_INFO("remote: read gives %#x", static_cast<unsigned>(result));
        }
        return false;
    }
    logSample(sample.bytes, sample.data.buttons);
    if (sample.data.connected == 0) {
        return false;
    }
    buttons = sample.data.buttons;
    key = sample.bytes[kKeyOffset];
    return true;
}

}  // namespace playfin::platform::backend
