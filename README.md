<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo-on-dark.png">
    <img alt="Playfin" src="docs/images/logo-on-light.png" width="300">
  </picture>
</h1>

A Jellyfin client for the PlayStation 4, for consoles running homebrew (GoldHEN).

<p>
  <a href="https://github.com/LowganDev/PlayfinPS4/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/LowganDev/PlayfinPS4?label=release&color=0070d1"></a>
  <img alt="PS4 with GoldHEN" src="https://img.shields.io/badge/PS4-GoldHEN-003791">
  <img alt="Jellyfin 10.10 and later" src="https://img.shields.io/badge/Jellyfin-10.10%2B-aa5cc3">
  <img alt="14 languages" src="https://img.shields.io/badge/languages-14-00a4dc">
  <a href="LICENSE"><img alt="Licence GPL-3.0" src="https://img.shields.io/badge/licence-GPL--3.0-blue"></a>
</p>

<a href="https://github.com/LowganDev/PlayfinPS4/releases/latest/download/IV0001-PLFN00000_00-PLFN000000100000.pkg"><img alt="Download for PS4" src="docs/images/download.png" width="281"></a>

The latest version's package; earlier versions are on the [Releases](https://github.com/LowganDev/PlayfinPS4/releases)
page. Once installed, Playfin updates itself from its settings.

![Home](docs/images/screens/home.jpg)

| | |
|---|---|
| ![Details](docs/images/screens/details.jpg) | ![A library](docs/images/screens/library.jpg) |
| ![Quick look](docs/images/screens/peek.jpg) | ![Search](docs/images/screens/search.jpg) |

The films in the screenshots are the Blender Foundation's open films; their artwork is free to use
([credits](scripts/showcase/CREDITS.md)).

## What it does

- **Servers and users:** several servers and several users per server, found on your network or added by
  address; sign-in with a password or Quick Connect; self-signed HTTPS servers trusted on first use (their key is
  then pinned). Each PS4 account starts on its own Jellyfin user, and a sign-in can be shared with the others.
- **Home:** Resume, Next up, what is on TV, your libraries in the order you choose, the latest additions, your
  downloads; the date and time in the header.
- **Libraries:** sorted and filtered, an A–Z jump bar, a quick look at any title (△).
- **Details:** the version's quality (4K, HDR10, Dolby Vision, 5.1…), when it ends, the director and the cast
  (each person's films and series), seasons and episodes, favourites and watched marks, the version to play.
- **Video:** direct play of H.264, HEVC, VP9 and AV1 up to 1080p, or the server's transcode otherwise; audio and
  subtitle tracks (text and picture subtitles, every script including Japanese, Korean and Chinese); resume;
  intro and credits skipping; the next episode in a card at the end; the volume and the picture's size.
- **Downloads:** films, episodes and whole seasons kept on the console and played without the server.
- **Music:** albums, artists, playlists, genres and songs; a queue that keeps playing while you browse, shuffle
  and repeat.
- **Music visualizer:** the track's spectrum behind Now Playing, as bars, mirrored bars or a wave (□).
- **Live TV:** channels on now with favourites, categories and a search, a programme guide, recordings
  (scheduling and cancelling), channel change. M3U channels play straight from their source (the server as the
  fallback) and reopen by themselves when the picture stops; an XMLTV guide can be read on the console for
  servers without one.
- **Search:** results while you type on the on-screen keyboard; films, series, episodes, people, music and
  channels, narrowed to one kind with L2 / R2.
- **And:** 14 languages, an accent colour of your choice, a screensaver, updates from the settings.

### Controls

| Where | Button | Action |
|---|---|---|
| Everywhere | ✕ / ◯ | Open, play / back |
| A library | ← (first column) / △ | Jump to a letter / quick look |
| Home | Touchpad | Refresh |
| Player | ✕ | Pause, play |
| Player | ← → or L1 / R1 (held: faster) | Seek 10 s, then 30 s, 1 min, 2 min while held |
| Player | L2 / R2 held 3 s | Previous / next episode |
| Player | △ / OPTIONS | Audio, subtitles, volume, picture |
| Next-episode card | ✕ / ◯ | Play it now / cancel |
| Live TV | L2 / R2, △, touchpad | Filter the channels, favourite, search |
| Live TV player | L1 / R1 (or L2 / R2 held 3 s), ↑ / ↓ | Change channel, volume |
| Music | L2 / R2 | Albums, artists, playlists, genres, songs |

## Requirements

- A PS4 with GoldHEN (the app is a homebrew package; the console's firmware and saves are not touched).
- A Jellyfin server, version 10.10 or later.

### Tested consoles

| Type | Firmware | Status |
|---|---|---|
| PS4 Fat | 13.52 | ✅ |

## Installing

1. Download `IV0001-PLFN00000_00-PLFN000000100000.pkg` with the button at the top, copy it to the console (USB
   drive or FTP) and install it from GoldHEN → Debug Settings → Package Installer.
2. Start Playfin from the home screen. Servers on your network are found automatically; others are added by
   address (for example `192.168.1.20:8096` or `https://media.example.com`).

The app keeps its settings in `/data/Playfin/` on the console (servers, settings, downloads, the image cache and
`playfin.log`). To update, install the newer package the same way.

## Building

Linux (development): `cmake --preset linux-debug && cmake --build --preset linux-debug`.
PS4 package: `scripts/build-ps4.sh` (Docker). Details in [docs/building.md](docs/building.md), tests in
[docs/testing.md](docs/testing.md), how the code is organised in [docs/architecture.md](docs/architecture.md),
security notes in [docs/security.md](docs/security.md).

## Licence and source

Playfin is free software under the GNU General Public License v3.0 (see [LICENSE](LICENSE)). It is built with
third-party components under their own licences, listed in [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).
