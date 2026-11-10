#include "support/fake_ftp.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace playfin::test {

namespace {

bool sendAll(int fd, const std::string& text) {
    std::size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t n = ::send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool readLine(int fd, std::string& line) {
    line.clear();
    char c = 0;
    while (true) {
        const ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) {
            return false;
        }
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        line += c;
        if (line.size() > 4096) {
            return false;
        }
    }
}

int listenOn(int& port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    const int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(fd, 8) != 0) {
        ::close(fd);
        return -1;
    }
    socklen_t length = sizeof(address);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
    port = ntohs(address.sin_port);
    return fd;
}

std::string listing(const std::string& dir) {
    std::string out = "drwxrwxrwx 1 -1 -1 131072 Nov 10 2026 .\r\ndrwxrwxrwx 1 -1 -1 131072 Jan  1 2026 ..\r\n";
    DIR* handle = ::opendir(dir.c_str());
    if (handle == nullptr) {
        return {};
    }
    while (const dirent* entry = ::readdir(handle)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        struct stat info {};
        if (::stat((dir + "/" + name).c_str(), &info) != 0) {
            continue;
        }
        char line[512];
        std::snprintf(line, sizeof(line), "%s 1 -1 -1 %lld Nov 10 2026 %s\r\n",
                      S_ISDIR(info.st_mode) ? "drwxrwxrwx" : "-rwxrwxrwx", static_cast<long long>(info.st_size),
                      name.c_str());
        out += line;
    }
    ::closedir(handle);
    return out;
}

}  // namespace

FakeFtp::FakeFtp(std::string root) : root_(std::move(root)) {
    listener_ = listenOn(port_);
    acceptor_ = std::thread([this] { acceptLoop(); });
}

FakeFtp::~FakeFtp() {
    stopping_ = true;
    ::shutdown(listener_, SHUT_RDWR);
    ::close(listener_);
    acceptor_.join();
    std::vector<std::thread> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int fd : sockets_) {
            ::shutdown(fd, SHUT_RDWR);
        }
        sessions.swap(sessions_);
    }
    for (std::thread& session : sessions) {
        session.join();
    }
}

std::string FakeFtp::base() const { return "ftp://127.0.0.1:" + std::to_string(port_); }

void FakeFtp::acceptLoop() {
    while (!stopping_) {
        const int fd = ::accept(listener_, nullptr, nullptr);
        if (fd < 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.push_back(fd);
        sessions_.emplace_back([this, fd] { serve(fd); });
    }
}

void FakeFtp::serve(int control) {
    sendAll(control, "220 Fake FTP\r\n");
    int passive = -1;
    long long rest = 0;
    std::string renameFrom;
    std::string line;
    const auto local = [this](const std::string& path) { return root_ + path; };
    const auto dataConnection = [&]() -> int {
        if (passive < 0) {
            return -1;
        }
        const int fd = ::accept(passive, nullptr, nullptr);
        ::close(passive);
        passive = -1;
        return fd;
    };
    while (readLine(control, line)) {
        const std::size_t space = line.find(' ');
        std::string verb = line.substr(0, space);
        for (char& c : verb) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        const std::string arg = space == std::string::npos ? std::string() : line.substr(space + 1);
        if (verb == "USER" || verb == "PASS") {
            sendAll(control, "230 User logged in, proceed.\r\n");
        } else if (verb == "PWD") {
            sendAll(control, "257 \"/\" is the current directory.\r\n");
        } else if (verb == "TYPE" || verb == "OPTS" || verb == "NOOP") {
            sendAll(control, "200 Command okay.\r\n");
        } else if (verb == "SYST") {
            sendAll(control, "215 UNIX Type: L8\r\n");
        } else if (verb == "PASV") {
            int port = 0;
            passive = listenOn(port);
            sendAll(control, "227 Entering Passive Mode (127,0,0,1," + std::to_string(port / 256) + "," +
                                 std::to_string(port % 256) + ").\r\n");
        } else if (verb == "REST") {
            rest = std::atoll(arg.c_str());
            sendAll(control, "350 Requested file action pending further information.\r\n");
        } else if (verb == "SIZE") {
            struct stat info {};
            if (::stat(local(arg).c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
                sendAll(control, "213 " + std::to_string(info.st_size) + "\r\n");
            } else {
                sendAll(control, "550 File unavailable.\r\n");
            }
        } else if (verb == "LIST") {
            std::string dir = arg.empty() ? "/" : arg;
            const std::string text = listing(local(dir));
            sendAll(control, "150 File status okay; about to open data connection.\r\n");
            const int data = dataConnection();
            if (data >= 0) {
                sendAll(data, text);
                ::close(data);
            }
            sendAll(control, "226 Closing data connection.\r\n");
        } else if (verb == "RETR") {
            std::ifstream in(local(arg), std::ios::binary);
            if (!in) {
                sendAll(control, "550 File unavailable.\r\n");
                continue;
            }
            in.seekg(rest);
            rest = 0;
            sendAll(control, "150 File status okay; about to open data connection.\r\n");
            const int data = dataConnection();
            char buffer[65536];
            bool ok = data >= 0;
            while (ok && in) {
                in.read(buffer, sizeof(buffer));
                const std::streamsize got = in.gcount();
                if (got > 0) {
                    ok = sendAll(data, std::string(buffer, static_cast<std::size_t>(got)));
                }
            }
            if (data >= 0) {
                ::close(data);
            }
            sendAll(control, ok ? "226 Transfer complete.\r\n" : "426 Connection closed.\r\n");
        } else if (verb == "STOR" || verb == "APPE") {
            std::ofstream out(local(arg), verb == "APPE" ? std::ios::binary | std::ios::app : std::ios::binary);
            if (!out) {
                sendAll(control, "550 File unavailable.\r\n");
                continue;
            }
            sendAll(control, "150 File status okay; about to open data connection.\r\n");
            const int data = dataConnection();
            char buffer[65536];
            while (data >= 0) {
                const ssize_t n = ::recv(data, buffer, sizeof(buffer), 0);
                if (n <= 0) {
                    break;
                }
                out.write(buffer, n);
            }
            if (data >= 0) {
                ::close(data);
            }
            out.close();
            sendAll(control, "226 Transfer complete.\r\n");
        } else if (verb == "MKD") {
            sendAll(control, ::mkdir(local(arg).c_str(), 0755) == 0 ? "257 Created.\r\n" : "550 Exists.\r\n");
        } else if (verb == "DELE") {
            sendAll(control, ::unlink(local(arg).c_str()) == 0 ? "250 Deleted.\r\n" : "550 File unavailable.\r\n");
        } else if (verb == "RNFR") {
            renameFrom = arg;
            sendAll(control, "350 Ready for RNTO.\r\n");
        } else if (verb == "RNTO") {
            const bool ok = ::rename(local(renameFrom).c_str(), local(arg).c_str()) == 0;
            sendAll(control, ok ? "250 Renamed.\r\n" : "550 File unavailable.\r\n");
        } else if (verb == "QUIT") {
            sendAll(control, "221 Bye.\r\n");
            break;
        } else {
            sendAll(control, "502 Command not implemented.\r\n");
        }
    }
    if (passive >= 0) {
        ::close(passive);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sockets_.begin(); it != sockets_.end(); ++it) {
        if (*it == control) {
            sockets_.erase(it);
            break;
        }
    }
    ::close(control);
}

}  // namespace playfin::test
