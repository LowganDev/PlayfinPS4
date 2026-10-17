# Security notes

## Network
- HTTPS certificates are verified against the Mozilla CA list shipped in `resources/certs/cacert.pem` (the
  console's own list can be outdated).
- Self-signed servers: trust on first use. The app shows the SHA-256 fingerprint of the server's public key; once
  accepted, that exact key is pinned (`servers.json`) and any other key is refused, even one signed by a known CA.
  A changed key is refused (TLS error telling the user to add the server again); trusting the new key takes
  that explicit re-add. Saved tokens are only sent to addresses the server was already known by: a new
  address (for example a discovery reply carrying a known server id) needs a sign-in there first. When an address has several candidates
  (https/http, default ports), an untrusted certificate is offered only if no candidate answers as Jellyfin.
- Redirects never go from https to http; API (JSON) replies are limited to 8 MiB, files (`Client::fetch`) to
  64 MiB; requests time out.
- Discovery keeps at most 32 servers and ignores replies with an id or name over 128 bytes or an address over
  512 bytes.
- The access token travels only in the `Authorization` header, never in URLs (images included), and
  `Client::fetch` refuses to send it to another host.

## Storage
- `servers.json` (data folder, mode 0600, atomic writes) holds server addresses, pins, user names and access
  tokens. Passwords are never stored.
- The PS4 has no secure storage: anyone with file access to the console can read the tokens. If a console is
  lost, revoke its session in the Jellyfin dashboard (Devices).
- Without a data folder, or when `servers.json` cannot be read, the app runs without saving and never
  overwrites the file.
- The app writes only inside its data folder (`/data/Playfin` on PS4); every path goes through
  `core::dataPath()`.

## Logs
- Every log line is redacted (`core::redact`): API keys, tokens, `Authorization` headers, password fields and the
  Quick Connect secret are replaced by `***`. Request bodies are never logged.

## Testing
- Server replies, discovery packets, addresses and log redaction are fuzzed (`linux-fuzz` preset).

## Review

A review of the whole code. Threats considered: someone on the network
(Wi-Fi, LAN), a malicious or compromised server, a malicious media file served by a trusted server (downloaded
content), other homebrew on the console.

| Area | Finding | Action |
|---|---|---|
| Pictures from the server | NanoVG's bundled stb_image was v2.10 (2016). Fuzzing `decodeImage` (`fuzz_image`) found an out-of-bounds write in its JPEG Huffman table code within minutes: a server could crash the app, or worse, with a crafted picture. Every decoder of stb_image was compiled in (PSD, GIF, HDR…). | stb_image 2.30 (upstream 2c980bb), only the JPEG and PNG decoders (`STBI_ONLY_JPEG`, `STBI_ONLY_PNG`), `STBI_MAX_DIMENSIONS` 4096 on top of the existing header check (4096 px a side, 8 Mpx: a progressive JPEG needs about 13 bytes per pixel while it decodes, two decodes at once on the PS4). The crashing file is a regression test (`images_test`, ASan) and a fuzz seed. 5 minutes of fuzzing without a finding after the change. |
| Subtitles (libass) | The PS4 image's libass 0.14.0 has CVE-2020-24994 and CVE-2020-26682 (crafted ASS subtitles overflowing the outline code). | libass 0.17.3 built by `scripts/ps4/libass` (with HarfBuzz 11.2.1, `scripts/ps4/harfbuzz`, required since libass 0.15). |
| Fonts (FreeType) | The image's FreeType 2.10.1 has CVE-2020-15999 (heap overflow on PNG glyphs, exploited in the wild in 2020). Fonts attached to media files reached FreeType through libass. | mpv runs with `embeddedfonts=no` (only Playfin's fonts reach FreeType), and FreeType 2.13.3 is built by `scripts/ps4/freetype` without bzip2, PNG glyphs, brotli or HarfBuzz (libpng 1.6.37 is no longer linked). |
| FFmpeg's bzip2 | FFmpeg was built with bzlib against the image's bzip2 1.0.6 (CVE-2016-3189, CVE-2019-12900), reachable through bzip2-compressed Matroska tracks. | FFmpeg built with `--disable-bzlib`; bzip2 is no longer linked. |
| Player | mpv could read configuration and scripts (already off), and a media file could make it open other files (mov references, playlists). | `player::hardeningOptions()` (tested): `config=no`, `load-scripts=no`, `ytdl=no`, `input-default-bindings=no`, `embeddedfonts=no`, `access-references=no`, `load-unsafe-playlists=no`. FFmpeg keeps only the decoders, demuxers and protocols Jellyfin's streams need. |
| TLS and the token | Verified against the shipped Mozilla list; pinned keys for self-signed servers; the token only in the `Authorization` header, sent to the server's own origin only (images, streams, transcoding URLs: `api_key` removed, other hosts refused). FFmpeg cannot check a pin: for a pinned self-signed server mpv streams with `tls-verify=no` (docs/ps4-notes.md). Plain HTTP servers on the LAN send the token in clear, as every Jellyfin client does. | No change (documented above). |
| Parsing | JSON replies, discovery packets, URLs, settings, timers: bounded sizes, fuzzed (`fuzz_models`, `fuzz_discovery`, `fuzz_url`). | `parseTimers` and `parseUtc` added to `fuzz_models`. |
| Files | Writes only in the data folder (`core::dataPath`), atomic, 0600 for servers.json and settings.json; caches bounded. | No change. |
| Logs | Redacted (`core::redact`); request bodies never logged; the access token test of `ui_shell`. | No change. |
| Other dependencies | curl 8.22.0, Mbed TLS 3.6.7, FFmpeg 7.1.5, dav1d 1.5.4, zlib 1.3.1 are current. mpv 0.36.0 (2023), SDL2 2.0.18 (PacBrew PS4 port), FriBidi 1.0.4 and libsamplerate 0.1.9 are older: SDL2's published flaws are in loaders Playfin does not use (BMP, WAV); FriBidi's (CVE-2022-25308/25309/25310) are in its command-line tool and in functions libass does not call. | Recorded; to revisit when updating mpv (a newer mpv needs the PS4 patch ported). |

The PS4 dependency packages are built from `scripts/ps4` by `scripts/ps4/build-deps.sh` and published on the
`deps` release of the repository (docs/release.md).
