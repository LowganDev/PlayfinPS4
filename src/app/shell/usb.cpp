#include "app/shell/usb.hpp"

#include <algorithm>
#include <cctype>

namespace playfin::app {

namespace {

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string extensionOf(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? std::string() : lower(name.substr(dot + 1));
}

class Reader : public player::StreamReader {
public:
    Reader(std::shared_ptr<const net::FtpClient> ftp, std::string path)
        : ftp_(std::move(ftp)), reader_(*ftp_, std::move(path)) {}
    long long size() override { return reader_.size(); }
    long long read(char* buffer, std::size_t size) override { return reader_.read(buffer, size); }
    bool seek(long long offset) override { return reader_.seek(offset); }
    void cancel() override { reader_.cancel(); }

private:
    std::shared_ptr<const net::FtpClient> ftp_;  // outlives the reader
    net::FtpReader reader_;
};

}  // namespace

std::string usbDriveOf(const std::string& path) {
    const std::string prefix = "/mnt/usb";
    if (path.compare(0, prefix.size(), prefix) != 0 || path.size() < prefix.size() + 1) {
        return {};
    }
    const char digit = path[prefix.size()];
    if (digit < '0' || digit > '7') {
        return {};
    }
    if (path.size() > prefix.size() + 1 && path[prefix.size() + 1] != '/') {
        return {};
    }
    return path.substr(0, prefix.size() + 1);
}

bool isUsbPath(const std::string& path) { return !usbDriveOf(path).empty() && net::validFtpPath(path); }

std::string usbWritablePath(const std::string& drive, const std::string& name) {
    if (usbDriveOf(drive) != drive || name.empty() || name.size() > 128 || name.find('/') != std::string::npos ||
        name == "." || name == "..") {
        return {};
    }
    const std::string path = drive + "/" + kUsbFolder + "/" + name;
    return isUsbPath(path) ? path : std::string();
}

std::string usbUrl(const std::string& path) { return std::string(player::kUsbScheme) + path; }

core::Result<std::vector<std::string>> usbDrives(const net::FtpClient& ftp, const core::CancelToken& token) {
    auto mounts = ftp.list("/mnt", token);
    if (!mounts.ok()) {
        return mounts.error();
    }
    std::vector<std::string> drives;
    for (const net::FtpEntry& entry : mounts.value()) {
        const std::string path = "/mnt/" + entry.name;
        if (!entry.directory || usbDriveOf(path) != path) {
            continue;
        }
        // a drive plugged in is a folder with a size (its cluster); an empty mount point has none and lists nothing
        if (entry.size > 0) {
            drives.push_back(path);
            continue;
        }
        const auto inside = ftp.list(path, token);
        if (inside.ok() && !inside.value().empty()) {
            drives.push_back(path);
        }
    }
    std::sort(drives.begin(), drives.end());
    return drives;
}

UsbKind usbKind(const net::FtpEntry& entry) {
    if (entry.directory) {
        return entry.name.empty() || entry.name.front() == '.' || entry.name == "$RECYCLE.BIN" ||
                       entry.name == "System Volume Information"
                   ? UsbKind::Other
                   : UsbKind::Folder;
    }
    if (entry.name.empty() || entry.name.front() == '.') {
        return UsbKind::Other;  // macOS' "._" files and the like
    }
    static const char* const videos[] = {"mkv", "mp4", "m4v", "mov", "avi", "ts", "m2ts", "mts", "webm",
                                         "wmv", "mpg", "mpeg", "flv", "3gp", "ogv", "vob"};
    static const char* const audio[] = {"mp3", "flac", "m4a", "aac", "ogg", "oga", "opus", "wav", "wma", "alac", "aiff"};
    static const char* const subtitles[] = {"srt", "ass", "ssa", "vtt", "sub"};
    const std::string extension = extensionOf(entry.name);
    for (const char* known : videos) {
        if (extension == known) {
            return UsbKind::Video;
        }
    }
    for (const char* known : audio) {
        if (extension == known) {
            return UsbKind::Audio;
        }
    }
    for (const char* known : subtitles) {
        if (extension == known) {
            return UsbKind::Subtitle;
        }
    }
    return UsbKind::Other;
}

std::string usbTitle(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::vector<std::string> usbSubtitlesFor(const std::string& video, const std::vector<net::FtpEntry>& entries) {
    const std::string stem = lower(usbTitle(video));
    std::vector<std::string> out;
    for (const net::FtpEntry& entry : entries) {
        if (usbKind(entry) != UsbKind::Subtitle) {
            continue;
        }
        const std::string name = lower(usbTitle(entry.name));  // "film" or "film.fr"
        if (name == stem || (name.size() > stem.size() && name.compare(0, stem.size(), stem) == 0 &&
                             name[stem.size()] == '.')) {
            out.push_back(entry.name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

player::StreamOpener usbOpener(std::shared_ptr<const net::FtpClient> ftp) {
    if (!ftp) {
        return {};
    }
    return [ftp](const std::string& path) -> std::unique_ptr<player::StreamReader> {
        if (!isUsbPath(path)) {
            return nullptr;
        }
        return std::make_unique<Reader>(ftp, path);
    };
}

}  // namespace playfin::app
