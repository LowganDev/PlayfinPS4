# Changelog

## 2.0.0 (unreleased)

- Next episode: a card in the last 10 s with the episode's picture and title; ✕ plays it now, ◯ cancels, it plays
  on by itself when the countdown ends. The last episode of a season goes on to the next season (specials are
  skipped); the details page's previous / next episode cross seasons too.
- Player: the volume (kept), the picture whole, filling the screen, stretched or smaller (for TVs that cut the
  edges), when the video ends in the bar. ← → and L1 / R1 seek 10 s; held, they go faster (30 s after 2 s, 1 min
  after 4 s, 2 min after 6 s). L2 / R2 held 3 s play the previous or next episode (a panel fills meanwhile; a
  short press does nothing).
- Details: the version's quality (4K, HDR10, Dolby Vision, codec, 5.1…), when it ends, the director, the cast and
  crew; a person's films and series; the version to play when there are several.
- Libraries: ← in the first column jumps to a letter; △ gives a quick look (synopsis, quality, cast); pictures
  shimmer while they load.
- Downloads: films, episodes and whole seasons kept on the console and played without the server.
- Software update: Settings › Mise à jour finds a newer Playfin, downloads its package to /data/pkg and checks it;
  a dot on the settings tab when one is out.
- Home: the date and time in the header, the order of the libraries chosen in the settings, the touchpad
  refreshes it; the downloads in a row.
- Search: an on-screen keyboard that searches while you type (0.4 s after the last key; □ deletes a letter), the
  system keyboard still on the field for accents; people, music and channels too; L2 / R2 narrow the results to
  one kind. A search typed key by key is kept in the recent ones once a result is opened.
- Music: genres and songs (L2 / R2 between the tabs), a genre's albums, the volume.
- Live TV: favourite channels (△), channels filtered by L2 / R2 (favourites, recently watched, films, sport,
  news, kids, radios), a channel search.
- Each PS4 account starts on its own Jellyfin user; a sign-in can be shared with the accounts that have none.
- A screensaver after five idle minutes: the server's films and series full screen, with the time.
- The controller's buttons and what they do now, at the bottom left of every screen: navigate, select, the
  screen's own actions (A–Z, quick look, filters, tabs, refresh), back; in the player with its bar (pause, 10 s,
  1 min, 5 min, options, stop; volume and channel on Live TV).

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
