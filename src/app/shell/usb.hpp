#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/result.hpp"
#include "core/task.hpp"
#include "net/ftp.hpp"
#include "player/engine.hpp"

namespace playfin::app {

// USB drives. The app's sandbox does not show them (/mnt/usbN is missing from inside it, docs/ps4-notes.md), so
// Playfin reaches them through GoldHEN's FTP server on the console itself. Playfin reads anywhere on a drive and
// writes only in a "Playfin" folder at its root (downloads); every path is checked before it is used.

constexpr const char* kConsoleFtp = "ftp://127.0.0.1:2121";
constexpr const char* kUsbFolder = "Playfin";  // where downloads go on a drive

// "/mnt/usbN" (N 0 to 7), or a path inside one, valid for the FTP client
bool isUsbPath(const std::string& path);
// "/mnt/usbN" of a USB path, empty for any other path
std::string usbDriveOf(const std::string& path);
// "<drive>/Playfin/<name>" when `name` is a plain file name (no "/", no dots only), else empty
std::string usbWritablePath(const std::string& drive, const std::string& name);
// the player's address of a file on a drive: "pfusb://<path>"
std::string usbUrl(const std::string& path);

// The drives plugged in ("/mnt/usb0", …), in order.
core::Result<std::vector<std::string>> usbDrives(const net::FtpClient& ftp, const core::CancelToken& token = {});

enum class UsbKind { Folder, Video, Audio, Subtitle, Other };
UsbKind usbKind(const net::FtpEntry& entry);
// a file's name without its extension ("Film (2026)" for "Film (2026).mkv")
std::string usbTitle(const std::string& name);
// the subtitle files beside a video ("Film.srt", "Film.fr.srt", "Film.en.ass"), from its folder's entries
std::vector<std::string> usbSubtitlesFor(const std::string& video, const std::vector<net::FtpEntry>& entries);

// The player's reader of "pfusb://" files through `ftp` (USB paths only).
player::StreamOpener usbOpener(std::shared_ptr<const net::FtpClient> ftp);

}  // namespace playfin::app
