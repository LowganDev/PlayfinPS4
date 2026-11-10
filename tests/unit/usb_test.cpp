#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "app/shell/usb.hpp"
#include "support/fake_ftp.hpp"

using namespace playfin;

TEST_CASE("USB paths: only /mnt/usb0 to /mnt/usb7, and writes only in a drive's Playfin folder") {
    CHECK(app::usbDriveOf("/mnt/usb0") == "/mnt/usb0");
    CHECK(app::usbDriveOf("/mnt/usb3/Films/a.mkv") == "/mnt/usb3");
    CHECK(app::usbDriveOf("/mnt/usb8/x").empty());
    CHECK(app::usbDriveOf("/mnt/usb01/x").empty());
    CHECK(app::usbDriveOf("/data/Playfin").empty());
    CHECK(app::isUsbPath("/mnt/usb0/Films/a b.mkv"));
    CHECK_FALSE(app::isUsbPath("/mnt/usb0/../../system"));
    CHECK(app::usbWritablePath("/mnt/usb0", "m-1.mkv") == "/mnt/usb0/Playfin/m-1.mkv");
    CHECK(app::usbWritablePath("/mnt/usb0", "../x").empty());
    CHECK(app::usbWritablePath("/mnt/usb0", "..").empty());
    CHECK(app::usbWritablePath("/mnt/usb0/Films", "x.mkv").empty());  // a drive, nothing deeper
    CHECK(app::usbWritablePath("/data", "x.mkv").empty());
    CHECK(app::usbUrl("/mnt/usb0/a.mkv") == "pfusb:///mnt/usb0/a.mkv");
}

TEST_CASE("USB files: what is a video, music or a subtitle; the subtitles beside a video") {
    const auto file = [](const char* name) { return net::FtpEntry{name, false, 1}; };
    CHECK(app::usbKind(file("Film (2026).MKV")) == app::UsbKind::Video);
    CHECK(app::usbKind(file("01 Song.flac")) == app::UsbKind::Audio);
    CHECK(app::usbKind(file("Film.fr.srt")) == app::UsbKind::Subtitle);
    CHECK(app::usbKind(file("._Film.mkv")) == app::UsbKind::Other);  // macOS' metadata files
    CHECK(app::usbKind(file("notes.txt")) == app::UsbKind::Other);
    CHECK(app::usbKind(net::FtpEntry{"Films", true, 0}) == app::UsbKind::Folder);
    CHECK(app::usbKind(net::FtpEntry{"System Volume Information", true, 0}) == app::UsbKind::Other);
    CHECK(app::usbTitle("Film (2026).mkv") == "Film (2026)");
    const std::vector<net::FtpEntry> entries = {file("Film.mkv"), file("Film.fr.srt"), file("Film.srt"),
                                                file("Film 2.srt"), file("Other.srt"), file("film.EN.ass")};
    CHECK(app::usbSubtitlesFor("Film.mkv", entries) == std::vector<std::string>{"Film.fr.srt", "Film.srt", "film.EN.ass"});
}

TEST_CASE("USB drives are found through the console's FTP server") {
    char tmpl[] = "/tmp/playfin-usb-XXXXXX";
    REQUIRE(mkdtemp(tmpl) != nullptr);
    const std::string root = tmpl;
    std::filesystem::create_directories(root + "/mnt/usb1/Films");
    std::filesystem::create_directories(root + "/mnt/usb0");
    std::filesystem::create_directories(root + "/mnt/disc");
    {
        test::FakeFtp server(root);
        const net::FtpClient ftp(server.base());
        const auto drives = app::usbDrives(ftp);
        REQUIRE(drives.ok());
        CHECK(drives.value() == std::vector<std::string>{"/mnt/usb0", "/mnt/usb1"});
    }
    const net::FtpClient nobody("ftp://127.0.0.1:1");  // no FTP server: an error, not a crash
    CHECK_FALSE(app::usbDrives(nobody).ok());
    std::filesystem::remove_all(root);
}
