#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "net/ftp.hpp"
#include "support/fake_ftp.hpp"

using namespace playfin;

namespace {

struct Drive {
    std::string root;
    Drive() {
        char tmpl[] = "/tmp/playfin-ftp-XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        root = tmpl;
        std::filesystem::create_directories(root + "/mnt/usb0/Films");
    }
    ~Drive() { std::filesystem::remove_all(root); }
    void write(const std::string& path, const std::string& text) const {
        std::ofstream(root + path, std::ios::binary) << text;
    }
    std::string read(const std::string& path) const {
        std::ifstream in(root + path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }
};

std::string bytes(std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += static_cast<char>('a' + (i * 7 + i / 251) % 26);
    }
    return out;
}

}  // namespace

TEST_CASE("an FTP listing in the Unix style: folders, files and sizes; dots and odd names left out") {
    const auto entries = net::parseFtpList(
        "drwxrwxrwx 1 -1 -1 131072 Nov 10 2026 .\r\n"
        "drwxrwxrwx 1 -1 -1 131072 Jan  1 2026 ..\r\n"
        "drwxrwxrwx 1 -1 -1 131072 Nov 10 2026 Films\r\n"
        "-rwxrwxrwx 1 -1 -1 36937725 Nov 10 2026 Test HEVC 5.1 (2026).mkv\r\n"
        "lrwxrwxrwx 1 0 0 4 Nov 10 2026 link -> x\r\n"
        "total 12\r\n");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "Films");
    CHECK(entries[0].directory);
    CHECK(entries[1].name == "Test HEVC 5.1 (2026).mkv");  // spaces kept
    CHECK_FALSE(entries[1].directory);
    CHECK(entries[1].size == 36937725);
}

TEST_CASE("FTP paths: absolute, no dots, no empty parts") {
    CHECK(net::validFtpPath("/mnt/usb0/Films/A film.mkv"));
    CHECK(net::validFtpPath("/mnt/usb0/"));
    CHECK_FALSE(net::validFtpPath("mnt/usb0"));
    CHECK_FALSE(net::validFtpPath("/mnt/usb0/../../data"));
    CHECK_FALSE(net::validFtpPath("/mnt//usb0"));
    CHECK_FALSE(net::validFtpPath("/mnt/usb0/./x"));
    CHECK_FALSE(net::validFtpPath(std::string("/mnt/usb0/a\nDELE /x")));
}

TEST_CASE("FTP: a folder listed, a file's size and content, written, appended, renamed and deleted") {
    Drive drive;
    drive.write("/mnt/usb0/Films/a b.mp4", "0123456789");
    test::FakeFtp server(drive.root);
    const net::FtpClient ftp(server.base());
    const auto list = ftp.list("/mnt/usb0/Films");
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 1);
    CHECK(list.value()[0].name == "a b.mp4");
    CHECK(list.value()[0].size == 10);
    const auto size = ftp.size("/mnt/usb0/Films/a b.mp4");
    REQUIRE(size.ok());
    CHECK(size.value() == 10);
    const auto text = ftp.fetch("/mnt/usb0/Films/a b.mp4", 100);
    REQUIRE(text.ok());
    CHECK(text.value() == "0123456789");
    CHECK_FALSE(ftp.fetch("/mnt/usb0/Films/a b.mp4", 4).ok());  // over the limit

    REQUIRE(ftp.makeDir("/mnt/usb0/Playfin").ok());
    REQUIRE(ftp.makeDir("/mnt/usb0/Playfin").ok());  // already there: fine
    std::string pieces[] = {"hello ", "world"};
    int next = 0;
    const auto source = [&](char* buffer, std::size_t room) -> std::size_t {
        if (next >= 2) {
            return 0;
        }
        const std::string& piece = pieces[next++];
        const std::size_t count = std::min(room, piece.size());
        std::memcpy(buffer, piece.data(), count);
        return count;
    };
    REQUIRE(ftp.upload("/mnt/usb0/Playfin/x.part", source, false).ok());
    next = 1;
    REQUIRE(ftp.upload("/mnt/usb0/Playfin/x.part", source, true).ok());
    CHECK(drive.read("/mnt/usb0/Playfin/x.part") == "hello worldworld");
    REQUIRE(ftp.rename("/mnt/usb0/Playfin/x.part", "/mnt/usb0/Playfin/x.mkv").ok());
    CHECK(drive.read("/mnt/usb0/Playfin/x.mkv") == "hello worldworld");
    REQUIRE(ftp.remove("/mnt/usb0/Playfin/x.mkv").ok());
    CHECK_FALSE(std::filesystem::exists(drive.root + "/mnt/usb0/Playfin/x.mkv"));
    CHECK_FALSE(ftp.remove("/mnt/usb0/Playfin/gone").ok());
    CHECK_FALSE(ftp.list("/mnt/../etc").ok());  // refused before anything is sent
}

TEST_CASE("an FTP reader reads a file in order and from wherever it is moved to") {
    Drive drive;
    const std::string content = bytes(9 * 1024 * 1024 + 123);  // past the reader's read-ahead
    drive.write("/mnt/usb0/Films/big.mkv", content);
    test::FakeFtp server(drive.root);
    const net::FtpClient ftp(server.base());
    net::FtpReader reader(ftp, "/mnt/usb0/Films/big.mkv");
    CHECK(reader.size() == static_cast<std::int64_t>(content.size()));
    std::string got;
    char buffer[65536];
    while (true) {
        const long long n = reader.read(buffer, sizeof(buffer));
        REQUIRE(n >= 0);
        if (n == 0) {
            break;
        }
        got.append(buffer, static_cast<std::size_t>(n));
    }
    CHECK(got == content);
    REQUIRE(reader.seek(5 * 1024 * 1024));
    const long long n = reader.read(buffer, 100);
    REQUIRE(n > 0);
    CHECK(std::string(buffer, static_cast<std::size_t>(n)) == content.substr(5 * 1024 * 1024, static_cast<std::size_t>(n)));
    REQUIRE(reader.seek(static_cast<std::int64_t>(content.size()) - 3));
    std::string tail;
    while (true) {
        const long long m = reader.read(buffer, sizeof(buffer));
        if (m <= 0) {
            break;
        }
        tail.append(buffer, static_cast<std::size_t>(m));
    }
    CHECK(tail == content.substr(content.size() - 3));
    reader.cancel();
    CHECK(reader.read(buffer, 10) == -1);
    net::FtpReader missing(ftp, "/mnt/usb0/Films/none.mkv");
    CHECK(missing.size() == -1);
    CHECK(missing.read(buffer, 10) == -1);
}
