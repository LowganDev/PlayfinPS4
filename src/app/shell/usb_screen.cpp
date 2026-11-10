#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/music_player.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "app/shell/usb.hpp"
#include "core/log.hpp"
#include "ui/application.hpp"
#include "ui/i18n.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

// "1,2 Go", "340 Mo" (decimal units, as the console shows its storage)
std::string fileSize(std::int64_t bytes) {
    char text[32];
    if (bytes >= 1000LL * 1000 * 1000) {
        std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(bytes) / 1e9);
        std::string number = text;
        if (const auto dot = number.find('.'); dot != std::string::npos) {
            number.replace(dot, 1, ui::tr("settings.decimal_separator"));
        }
        return ui::tr("downloads.gb", {number});
    }
    return ui::tr("downloads.mb", {std::to_string((bytes + 999999) / (1000 * 1000))});
}

// A folder of a USB drive: its folders first, then its videos and music, by name; ✕ opens a folder, plays a video,
// or plays the folder's music from that track.
class UsbScreen : public ShellScreen {
public:
    UsbScreen(AppSession& session, std::string path, std::string title)
        : ShellScreen(session), path_(std::move(path)) {
        column_ = addPage(*this, title);
        status_ = column_->add<ui::Label>(ui::tr("usb.loading"), bodyText());
        status_->color = ui::theme().textSecondary;
        auto* scroll = column_->add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        scroll->width = ui::SizeSpec::fill();
        scroll->height = ui::SizeSpec::fill();
        auto list = std::make_unique<ui::Box>(ui::Axis::Column);
        list->width = ui::SizeSpec::fill();
        list->gap = ui::theme().spacingS;
        list->padding = {ui::theme().spacingS, 0, 120, 0};
        list_ = list.get();
        scroll->setContent(std::move(list));
        load();
    }

private:
    void load() {
        const auto ftp = session_.usbFtp;
        const std::string path = path_;
        const core::CancelToken token = cancelToken();
        run<core::Result<std::vector<net::FtpEntry>>>(
            [ftp, path, token]() -> core::Result<std::vector<net::FtpEntry>> {
                if (!ftp || !isUsbPath(path)) {
                    return core::makeError(core::ErrorKind::NotFound, "no USB drive");
                }
                return ftp->list(path, token);
            },
            [this](core::Result<std::vector<net::FtpEntry>> entries) { show(std::move(entries)); });
    }

    void show(core::Result<std::vector<net::FtpEntry>> listed) {
        if (!listed.ok()) {
            PF_LOG_WARN("usb: %s: %s", path_.c_str(), listed.error().message.c_str());
            status_->setText(ui::tr("usb.unreachable"));
            status_->color = ui::theme().error;
            markDirty();
            return;
        }
        std::vector<net::FtpEntry> folders;
        for (const net::FtpEntry& entry : listed.value()) {
            const UsbKind kind = usbKind(entry);
            if (kind == UsbKind::Folder) {
                folders.push_back(entry);
            } else if (kind == UsbKind::Video || kind == UsbKind::Audio) {
                files_.push_back(entry);
            }
        }
        const auto byName = [](const net::FtpEntry& a, const net::FtpEntry& b) { return a.name < b.name; };
        std::sort(folders.begin(), folders.end(), byName);
        std::sort(files_.begin(), files_.end(), byName);
        ui::View* first = nullptr;
        for (const net::FtpEntry& folder : folders) {
            const std::string path = path_ + "/" + folder.name;
            const std::string name = folder.name;
            auto* row = list_->add<ui::ValueRow>(name, ui::tr("usb.folder"), [this, path, name] {
                ui::Application::current()->push(makeUsbBrowser(session_, path, name));
            });
            row->width = ui::SizeSpec::fill();
            first = first != nullptr ? first : row;
        }
        for (std::size_t i = 0; i < files_.size(); ++i) {
            auto* row = list_->add<ui::ValueRow>(files_[i].name, fileSize(files_[i].size), [this, i] { play(i); });
            row->width = ui::SizeSpec::fill();
            first = first != nullptr ? first : row;
        }
        status_->visible = first == nullptr;
        status_->setText(ui::tr("usb.empty"));
        if (first != nullptr) {
            focusIfEmpty(first);
        }
        markDirty();
    }

    jellyfin::Item itemFor(const net::FtpEntry& entry, jellyfin::ItemType type) const {
        jellyfin::Item item;
        const std::string path = path_ + "/" + entry.name;
        item.id = "usb:" + path;
        item.name = usbTitle(entry.name);
        item.type = type;
        item.localUrl = usbUrl(path);
        const std::size_t slash = path_.rfind('/');
        item.album = slash == std::string::npos ? path_ : path_.substr(slash + 1);
        return item;
    }

    void play(std::size_t index) {
        const net::FtpEntry& entry = files_[index];
        if (usbKind(entry) == UsbKind::Video) {
            ui::Application::current()->push(makePlayer(session_, itemFor(entry, jellyfin::ItemType::Video), 0));
            return;
        }
        // the folder's music, from this track on (and the ones before after it, as an album plays)
        std::vector<jellyfin::Item> tracks;
        int start = 0;
        for (std::size_t i = 0; i < files_.size(); ++i) {
            if (usbKind(files_[i]) != UsbKind::Audio) {
                continue;
            }
            if (i == index) {
                start = static_cast<int>(tracks.size());
            }
            tracks.push_back(itemFor(files_[i], jellyfin::ItemType::Audio));
        }
        session_.music().play(std::move(tracks), start, false);
        ui::Application::current()->push(makeNowPlaying(session_));
    }

    std::string path_;
    ui::Box* column_;
    ui::Label* status_;
    ui::Box* list_;
    std::vector<net::FtpEntry> files_;
};

}  // namespace

std::unique_ptr<ui::Screen> makeUsbBrowser(AppSession& session, std::string path, std::string title) {
    return std::make_unique<UsbScreen>(session, std::move(path), std::move(title));
}

}  // namespace playfin::app
