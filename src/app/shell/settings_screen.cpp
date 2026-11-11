#include <nanovg.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/settings.hpp"
#include "app/shell/chrome.hpp"
#include "app/shell/format.hpp"
#include "app/shell/licenses.hpp"
#include "app/shell/nav_bar.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "app/shell/tv_guide.hpp"
#include "core/data_dir.hpp"
#include "core/file_io.hpp"
#include "core/log.hpp"
#include "core/strings.hpp"
#include "core/version.hpp"
#include "ui/draw.hpp"
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

// the play methods of the settings ("auto", "direct", "transcode"), in their order in the list
const std::vector<std::string>& playMethods() {
    static const std::vector<std::string> methods{"auto", "direct", "transcode"};
    return methods;
}

std::string playMethodName(const std::string& method) { return ui::tr("settings.play_method." + method); }

std::string languageName(const std::string& tag) {
    for (const auto& language : uiLanguages()) {
        if (language.first == tag) {
            return language.second;
        }
    }
    return ui::tr("settings.language.system");
}

// The banner at the top of the settings while a newer Playfin is published: accent fill, the version, what to do,
// and the download's state on the right; ✕ does what the update row does.
class UpdateBanner : public ui::Box {
public:
    explicit UpdateBanner(std::function<void()> onPress) : ui::Box(ui::Axis::Row) {
        name = "update banner";
        focusable = true;
        drawsOwnFocus = true;
        width = ui::SizeSpec::fixed(1100);
        padding = {ui::theme().spacingL, ui::theme().spacingL, ui::theme().spacingL, ui::theme().spacingL};
        cornerRadius = ui::theme().radius;
        crossAlign = ui::CrossAlign::Center;
        gap = ui::theme().spacingL;
        auto* texts = add<ui::Box>(ui::Axis::Column);
        texts->width = ui::SizeSpec::fill();
        texts->gap = ui::theme().spacingS / 2;
        title_ = texts->add<ui::Label>("", ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
        title_->width = ui::SizeSpec::fill();
        text_ = texts->add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        text_->width = ui::SizeSpec::fill();
        text_->maxLines = 2;
        state_ = add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::SemiBold});
        refresh();
        bind(platform::Action::Confirm, ui::tr("ui.select"), [onPress = std::move(onPress)] {
            onPress();
            return true;
        });
    }
    void set(const std::string& title, const std::string& text, const std::string& state) {
        title_->setText(title);
        text_->setText(text);
        state_->setText(state);
    }
    void onFocusChanged(bool focused) override {
        focused_ = focused;
        refresh();
    }
    void onThemeChanged() override {
        refresh();
        ui::Box::onThemeChanged();
    }
    // focused: a white line inside its edge (the fill stays the accent, as the focused rows' is)
    void draw(NVGcontext* vg) const override {
        ui::Box::draw(vg);
        if (!focused_) {
            return;
        }
        const float inset = ui::theme().focusRingWidth / 2 + 2;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, inset, inset, frame.width - 2 * inset, frame.height - 2 * inset, cornerRadius - inset);
        nvgStrokeColor(vg, ui::nvgColor(ui::theme().onAccent));
        nvgStrokeWidth(vg, ui::theme().focusRingWidth);
        nvgStroke(vg);
    }

private:
    void refresh() {
        background = ui::theme().accent;
        title_->color = ui::theme().onAccent;
        text_->color = ui::withAlpha(ui::theme().onAccent, 0xD8);
        state_->color = ui::theme().onAccent;
        title_->markDirty();
        text_->markDirty();
        state_->markDirty();
        markDirty();
    }
    bool focused_ = false;
    ui::Label* title_;
    ui::Label* text_;
    ui::Label* state_;
};

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
        banner_ = column->add<UpdateBanner>([this] { pressUpdate(); });
        banner_->margin = {ui::theme().spacingS, 0, 0, 0};

        addHeading(*column, ui::tr("settings.display"));
        theme_ = column->add<ui::ValueRow>(ui::tr("settings.theme"), ui::themeById(session_.settings().theme).name,
                                           [this] { chooseTheme(); });
        accent_ = column->add<ui::ValueRow>(ui::tr("settings.accent"), accentName(session_.settings().accent),
                                            [this] { chooseAccent(); });
        language_ = column->add<ui::ValueRow>(ui::tr("settings.language"), "", [this] { chooseLanguage(); });
        refreshLanguage();
        column->add<ui::ValueRow>(ui::tr("settings.library_order"), "", [this] {
            ui::Application::current()->push(makeLibraryOrder(session_));
        });
        remembered = theme_;  // the first setting, not the bar above it

        addHeading(*column, ui::tr("settings.playback"));
        quality_ = column->add<ui::ValueRow>(ui::tr("settings.quality"), bitrateName(session_.settings().maxBitrate),
                                             [this] { chooseQuality(); });
        playMethod_ = column->add<ui::ValueRow>(ui::tr("settings.play_method"),
                                                playMethodName(session_.settings().playMethod),
                                                [this] { choosePlayMethod(); });

        // an XMLTV address for servers without a guide (Live TV)
        guide_ = column->add<ui::ValueRow>(ui::tr("settings.tv_guide"), "", [this] { pressGuide(); });
        session_.tvGuide().listen(this, [this] { refreshGuide(); });
        refreshGuide();

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
        if (!session_.consoleUser.empty()) {  // the PS4's other accounts may start signed in as this user
            share_ = column->add<ui::ValueRow>(ui::tr("settings.share"), "", [this] {
                session_.shareSignIn(!session_.signInShared());
                share_->setValue(ui::tr(session_.signInShared() ? "settings.on" : "settings.off"));
            });
            share_->setValue(ui::tr(session_.signInShared() ? "settings.on" : "settings.off"));
        }

        addHeading(*column, ui::tr("settings.about"));
        column->add<ui::ValueRow>(ui::tr("settings.version"), core::version(), nullptr);
        update_ = column->add<ui::ValueRow>(ui::tr("settings.update"), "", [this] { pressUpdate(); });
        refreshUpdate();
        if (session_.update) {
            remembered = banner_;  // the screen opens on the banner
        }
        server_ = column->add<ui::ValueRow>(ui::tr("settings.server"), serverName, nullptr);
        column->add<ui::ValueRow>(ui::tr("settings.licenses"), "", [shared] {
            ui::Application::current()->push(makeLicenses(*shared));
        });
        scroll->setContent(std::move(content));
    }
    ~SettingsScreen() override { session_.tvGuide().unlisten(this); }

    void onShow() override {
        refreshUpdate();  // the check at start may have answered since
        if (!loaded_) {
            loaded_ = true;
            later([this] { loadServer(); });
        }
    }

private:
    // ---- the TV guide read on the console

    void refreshGuide() {
        const TvGuide& guide = session_.tvGuide();
        switch (guide.state()) {
        case TvGuide::State::Off:
            guide_->setValue(ui::tr("settings.tv_guide.off"));
            break;
        case TvGuide::State::Loading:
            guide_->setValue(ui::tr("settings.tv_guide.loading"));
            break;
        case TvGuide::State::Ready:
            guide_->setValue(ui::translator().plural("settings.tv_guide.ready", static_cast<long long>(guide.channelCount())));
            break;
        case TvGuide::State::Failed:
            guide_->setValue(ui::tr("settings.tv_guide.failed"));
            break;
        }
    }

    // the address on the keyboard; empty: the server's guide only
    void pressGuide() {
        platform::TextInputRequest request;
        request.title = ui::tr("settings.tv_guide");
        request.initial = session_.settings().xmltvUrl;
        request.kind = platform::TextInputRequest::Kind::Url;
        request.maxLength = 2048;
        const core::CancelToken token = token_;
        ui::Application::current()->requestText(request, [this, token](std::optional<std::string> text) {
            if (!text || token.cancelled()) {
                return;
            }
            const std::string url(core::trimmed(*text));
            if (!url.empty() && !validGuideUrl(url)) {
                ui::Application::current()->toast(ui::tr("settings.tv_guide.invalid"));
                return;
            }
            session_.settings().xmltvUrl = url;
            session_.saveSettings();
            session_.tvGuide().refresh();
        });
    }

    // ---- software update

    void refreshUpdate() {
        if (downloading_) {
            return;  // the progress shows there
        }
        banner_->visible = session_.update.has_value();
        if (session_.update) {
            banner_->set(ui::tr("settings.update.banner", {session_.update->version}),
                         ui::tr(ready_ ? "settings.update.banner_ready" : "settings.update.banner_text"),
                         ui::tr(ready_ ? "settings.update.ready" : "settings.update.download"));
        }
        banner_->markDirty();
        if (ready_) {
            update_->setValue(ui::tr("settings.update.ready"));
        } else if (session_.update) {
            update_->setValue(ui::tr("settings.update.available", {session_.update->version}));
        } else if (session_.updateChecked) {
            update_->setValue(ui::tr("settings.update.latest"));
        } else {
            update_->setValue(ui::tr("settings.update.check"));
        }
    }

    void pressUpdate() {
        if (downloading_) {
            return;
        }
        if (!session_.update) {
            session_.updateChecked = false;  // asked again
            update_->setValue(ui::tr("settings.update.checking"));
            session_.checkForUpdate([this, token = token_] {
                if (token.cancelled()) {
                    return;
                }
                refreshUpdate();
                if (!session_.updateChecked) {  // asked by the user, so the failure is told
                    ui::Application::current()->toast(ui::tr("settings.update.unreachable"));
                }
            });
            return;
        }
        const Release release = *session_.update;
        if (ready_) {
            showInstall(release);
            return;
        }
        const std::string megabytes = std::to_string((release.size + 999999) / 1000000);
        ui::Application::current()->showModal(ui::makeDialog(
            ui::tr("settings.update.title", {release.version}), ui::tr("settings.update.explain", {megabytes}),
            {{ui::tr("settings.update.download"), [this, release] { later([this, release] { download(release); }); }},
             {ui::tr("ui.cancel"), {}}}));
    }

    // the package to /data/pkg, its progress on the row; then how to install it
    void download(const Release& release) {
        const auto path = core::packagePath(packageFileName(release.version));
        if (!path) {
            ui::Application::current()->toast(ui::tr("settings.update.failed"));
            return;
        }
        downloading_ = true;
        showProgress(0);
        const net::HttpClient* http = &session_.http();
        const std::string target = *path;
        const core::CancelToken token = token_;
        ui::Application* app = ui::Application::current();
        auto percent = std::make_shared<std::atomic<int>>(0);
        runTransfer<core::Result<core::Unit>>(
            [http, release, target, token, app, percent, this] {
                return downloadPackage(
                    *http, release, target,
                    [app, percent, token, this](long long received, long long total) {
                        const int now = total > 0 ? static_cast<int>(received * 100 / total) : 0;
                        if (now != percent->exchange(now) && app != nullptr) {
                            app->mainQueue().post([this, token, now] {
                                if (!token.cancelled() && downloading_) {
                                    showProgress(now);
                                }
                            });
                        }
                    },
                    token);
            },
            [this, release](core::Result<core::Unit> done) {
                downloading_ = false;
                if (!done.ok()) {
                    PF_LOG_WARN("update: %s", done.error().message.c_str());
                    refreshUpdate();
                    ui::Application::current()->toast(ui::tr("settings.update.failed"));
                    return;
                }
                ready_ = true;
                refreshUpdate();
                showInstall(release);
            });
    }

    void showInstall(const Release& release) {
        ui::Application::current()->showModal(
            ui::makeDialog(ui::tr("settings.update.title", {release.version}),
                           ui::tr("settings.update.install", {packageFileName(release.version)}),
                           {{ui::tr("ui.ok"), {}}}));
    }

    void showProgress(int percent) {
        const std::string progress = ui::tr("settings.update.progress", {std::to_string(percent)});
        update_->setValue(progress);
        if (session_.update) {
            banner_->set(ui::tr("settings.update.banner", {session_.update->version}),
                         ui::tr("settings.update.banner_text"), progress);
        }
    }

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

    // a theme changes every colour, and views take theirs when built: the screens are built again with it
    void chooseTheme() {
        std::vector<std::string> names;
        int selected = 0;
        const auto& themes = ui::themeChoices();
        for (std::size_t i = 0; i < themes.size(); ++i) {
            names.push_back(themes[i].name);
            if (session_.settings().theme == themes[i].id) {
                selected = static_cast<int>(i);
            }
        }
        ui::Application::current()->showModal(ui::makeChoiceDialog(ui::tr("settings.theme"), names, selected, [this](int index) {
            const std::string chosen = ui::themeChoices()[static_cast<std::size_t>(index)].id;
            if (chosen == session_.settings().theme) {
                return;  // the theme in use: its accent setting stays as it is
            }
            session_.settings().theme = chosen;
            session_.settings().accent = "theme";  // the theme's own, until another is chosen
            session_.saveSettings();
            applyAppearance(session_.settings());
            AppSession* session = &session_;
            later([session] { ui::Application::current()->replace(makeSettings(*session)); });
        }));
    }

    // "Couleur du thème" (the theme's own), then the palette
    void chooseAccent() {
        std::vector<std::string> names{accentName("theme")};
        int selected = 0;
        const auto& palette = ui::accentPalette();
        for (std::size_t i = 0; i < palette.size(); ++i) {
            names.push_back(accentName(palette[i].id));
            if (session_.settings().accent == palette[i].id) {
                selected = static_cast<int>(i) + 1;
            }
        }
        ui::Application::current()->showModal(ui::makeChoiceDialog(ui::tr("settings.accent"), names, selected, [this](int index) {
            session_.settings().accent =
                index == 0 ? std::string("theme") : std::string(ui::accentPalette()[static_cast<std::size_t>(index - 1)].id);
            session_.saveSettings();
            applyAppearance(session_.settings());  // the halo and every view follow on the next frame
            accent_->setValue(accentName(session_.settings().accent));
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

    void choosePlayMethod() {
        std::vector<std::string> names;
        int selected = 0;
        for (std::size_t i = 0; i < playMethods().size(); ++i) {
            names.push_back(playMethodName(playMethods()[i]));
            if (session_.settings().playMethod == playMethods()[i]) {
                selected = static_cast<int>(i);
            }
        }
        ui::Application::current()->showModal(
            ui::makeChoiceDialog(ui::tr("settings.play_method"), names, selected, [this](int index) {
                session_.settings().playMethod = playMethods()[static_cast<std::size_t>(index)];
                session_.saveSettings();
                playMethod_->setValue(playMethodName(session_.settings().playMethod));
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

    ui::ValueRow* theme_;
    ui::ValueRow* accent_;
    ui::ValueRow* language_;
    ui::ValueRow* quality_;
    ui::ValueRow* playMethod_;
    ui::ValueRow* update_ = nullptr;
    ui::ValueRow* guide_ = nullptr;
    ui::ValueRow* share_ = nullptr;
    bool downloading_ = false;
    bool ready_ = false;  // the package is in /data/pkg
    UpdateBanner* banner_ = nullptr;
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
        // the Jellyfin themes Playfin's take their look from, with their authors
        addHeading(*content, ui::tr("settings.licenses.themes"));
        auto* themesIntro = content->add<ui::Label>(ui::tr("settings.licenses.themes_intro"),
                                                    ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        themesIntro->color = ui::theme().textSecondary;
        themesIntro->width = ui::SizeSpec::fixed(1100);
        themesIntro->maxLines = 0;
        for (const ui::ThemeChoice& theme : ui::themeChoices()) {
            if (theme.author != nullptr) {
                content->add<ui::ValueRow>(std::string(theme.name) + " \xC2\xB7 " + theme.author,
                                           std::string(theme.source) + " \xC2\xB7 " + theme.license, nullptr);
            }
        }
        scroll->setContent(std::move(content));
    }
};

}  // namespace

std::unique_ptr<ui::Screen> makeSettings(AppSession& session) { return std::make_unique<SettingsScreen>(session); }
std::unique_ptr<ui::Screen> makeLicenses(AppSession& session) { return std::make_unique<LicensesScreen>(session); }

}  // namespace playfin::app
