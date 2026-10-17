#include <string>
#include <vector>

#include "app/settings.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/licenses.hpp"
#include "app/shell/nav_bar.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "core/file_io.hpp"
#include "core/version.hpp"
#include "ui/i18n.hpp"
#include "ui/scroll.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

constexpr std::size_t kMaxLicensesBytes = 256 * 1024;

std::string accentName(const std::string& id) { return ui::tr("settings.accent." + id); }

// "Auto (40 Mbit/s)", "20 Mbit/s", "1,5 Mbit/s"…
std::string bitrateName(std::int64_t bitrate) {
    const std::int64_t whole = bitrate / 1000000;
    const std::int64_t tenths = (bitrate % 1000000) / 100000;
    std::string number = std::to_string(whole);
    if (tenths != 0) {
        number += ui::tr("settings.decimal_separator") + std::to_string(tenths);
    }
    const std::string mbps = ui::tr("settings.mbps", {number});
    return bitrate == bitrateChoices().front() ? ui::tr("settings.quality.auto", {mbps}) : mbps;
}

std::string languageName(const std::string& tag) {
    for (const auto& language : uiLanguages()) {
        if (language.first == tag) {
            return language.second;
        }
    }
    return ui::tr("settings.language.system");
}

ui::Label* addHeading(ui::Box& column, const std::string& text) {
    auto* heading = column.add<ui::Label>(text, ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::SemiBold});
    heading->margin = {ui::theme().spacingL, 0, 0, 0};
    return heading;
}

class SettingsScreen : public ShellScreen {
public:
    explicit SettingsScreen(AppSession& session) : ShellScreen(session) {
        auto* scroll = add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingM;
        ui::Box* column = content.get();
        column->add<NavBar>(session_, Tab::Settings);

        addHeading(*column, ui::tr("settings.display"));
        accent_ = column->add<ui::ValueRow>(ui::tr("settings.accent"), accentName(session_.settings().accent),
                                            [this] { chooseAccent(); });
        language_ = column->add<ui::ValueRow>(ui::tr("settings.language"), "", [this] { chooseLanguage(); });
        refreshLanguage();
        remembered = accent_;  // the first setting, not the bar above it

        addHeading(*column, ui::tr("settings.playback"));
        quality_ = column->add<ui::ValueRow>(ui::tr("settings.quality"), bitrateName(session_.settings().maxBitrate),
                                             [this] { chooseQuality(); });

        addHeading(*column, ui::tr("settings.account"));
        std::string serverName;
        std::string userName;
        if (const jellyfin::StoredServer* server = session_.server()) {
            serverName = server->name;
            for (const auto& user : server->users) {
                if (user.id == session_.data().lastUserId) {
                    userName = user.name;
                }
            }
        }
        AppSession* shared = &session_;
        column->add<ui::ValueRow>(ui::tr("shell.menu.switch_user"), userName, [shared] { showUsers(*shared); });
        column->add<ui::ValueRow>(ui::tr("shell.menu.switch_server"), serverName, [shared] {
            ui::Application::current()->replace(makeServers(*shared));
        });
        column->add<ui::ValueRow>(ui::tr("shell.menu.sign_out"), "", [shared] { signOut(*shared); });

        addHeading(*column, ui::tr("settings.about"));
        column->add<ui::ValueRow>(ui::tr("settings.version"), core::version(), nullptr);
        server_ = column->add<ui::ValueRow>(ui::tr("settings.server"), serverName, nullptr);
        column->add<ui::ValueRow>(ui::tr("settings.licenses"), "", [shared] {
            ui::Application::current()->push(makeLicenses(*shared));
        });
        scroll->setContent(std::move(content));
    }
    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { loadServer(); });
        }
    }

private:
    void loadServer() {
        const auto client = session_.client();
        if (!client) {
            return;
        }
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::ServerInfo>>([client, token] { return client->publicInfo(token); },
                                                [this](core::Result<jellyfin::ServerInfo> info) {
                                                    if (info.ok()) {
                                                        server_->setValue(info.value().name + " \xC2\xB7 " +
                                                                          info.value().version);
                                                    }
                                                });
    }

    void chooseAccent() {
        std::vector<std::string> names;
        int selected = 0;
        const auto& palette = ui::accentPalette();
        for (std::size_t i = 0; i < palette.size(); ++i) {
            names.push_back(accentName(palette[i].id));
            if (session_.settings().accent == palette[i].id) {
                selected = static_cast<int>(i);
            }
        }
        ui::Application::current()->showModal(ui::makeChoiceDialog(ui::tr("settings.accent"), names, selected, [this](int index) {
            const ui::AccentChoice& choice = ui::accentPalette()[static_cast<std::size_t>(index)];
            session_.settings().accent = choice.id;
            session_.saveSettings();
            ui::applyAccent(choice);  // the halo and every view follow on the next frame
            accent_->setValue(accentName(choice.id));
        }));
    }

    void chooseQuality() {
        std::vector<std::string> names;
        int selected = 0;
        const auto& choices = bitrateChoices();
        for (std::size_t i = 0; i < choices.size(); ++i) {
            names.push_back(bitrateName(choices[i]));
            if (session_.settings().maxBitrate == choices[i]) {
                selected = static_cast<int>(i);
            }
        }
        ui::Application::current()->showModal(ui::makeChoiceDialog(ui::tr("settings.quality"), names, selected, [this](int index) {
            session_.settings().maxBitrate = bitrateChoices()[static_cast<std::size_t>(index)];
            session_.saveSettings();
            quality_->setValue(bitrateName(session_.settings().maxBitrate));
        }));
    }

    void chooseLanguage() {
        std::vector<std::string> names{ui::tr("settings.language.system")};
        std::vector<std::string> tags{""};
        int selected = 0;
        for (const auto& language : uiLanguages()) {
            if (session_.settings().language == language.first) {
                selected = static_cast<int>(tags.size());
            }
            names.push_back(language.second);
            tags.push_back(language.first);
        }
        ui::Application::current()->showModal(ui::makeChoiceDialog(ui::tr("settings.language"), names, selected,
                                                                   [this, tags](int index) {
            session_.settings().language = tags[static_cast<std::size_t>(index)];
            session_.saveSettings();
            refreshLanguage();
        }));
    }

    // a language other than the one the app started with applies at the next start, and says so
    void refreshLanguage() {
        const std::string& language = session_.settings().language;
        const std::string name = languageName(language);
        language_->setValue(language == session_.languageAtStart ? name
                                                                 : ui::tr("settings.language.next_start", {name}));
    }

    ui::ValueRow* accent_;
    ui::ValueRow* language_;
    ui::ValueRow* quality_;
    ui::ValueRow* server_;
    bool loaded_ = false;
};

// A paragraph of the licences: focusable so that the list scrolls with the pad.
class Paragraph : public ui::Label {
public:
    explicit Paragraph(std::string text) : ui::Label(std::move(text), ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::Regular}) {
        focusable = true;
        width = ui::SizeSpec::fill();
        maxLines = 0;
        color = ui::theme().textSecondary;
    }
};

// One licence: the texts of a component, one focusable paragraph per block (the pad scrolls through them).
class LicenseTextScreen : public ShellScreen {
public:
    // `paths`: the entry's files, full paths
    LicenseTextScreen(AppSession& session, const LicenseEntry& entry, const std::vector<std::string>& paths)
        : ShellScreen(session) {
        auto* scroll = add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingL;
        content->add<ui::Label>(entry.name, ui::FontStyle{ui::theme().fontLogo, ui::FontWeight::Bold});
        auto* meta = content->add<ui::Label>(entry.version + " \xC2\xB7 " + entry.license,
                                             ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        meta->color = ui::theme().textSecondary;
        meta->visible = !entry.version.empty();
        for (const auto& path : paths) {
            std::string text;
            if (core::readSmallFile(path, kMaxLicensesBytes, text) != core::ReadStatus::Ok) {
                text = ui::tr("settings.licenses.missing");
            }
            std::size_t start = 0;
            while (start < text.size()) {
                std::size_t end = text.find("\n\n", start);
                if (end == std::string::npos) {
                    end = text.size();
                }
                const std::string paragraph = text.substr(start, end - start);
                if (paragraph.find_first_not_of(" \n") != std::string::npos) {
                    content->add<Paragraph>(paragraph);
                }
                start = end + 2;
            }
        }
        scroll->setContent(std::move(content));
    }
};

// The components and their licences; ✕ shows a licence's texts.
class LicensesScreen : public ShellScreen {
public:
    explicit LicensesScreen(AppSession& session) : ShellScreen(session) {
        auto* scroll = add<ui::VScroll>();
        scroll->policy = ui::VScroll::Policy::Nearest;
        auto content = std::make_unique<ui::Box>(ui::Axis::Column);
        content->padding = pagePadding();
        content->gap = ui::theme().spacingM;
        content->add<ui::Label>(ui::tr("settings.licenses"), ui::FontStyle{ui::theme().fontLogo, ui::FontWeight::Bold});
        auto* intro = content->add<ui::Label>(ui::tr("settings.licenses.intro"),
                                              ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        intro->color = ui::theme().textSecondary;
        intro->width = ui::SizeSpec::fixed(1100);
        intro->maxLines = 0;
        AppSession* shared = &session_;
        LicenseEntry playfin{"Playfin", core::version(), "GPL-3.0-or-later", {"GPL-3.0.txt"}, {}};
        std::vector<LicenseEntry> entries{playfin};
        std::string manifest;
        if (core::readSmallFile(session_.licensesDir + "/manifest.txt", kMaxLicensesBytes, manifest) ==
            core::ReadStatus::Ok) {
            for (auto& entry : parseLicenseManifest(manifest)) {
                entries.push_back(std::move(entry));
            }
        }
        for (const auto& entry : entries) {
            content->add<ui::ValueRow>(entry.name, entry.license, [shared, entry] {
                std::vector<std::string> paths;
                for (const auto& file : entry.files) {
                    paths.push_back(shared->licensesDir + "/" + file);
                }
                ui::Application::current()->push(std::make_unique<LicenseTextScreen>(*shared, entry, paths));
            });
            if (&entry == &entries.front()) {
                // after Playfin: the copyright lines and credits the licences ask to reproduce (FreeType), the
                // patches' authors, where the sources are
                LicenseEntry notices{ui::tr("settings.licenses.notices"), "", "", {}, {}};
                content->add<ui::ValueRow>(notices.name, "", [shared, notices] {
                    ui::Application::current()->push(std::make_unique<LicenseTextScreen>(
                        *shared, notices, std::vector<std::string>{shared->noticesFile}));
                });
            }
        }
        scroll->setContent(std::move(content));
    }
};

}  // namespace

std::unique_ptr<ui::Screen> makeSettings(AppSession& session) { return std::make_unique<SettingsScreen>(session); }
std::unique_ptr<ui::Screen> makeLicenses(AppSession& session) { return std::make_unique<LicensesScreen>(session); }

}  // namespace playfin::app
