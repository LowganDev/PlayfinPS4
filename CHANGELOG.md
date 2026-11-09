# Changelog

## 2.1.0 (unreleased)

- Software update: Settings › Mise à jour finds a newer Playfin, downloads its package to /data/pkg and checks it;
  a dot on the settings tab when one is out.

## 2.0.0 (2026-11-09)

Playfin 2.0 brings Playfin close to the Jellyfin experience on a PS4: a home laid out as in Jellyfin, a
smoother interface, downloads, a much richer player, and Live TV that works with M3U lists and XMLTV guides.

#### Highlights
- **Home as in Jellyfin:** your libraries first (in the order you choose), then Resume, Next up, what is on TV,
  your downloads and the latest additions, on larger cards. The date and time in the header.
- **Smoother:** the interface runs at a steady 30 frames a second on the console (12 to 15 before on the home
  screen and the details pages).
- **Downloads:** films, episodes and whole seasons kept on the console and played without the server.
- **Next episode:** a card at the end of an episode plays the next one, across seasons. L2 / R2 held 3 s play
  the previous or next episode.
- **Button hints:** what each button does on the current screen, at the bottom left.

#### Player
- Seeking with ← → or L1 / R1: 10 s, faster while held.
- Volume, picture size and fit (whole, fill, stretch, smaller for TVs that cut the edges), the time it ends.
- □ hides the bar, even when paused.
- Play method in the settings: automatic, always direct play, or always the server's transcode.

#### Browsing
- Details: the version's quality (4K, HDR10, Dolby Vision, 5.1…), the cast and crew, each person's films and
  series, the version to play.
- Libraries: an A–Z jump (← in the first column) and a quick look at any title (△).
- Search: an on-screen keyboard that searches while you type; people, music and channels too; L2 / R2 narrow
  the results to one kind.

#### Music
- Genres and songs, a genre's albums, the volume.
- A visualizer behind Now Playing: bars, mirrored bars or a wave (□ changes it).

#### Live TV
- M3U channels play straight from their source, through the server when their source cannot be reached; a
  channel that stops is reopened by itself.
- An XMLTV guide read on the console (Settings › TV guide) for servers without one.
- Favourite channels, filters (favourites, recently watched, films, sport, news, kids, radios), a channel
  search.

#### Also
- Each PS4 account starts on its own Jellyfin user; a sign-in can be shared with the accounts that have none.
- A screensaver after five idle minutes, with your films and series and the time.

#### Fixes
- Live TV channels and transcoded videos did not play since 1.0.0 ("This video cannot be played").
- Host names are resolved at once on the console (they took 5 s behind some routers).
- A long title no longer pushes a duration out of its row.

#### Known limit
- On the console, a channel whose own stream is https plays through the server (Playfin cannot open https
  streams itself yet).

## 1.0.1 (2026-10-17)

- Home screen icon: the halo above "Playfin." in Jellyfin's purple-to-blue gradient.

## 1.0.0 (2026-10-17)

- Servers: several servers and several users per server, found on the network or added by address; sign-in with
  a password or Quick Connect; HTTPS with certificate pinning for self-signed servers.
- Home with Resume, Next up, what is on TV and the latest additions; libraries with sorting and filters,
  collections and playlists; details with seasons, favourites and watched marks; search with recent searches.
- Video: direct play or the server's transcode, audio and subtitle tracks (styled subtitles, every script
  including Japanese, Korean and Chinese), resume, intro and credits skipping, next episode.
- Music: albums, artists and playlists, a queue that keeps playing while browsing, Now playing, shuffle and
  repeat.
- Live TV: channels on now, a programme guide, recordings, watching a channel with channel change.
- Interface: made for the controller and the TV, 14 languages, an accent colour of your choice, a maximum
  playback quality, the third-party licences and copyright notices in the settings.
- Security: verified TLS, hardened player and picture decoding, logs without secrets.
