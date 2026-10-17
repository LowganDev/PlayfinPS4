<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo-on-dark.png">
    <img alt="Playfin" src="docs/images/logo-on-light.png" width="300">
  </picture>
</h1>

A Jellyfin client for the PlayStation 4, for consoles running homebrew (GoldHEN).

<a href="https://github.com/LowganDev/Playfin/releases/latest/download/IV0001-PLFN00000_00-PLFN000000100000.pkg"><img alt="Download for PS4" src="docs/images/download.png" width="281"></a>

The latest version's package; earlier versions are on the [Releases](https://github.com/LowganDev/Playfin/releases)
page.


## What it does

- Several servers and several users per server; sign-in with a password or Quick Connect; self-signed HTTPS
  servers trusted on first use (their key is then pinned).
- Home with Resume, Next up, what is on TV and the latest additions; libraries sorted and filtered; details with
  seasons, favourites and watched marks; search with recent searches.
- Video: direct play of H.264, HEVC, VP9 and AV1 up to 1080p, or the server's transcode otherwise; audio and
  subtitle tracks (text and picture subtitles); resume; intro and credits skipping; next episode.
- Music: albums, artists and playlists, a queue that keeps playing while you browse, shuffle and repeat.
- Live TV: channels on now, a programme guide, recordings (scheduling and cancelling), watching a channel with
  channel change.
- 14 languages.
- Some customization.

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

The app keeps its settings in `/data/Playfin/` on the console (servers, settings, the image cache and
`playfin.log`).

## Building

Linux (development): `cmake --preset linux-debug && cmake --build --preset linux-debug`.
PS4 package: `scripts/build-ps4.sh` (Docker). Details in [docs/building.md](docs/building.md), tests in
[docs/testing.md](docs/testing.md), how the code is organised in [docs/architecture.md](docs/architecture.md),
security notes in [docs/security.md](docs/security.md).

## Licence and source

Playfin is free software under the GNU General Public License v3.0 (see [LICENSE](LICENSE)). It is built with
third-party components under their own licences, listed in [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).
