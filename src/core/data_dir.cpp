#include "core/data_dir.hpp"

#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <utility>

namespace playfin::core {

namespace {

std::string gDataRoot;

bool isDirectory(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

}  // namespace

std::optional<std::string> resolveInside(const std::string& root, std::string_view relative) {
    if (root.empty() || relative.empty() || relative.front() == '/') {
        return std::nullopt;
    }
    if (relative.find('\\') != std::string_view::npos || relative.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t start = 0;
    while (start <= relative.size()) {
        std::size_t slash = relative.find('/', start);
        if (slash == std::string_view::npos) {
            slash = relative.size();
        }
        const std::string_view segment = relative.substr(start, slash - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return std::nullopt;
        }
        start = slash + 1;
    }
    std::string out = root;
    if (out.back() != '/') {
        out += '/';
    }
    out.append(relative);
    return out;
}

void setDataRoot(std::string root) { gDataRoot = std::move(root); }

const std::string& dataRoot() { return gDataRoot; }

std::optional<std::string> dataPath(std::string_view relative) { return resolveInside(gDataRoot, relative); }

bool makeDirs(const std::string& path) {
#ifdef PLAYFIN_PS4
    // a folder created under /data may not end up owned by the app's user: 0755 then forbids creating files in it
    // (seen on the first console runs). Existing folders, from earlier runs, are opened up too.
    const bool ok = makeDirsWith(path, ::mkdir, 0777);
    if (ok) {
        chmod(path.c_str(), 0777);
    }
    return ok;
#else
    return makeDirsWith(path, ::mkdir);
#endif
}

bool makeDirsWith(const std::string& path, int (*makeDir)(const char*, mode_t), mode_t mode) {
    if (path.empty()) {
        return false;
    }
    for (std::size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string prefix = path.substr(0, i);
            if (isDirectory(prefix)) {
                continue;  // never mkdir what exists: the PS4 sandbox refuses it for "/" and "/data" (not EEXIST)
            }
            if (makeDir(prefix.c_str(), mode) != 0) {
                if (errno != EEXIST) {
                    return false;
                }
            } else {
                chmod(prefix.c_str(), mode);  // the umask would narrow it
            }
        }
    }
    return isDirectory(path);
}

}  // namespace playfin::core
