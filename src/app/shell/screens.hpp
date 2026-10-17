#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "app/shell/session.hpp"
#include "jellyfin/models.hpp"
#include "ui/application.hpp"

namespace playfin::app {

std::unique_ptr<ui::Screen> makeStart(AppSession& session);
std::unique_ptr<ui::Screen> makeServers(AppSession& session);
std::unique_ptr<ui::Screen> makeAddServer(AppSession& session);
std::unique_ptr<ui::Screen> makeUsers(AppSession& session);
// Users of the current server, with the servers screen behind it (replaces the whole stack).
void showUsers(AppSession& session);
std::unique_ptr<ui::Screen> makeSignIn(AppSession& session, std::string name, bool askName, bool hasPassword,
                                       bool quickConnect);
std::unique_ptr<ui::Screen> makeQuickConnect(AppSession& session);
std::unique_ptr<ui::Screen> makeHome(AppSession& session);
std::unique_ptr<ui::Screen> makeLibrary(AppSession& session, jellyfin::Item library);
// Details of an item, or the list of a library or folder (pushed).
void openItem(AppSession& session, const jellyfin::Item& item);
// A music library: albums, artists and playlists.
std::unique_ptr<ui::Screen> makeMusicLibrary(AppSession& session, jellyfin::Item library);
// An album, an artist or a playlist.
std::unique_ptr<ui::Screen> makeMusicItem(AppSession& session, jellyfin::Item item);
// The music playing: cover, controls, the queue.
std::unique_ptr<ui::Screen> makeNowPlaying(AppSession& session);
// Live TV: the channels on now, the guide, the recordings.
std::unique_ptr<ui::Screen> makeLiveTv(AppSession& session, jellyfin::Item view);
// Search field, recent searches, then movies, series and episodes found.
std::unique_ptr<ui::Screen> makeSearch(AppSession& session);
// Accent colour, playback quality, language, account, about.
std::unique_ptr<ui::Screen> makeSettings(AppSession& session);
// The third-party licences (session.licensesPath), scrolling.
std::unique_ptr<ui::Screen> makeLicenses(AppSession& session);
// Signs out on the server (in the background), forgets the token and shows the users of the server.
void signOut(AppSession& session);
std::unique_ptr<ui::Screen> makeDetails(AppSession& session, jellyfin::Item item);
// Plays an item full screen from `startTicks` (video under the UI); ○ goes back.
std::unique_ptr<ui::Screen> makePlayer(AppSession& session, jellyfin::Item item, std::int64_t startTicks);

}  // namespace playfin::app
