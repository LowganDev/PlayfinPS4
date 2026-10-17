#include <algorithm>
#include <chrono>
#include <thread>

#include "app/shell/cards.hpp"
#include "app/shell/chrome.hpp"
#include "core/log.hpp"
#include "app/shell/format.hpp"
#include "app/shell/screen.hpp"
#include "app/shell/screens.hpp"
#include "jellyfin/discovery.hpp"
#include "net/crypto.hpp"
#include "ui/i18n.hpp"
#include "ui/widgets.hpp"

namespace playfin::app {

namespace {

ui::Label* addStatus(ui::Box& column) {
    auto* status = column.add<ui::Label>("", ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
    status->width = ui::SizeSpec::fill();
    status->maxLines = 0;
    status->color = ui::theme().textSecondary;
    return status;
}

void showError(ui::Label* status, const std::string& text) {
    status->setText(text);
    status->color = ui::theme().error;
}

void openServer(AppSession& session, const jellyfin::Connection& connection, const std::string& pin) {
    session.openServer(connection, pin);
    if (ui::Application* app = ui::Application::current()) {
        app->push(makeUsers(session));
    }
}

// Tries `address`; an untrusted certificate asks the user, then retries with its pin.
void connectTo(ShellScreen& screen, std::string address, std::string pin, ui::Label* status) {
    if (screen.busy) {
        return;
    }
    screen.busy = true;
    status->setText(ui::tr("shell.add.connecting"));
    status->color = ui::theme().textSecondary;
    const net::HttpClient* http = &screen.session().http();
    const core::CancelToken token = screen.cancelToken();
    screen.run<core::Result<jellyfin::Connection>>(
        [http, address, pin, token] { return jellyfin::connect(*http, address, pin, token); },
        [&screen, address, pin, status](core::Result<jellyfin::Connection> result) {
            screen.busy = false;
            if (result.ok()) {
                status->setText("");
                openServer(screen.session(), result.value(), pin);
                return;
            }
            const core::Error& error = result.error();
            if (error.kind != core::ErrorKind::CertificateUntrusted) {
                showError(status, errorText(error));
                return;
            }
            status->setText("");
            const core::CertificateInfo certificate = error.certificate;
            const std::string message =
                ui::tr("shell.cert.message", {address, net::hexFingerprint(certificate.spkiSha256), certificate.issuer,
                                              certificate.notAfter});
            ui::Application::current()->showModal(ui::makeDialog(
                ui::tr("shell.cert.title"), message,
                {{ui::tr("shell.cert.trust"),
                  [&screen, address, certificate, status] {
                      connectTo(screen, address, net::curlPin(certificate.spkiSha256), status);
                  }},
                 {ui::tr("ui.cancel"), {}}}));
        });
}

class StartScreen : public ShellScreen {
public:
    explicit StartScreen(AppSession& session) : ShellScreen(session) {
        horizontal = ui::CrossAlign::Center;
        vertical = ui::CrossAlign::Center;
        add<ui::Spinner>();
    }
    void onShow() override {
        later([this] { start(); });
    }

private:
    void start() {
        ui::Application* app = ui::Application::current();
        if (startTarget(session_) != StartTarget::Home) {
            app->replace(makeServers(session_));
            return;
        }
        const auto client = session_.client();
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::UserInfo>>([client, token] { return client->currentUser(token); },
                                              [this](core::Result<jellyfin::UserInfo> user) {
                                                  ui::Application* current = ui::Application::current();
                                                  if (user.ok()) {
                                                      current->replace(makeHome(session_));
                                                  } else if (user.error().kind == core::ErrorKind::Unauthorized) {
                                                      session_.forgetToken();
                                                      showUsers(session_);
                                                  } else {
                                                      const std::string text = errorText(user.error());
                                                      current->replace(makeServers(session_));
                                                      current->toast(text);
                                                  }
                                              });
    }
};

class ServersScreen : public ShellScreen {
public:
    explicit ServersScreen(AppSession& session) : ShellScreen(session) {
        column_ = addPage(*this, ui::tr("shell.servers.title"));
        auto* saved = column_->add<ui::Box>(ui::Axis::Column);
        saved->gap = ui::theme().spacingM;
        saved->add<ui::Label>(ui::tr("shell.servers.saved"), ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
        savedRow_ = saved->add<ui::Box>(ui::Axis::Row);
        savedRow_->gap = ui::theme().spacingM;
        savedSection_ = saved;
        auto* found = column_->add<ui::Box>(ui::Axis::Column);
        found->gap = ui::theme().spacingM;
        found->add<ui::Label>(ui::tr("shell.servers.found"), ui::FontStyle{ui::theme().fontTitle, ui::FontWeight::SemiBold});
        foundRow_ = found->add<ui::Box>(ui::Axis::Row);
        foundRow_->gap = ui::theme().spacingM;
        foundStatus_ = addStatus(*found);
        foundSection_ = found;
        column_->add<ui::Button>(ui::tr("shell.servers.add"), [this] {
            ui::Application::current()->push(makeAddServer(session_));
        });
        status_ = addStatus(*column_);
        bind(platform::Action::Options, ui::tr("shell.servers.refresh"), [this] {
            refresh();
            return true;
        });
        fillSaved();
        foundSection_->visible = session_.discovery;
    }
    void onShow() override {
        if (session_.discovery && !searched_) {
            searched_ = true;
            later([this] { refresh(); });
        }
    }

private:
    void fillSaved() {
        savedRow_->clearChildren();
        for (const auto& server : session_.data().servers) {
            const std::string url = !server.lastUrl.empty() ? server.lastUrl
                                                           : (server.urls.empty() ? std::string() : server.urls.front());
            if (url.empty()) {
                continue;
            }
            const std::string pin = server.pin;
            savedRow_->add<ui::Button>(server.name.empty() ? url : server.name,
                                       [this, url, pin] { connectTo(*this, url, pin, status_); });
        }
        savedSection_->visible = !savedRow_->children().empty();
    }

    void refresh() {
        if (!session_.discovery || searching_) {
            return;
        }
        searching_ = true;
        foundRow_->clearChildren();
        foundStatus_->setText(ui::tr("shell.servers.searching"));
        const core::CancelToken token = token_;
        run<std::vector<jellyfin::DiscoveredServer>>(
            [token] { return jellyfin::discover(jellyfin::DiscoveryConfig{}, token); },
            [this](std::vector<jellyfin::DiscoveredServer> servers) {
                searching_ = false;
                PF_LOG_INFO("servers screen: %d discovered", static_cast<int>(servers.size()));
                foundStatus_->setText(servers.empty() ? ui::tr("shell.servers.none") : std::string());
                for (const auto& server : servers) {
                    std::string pin;
                    if (const jellyfin::StoredServer* known = jellyfin::findServer(session_.data(), server.id)) {
                        pin = known->pin;
                    }
                    const std::string address = server.address;
                    foundRow_->add<ui::Button>(server.name, [this, address, pin] {
                        connectTo(*this, address, pin, status_);
                    });
                }
                focusIfEmpty();
            });
    }

    ui::Box* column_;
    ui::Box* savedSection_;
    ui::Box* savedRow_;
    ui::Box* foundSection_;
    ui::Box* foundRow_;
    ui::Label* foundStatus_;
    ui::Label* status_;
    bool searched_ = false;
    bool searching_ = false;
};

class AddServerScreen : public ShellScreen {
public:
    explicit AddServerScreen(AppSession& session) : ShellScreen(session) {
        auto* column = addPage(*this, ui::tr("shell.add.title"));
        platform::TextInputRequest request;
        request.title = ui::tr("shell.add.address");
        request.kind = platform::TextInputRequest::Kind::Url;
        request.maxLength = 512;
        auto* field = column->add<ui::TextField>(ui::tr("shell.add.address"), request, [](const std::string&) {});
        field->width = ui::SizeSpec::fixed(900);
        auto* hint = column->add<ui::Label>(ui::tr("shell.add.hint"), ui::FontStyle{ui::theme().fontSmall, ui::FontWeight::Regular});
        hint->color = ui::theme().textSecondary;
        column->add<ui::Button>(ui::tr("shell.add.connect"), [this, field] {
            connectTo(*this, field->value(), "", status_);
        });
        status_ = addStatus(*column);
    }

private:
    ui::Label* status_ = nullptr;
};

struct UsersData {
    core::Result<std::vector<jellyfin::PublicUser>> users = std::vector<jellyfin::PublicUser>{};
    bool quickConnect = false;
};

class UsersScreen : public ShellScreen {
public:
    explicit UsersScreen(AppSession& session) : ShellScreen(session) {
        column_ = addPage(*this, ui::tr("shell.users.title"));
        if (const jellyfin::StoredServer* server = session_.server()) {
            auto* name = column_->add<ui::Label>(server->name, ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
            name->color = ui::theme().textSecondary;
        }
        row_ = column_->add<ui::Box>(ui::Axis::Row);
        row_->gap = ui::theme().spacingL;
        row_->padding = {ui::theme().spacingL, 0, 0, 0};  // room for the grown focused profile
        status_ = addStatus(*column_);
    }
    void onShow() override {
        if (!loaded_) {
            loaded_ = true;
            later([this] { load(); });
        }
    }

private:
    void load() {
        const auto client = session_.client();
        if (!client) {
            ui::Application::current()->replace(makeServers(session_));
            return;
        }
        status_->setText(ui::tr("shell.loading"));
        const core::CancelToken token = token_;
        run<UsersData>(
            [client, token] {
                UsersData data;
                data.users = client->publicUsers(token);
                const auto quick = client->quickConnectEnabled(token);
                data.quickConnect = quick.ok() && quick.value();
                return data;
            },
            [this](UsersData data) { show(std::move(data)); });
    }

    void show(UsersData data) {
        status_->setText("");
        quickConnect_ = data.quickConnect;
        const auto client = session_.client();
        std::vector<jellyfin::PublicUser> users;
        if (data.users.ok()) {
            users = data.users.value();
        } else {
            showError(status_, errorText(data.users.error()));
        }
        // users signed in on this console before, which the server may hide from its list
        if (const jellyfin::StoredServer* server = session_.addressKnown() ? session_.server() : nullptr) {
            for (const auto& stored : server->users) {
                const bool listed = std::any_of(users.begin(), users.end(),
                                                [&](const jellyfin::PublicUser& user) { return user.id == stored.id; });
                if (!listed && !stored.token.empty()) {
                    jellyfin::PublicUser user;
                    user.id = stored.id;
                    user.name = stored.name;
                    users.push_back(user);
                }
            }
        }
        for (const auto& user : users) {
            const std::string picture = client && !user.primaryImageTag.empty()
                                            ? client->userImageUrl(user.id, user.primaryImageTag, 180)
                                            : std::string();
            row_->add<ProfileCard>(user.name, picture, initialOf(user.name), [this, user] { choose(user); });
        }
        row_->add<ProfileCard>(ui::tr("shell.users.other"), "", "+", [this] {
            ui::Application::current()->push(makeSignIn(session_, "", true, true, quickConnect_));
        });
        if (quickConnect_) {
            row_->add<ProfileCard>(ui::tr("shell.users.quick_connect"), "", "#", [this] {
                ui::Application::current()->push(makeQuickConnect(session_));
            });
        }
        focusIfEmpty();
    }

    void choose(const jellyfin::PublicUser& user) {
        if (busy) {
            return;
        }
        const jellyfin::StoredServer* server = session_.addressKnown() ? session_.server() : nullptr;
        if (server != nullptr) {
            for (const auto& stored : server->users) {
                if (stored.id == user.id && !stored.token.empty()) {
                    const jellyfin::StoredUser copy = stored;
                    session_.useStoredUser(copy);
                    const auto client = session_.client();
                    const bool hasPassword = user.hasPassword;
                    const std::string name = user.name;
                    const core::CancelToken token = token_;
                    status_->setText(ui::tr("shell.loading"));
                    status_->color = ui::theme().textSecondary;
                    busy = true;
                    run<core::Result<jellyfin::UserInfo>>(
                        [client, token] { return client->currentUser(token); },
                        [this, name, hasPassword](core::Result<jellyfin::UserInfo> result) {
                            busy = false;
                            if (result.ok()) {
                                ui::Application::current()->replace(makeHome(session_));
                            } else if (result.error().kind != core::ErrorKind::Unauthorized) {
                                showError(status_, errorText(result.error()));  // the token may still be good
                            } else {
                                status_->setText("");
                                session_.forgetToken();
                                ui::Application::current()->push(
                                    makeSignIn(session_, name, false, hasPassword, quickConnect_));
                            }
                        });
                    return;
                }
            }
        }
        ui::Application::current()->push(makeSignIn(session_, user.name, false, user.hasPassword, quickConnect_));
    }

    ui::Box* column_;
    ui::Box* row_;
    ui::Label* status_;
    bool loaded_ = false;
    bool quickConnect_ = false;
};

class SignInScreen : public ShellScreen {
public:
    SignInScreen(AppSession& session, std::string name, bool askName, bool hasPassword, bool quickConnect)
        : ShellScreen(session), name_(std::move(name)) {
        auto* column = addPage(*this, ui::tr("shell.signin.title"));
        if (askName) {
            platform::TextInputRequest request;
            request.title = ui::tr("shell.signin.username");
            nameField_ = column->add<ui::TextField>(ui::tr("shell.signin.username"), request, [](const std::string&) {});
        } else {
            column->add<ui::Label>(name_, ui::FontStyle{ui::theme().fontHeading, ui::FontWeight::Regular});
        }
        if (hasPassword || askName) {
            platform::TextInputRequest request;
            request.title = ui::tr("shell.signin.password");
            request.kind = platform::TextInputRequest::Kind::Password;
            passwordField_ =
                column->add<ui::TextField>(ui::tr("shell.signin.password"), request, [](const std::string&) {});
        }
        column->add<ui::Button>(ui::tr("shell.signin.submit"), [this] { submit(); });
        if (quickConnect) {
            column->add<ui::Button>(ui::tr("shell.signin.quick_connect"), [this] {
                ui::Application::current()->push(makeQuickConnect(session_));
            });
        }
        status_ = addStatus(*column);
    }

private:
    void submit() {
        const std::string user = nameField_ != nullptr ? nameField_->value() : name_;
        const std::string password = passwordField_ != nullptr ? passwordField_->value() : std::string();
        const auto client = session_.client();
        status_->setText(ui::tr("shell.loading"));
        status_->color = ui::theme().textSecondary;
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::AuthResult>>(
            [client, user, password, token] { return client->signIn(user, password, token); },
            [this](core::Result<jellyfin::AuthResult> auth) {
                if (!auth.ok()) {
                    showError(status_, errorText(auth.error()));
                    return;
                }
                session_.signedIn(auth.value());
                ui::Application::current()->replace(makeHome(session_));
            });
    }

    std::string name_;
    ui::TextField* nameField_ = nullptr;
    ui::TextField* passwordField_ = nullptr;
    ui::Label* status_ = nullptr;
};

class QuickConnectScreen : public ShellScreen {
public:
    explicit QuickConnectScreen(AppSession& session) : ShellScreen(session) {
        auto* column = addPage(*this, ui::tr("shell.qc.title"));
        auto* message = column->add<ui::Label>(ui::tr("shell.qc.message"), ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        message->width = ui::SizeSpec::fixed(1100);
        message->maxLines = 0;
        code_ = column->add<ui::Label>("", ui::FontStyle{96, ui::FontWeight::SemiBold});
        auto* waiting = column->add<ui::Box>(ui::Axis::Row);
        waiting->gap = ui::theme().spacingM;
        waiting->crossAlign = ui::CrossAlign::Center;
        waiting->add<ui::Spinner>();
        waiting->add<ui::Label>(ui::tr("shell.qc.waiting"), ui::FontStyle{ui::theme().fontBody, ui::FontWeight::Regular});
        status_ = addStatus(*column);
        focusable = true;  // nothing else to focus: Back still works
    }
    void onShow() override {
        if (!started_) {
            started_ = true;
            later([this] { start(); });
        }
    }

private:
    void start() {
        const auto client = session_.client();
        const core::CancelToken token = token_;
        run<core::Result<jellyfin::QuickConnectState>>(
            [client, token] { return client->quickConnectStart(token); },
            [this, client](core::Result<jellyfin::QuickConnectState> state) {
                if (!state.ok()) {
                    showError(status_, errorText(state.error()));
                    return;
                }
                code_->setText(state.value().code);
                const std::string secret = state.value().secret;
                const core::CancelToken token = token_;
                run<core::Result<jellyfin::AuthResult>>(
                    [client, secret, token]() -> core::Result<jellyfin::AuthResult> {
                        for (int round = 0; round < 120 && !token.cancelled(); ++round) {
                            for (int i = 0; i < 50 && !token.cancelled(); ++i) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                            const auto poll = client->quickConnectPoll(secret, token);
                            if (!poll.ok()) {
                                return poll.error();
                            }
                            if (poll.value().authenticated) {
                                return client->quickConnectFinish(secret, token);
                            }
                        }
                        return core::makeError(core::ErrorKind::Timeout, "");
                    },
                    [this](core::Result<jellyfin::AuthResult> auth) {
                        if (!auth.ok()) {
                            showError(status_, errorText(auth.error()));
                            return;
                        }
                        session_.signedIn(auth.value());
                        ui::Application::current()->replace(makeHome(session_));
                    });
            });
    }

    ui::Label* code_;
    ui::Label* status_;
    bool started_ = false;
};

}  // namespace

void showUsers(AppSession& session) {
    ui::Application* app = ui::Application::current();
    if (app == nullptr) {
        return;
    }
    app->replace(makeServers(session));  // Back leads to the servers, never to nothing
    app->push(makeUsers(session));
}

std::unique_ptr<ui::Screen> makeStart(AppSession& session) { return std::make_unique<StartScreen>(session); }
std::unique_ptr<ui::Screen> makeServers(AppSession& session) { return std::make_unique<ServersScreen>(session); }
std::unique_ptr<ui::Screen> makeAddServer(AppSession& session) { return std::make_unique<AddServerScreen>(session); }
std::unique_ptr<ui::Screen> makeUsers(AppSession& session) { return std::make_unique<UsersScreen>(session); }
std::unique_ptr<ui::Screen> makeSignIn(AppSession& session, std::string name, bool askName, bool hasPassword,
                                       bool quickConnect) {
    return std::make_unique<SignInScreen>(session, std::move(name), askName, hasPassword, quickConnect);
}
std::unique_ptr<ui::Screen> makeQuickConnect(AppSession& session) {
    return std::make_unique<QuickConnectScreen>(session);
}

void signOut(AppSession& session) {
    ui::Application* app = ui::Application::current();
    if (const auto client = session.client()) {
        app->pool().submit([client] { (void)client->signOut(); });
    }
    session.forgetToken();
    showUsers(session);
}

}  // namespace playfin::app
