# Testing

    ctest --preset linux-debug      # unit tests + smoke test + UI screenshots
    cmake --preset linux-asan && cmake --build --preset linux-asan && ctest --preset linux-asan

- Unit tests: doctest, `tests/unit/*_test.cpp`, one executable `playfin_tests`.
- Sanitizers: the `linux-asan` preset builds everything with AddressSanitizer and UBSan. `tests/lsan.supp` may only
  suppress leaks inside system graphics libraries. Mesa's driver is unloaded at exit, so its frames print as
  `<unknown module>`: the suppressions name the call that entered the driver (`platform::init`,
  `glnvg__renderFlush`), never a whole library.
- UI tests: `playfin --screen gallery --test-script <file> --test-output <dir>` plays a script and saves screenshots; no window and
  no display are needed (`SDL_VIDEODRIVER=offscreen`, `MESA_LOADER_DRIVER_OVERRIDE=llvmpipe` = CPU rendering,
  identical on every run). Script commands: `wait <frames>`, `press <Action>`, `down <Action>` and `up <Action>` (held across frames), `type <text>`, `screenshot <name>`, `quit`. Images load
  synchronously in test mode. The gallery (`tests/ui/gallery.script`, references in `tests/ui/references/gallery/`)
  covers rows, grid, focus ring, dialog, text editing (CJK) and screen push/pop.
  The clock is fixed at 60 fps in test mode. `tests/ui/run.sh` compares each image of `tests/ui/references/`
  (fails when more than 0.1 % of the pixels differ by more than 8 per channel).
- Shell end to end (`ui_shell`, `tests/ui/run_shell.sh`): `fake_jellyfin --https --port 48920` (self-signed, fixed
  port so the certificate dialog is identical on every run), then `playfin --screen shell --no-discovery` plays
  `tests/ui/shell.script`: add the server, trust its certificate, pick a profile, sign in with a password, browse
  home, a library (sort bar), an episode's details, the player, the menu, then the search tab (empty and with
  results), the settings tab (list, accent chooser), the licences page and a series' details. Network jobs run
  inline in test mode (deterministic) and screen transitions are off. The run fails if the access token appears in
  `playfin.log`. Quick Connect is covered by unit tests only. Runs serially (fixed port); the same references are
  used under ASan. `ui_media` plays `tests/ui/media.script` on `fake_jellyfin --media` (the test server's views:
  Live TV, music) with `--fixed-time 2026-10-16T19:45:00Z --utc-offset 120` (the guide's clock); in script tests
  the music pauses at its first moment (`frozenPlayback`). `ui_themes` plays `tests/ui/themes.script` on the same
  fake server: each theme but Projecteur chosen in the settings, then the home with a card focused (its backdrop
  followed after 400 ms). After an interrupted run the port can stay in TIME-WAIT for up to a minute: the fake server
  then fails with "port in use" and every screenshot after the address shows "cannot reach the server".
- The fake server (`tests/support/fake_jellyfin.*`) also answers what the app screens need: `/Items` honours
  `sortOrder=Descending` (reversed), `filters` (`IsFavorite`: every 10th movie, `IsUnplayed`: odd ones) and
  `includeItemTypes` (`Series` → s-1 "Test Show", `Episode` → e-1 "Pilot", for searches); `/Shows/s-1/Episodes`
  answers per season (se-2 has e-3); `/Users/{id}/Images/Primary` serves a picture. `requests()`, `count(path)` and
  `playbackInfoBody()` let tests check what was asked. Added in 2.0: `/Items/{id}/Download` (the test clip; 403
  for `m-transcode`), `/MusicGenres`, people (`includeItemTypes=Person`), `sortBy=Random` (Big Buck Bunny, with a
  backdrop), `nameLessThan`, GitHub's latest release (`publishRelease`, `/github/latest`), and the showcase.
- In the Linux UI editor (no system keyboard) the text starts from the field's value: a test that searches twice
  clears the field first.
- `tests/fixtures/` and the fake server are shared by the unit and end-to-end tests: adding image tags or fields
  to a fixture changes the shell screenshots (regenerate and review them).
- stb_image (inside `nanovg.c`) and stb_image_write are built with `-fno-sanitize=shift` under sanitizers: their
  JPEG code shifts negative values (upstream, harmless).
- Command-line checks (`tests/cli/`, CTest): a test script needs `--test-output`; a startup failure (SDL `dummy`
  driver, no OpenGL) must exit 1 and be written to `playfin.log`.
- README screenshots: `scripts/readme-shots.sh` runs the shell in English. Films: without an argument,
  `fake_jellyfin --showcase` (the open films' artwork, `scripts/showcase/fetch-art.py`, credits in
  `scripts/showcase/CREDITS.md`; the player plays Sintel's artwork as a still 15-minute video made with ffmpeg) and
  `scripts/showcase/showcase.script`; with a data folder signed in to a real server (sign in once with
  `build/linux-debug/playfin --data-dir DIR`), a copy of it and `scripts/showcase/server-*.script` (written for the
  author's library: Apocalypse Now first in the second library, Barry found by a search). Music and Live TV
  (`livetv`, `music`, `now_playing`) come from the showcase server when it runs (`scripts/showcase/media.script`,
  signed in as playfin, its own clock; `--media` makes only those); without it they are kept. Writes `docs/images/screens/*.jpg` (review them
  before committing). In a test script the player and the music stay paused until ✕ (the visualizer is flat
  before); a resumed title is reported where it was, but it comes first in Continue watching. A Quick Connect
  sign-in cannot run in a test script: the script waits for the requests in flight, and its polling never ends.
- Script tests run with the clock fixed (`--fixed-time 2026-10-16T19:45:00Z --utc-offset 120`): the header's
  time and the player's "Se termine à" are the same on every run.
- Changing the look on purpose: `scripts/update-ui-refs.sh`, then review the new images before committing. The
  references are made by the software renderer (the script's default);
  `ui_gallery_gl` checks that NanoVG's GLES2 backend still draws the same within the imgdiff tolerance.
- Network tests run against `tests/support/fake_jellyfin.*` (cpp-httplib) over HTTP and HTTPS, with test
  certificates in `tests/fixtures/tls/` (a test CA, a CA-signed server, a self-signed server; test-only keys).
- Fuzzing: `cmake --preset linux-fuzz && cmake --build --preset linux-fuzz && ctest --preset linux-fuzz`
  (30 s per target; crashes land in `build/linux-fuzz/fuzz-artifacts/`).
- Under sanitizers the vendored stb is built with `-fno-sanitize=shift` (its JPEG encoder shifts into the sign bit,
  upstream code used only by the fake server's test posters).
- On the PS4, `/data/Playfin/playfin.log` (FTP) is opened before anything else and records each startup step
  (`startup: SDL`, `window`, `OpenGL ES context`, `network`, `canvas`, `fonts and translations`, `first frame`,
  `frame 0 drawn`): the last line before a crash shows where it stopped. Every log line is also sent to the
  kernel log (`sceKernelDebugOutText`), which still works when the file cannot be written.
- PS4 startup trace: `/data/Playfin/boot.txt` is written with raw kernel calls (`sceKernelOpen`/`Write`, no
  stdio, no buffering) from a constructor that runs before main ("loaded"), then "main", "data folder ok",
  the log file open result (errno on failure; append mode falls back to "w") and the first 200 log lines. It is
  rewritten on every launch. On PS4 log timestamps are UTC (no time zone files).
- Timing on the console is read from the log, not from Remote Play pictures: over Wi-Fi the Remote Play video can
  lag several seconds behind the console, and its first input after a pause is often lost (check every step with
  a screenshot). The player logs `player: next card at <s> of <s>` and `player: end of file at <s> of <s>`
  (measured in 2.0 on a real episode: the card 10.2 s before the end of the file, the next one 0.6 s after it).
- Rendering: `raster_test` (exact pixels for the fill rule, culling, stencil, paints, scissor, edge alpha, a random
  coverage check against the pixel-centre rule, the constant-colour fast path compared byte for byte with the
  general path, the integer image path within 2 per channel, convex polygons identical to their fans) and
  `nvg_sw_test` (NanoVG scenes: rectangles, circles, holes, crossing strokes, images, text, one thread vs four).
  `ui_gallery` and `ui_shell` render through the software backend against references made by the same renderer;
  `ui_gallery_gl` runs the same script through the GL backend. `fuzz_raster` feeds random paints, states
  and triangles (non-finite and huge values included).
- `playfin_render` is compiled with -O2 in every build type (the per-pixel hot path).
- Player: `engine_test` plays `tests/fixtures/media/clip.mp4` (generated, see its README) through libmpv with
  `ao=null` (frames, seek, pause, end, missing file, external subtitle, non-blocking render);
  `playback_api_test`, `profile_test`, `player_session_test` and the player cases of `shell_screens_test` run
  against the fake server, which streams the clip for every item (live channels and transcodes as HLS: a playlist
  and `clip.ts` as its segment, so that following an HLS playlist is tested; before, the relay served the MP4
  under `master.m3u8` and a player that could not follow HLS segments passed), answers PlaybackInfo (direct, or transcode for
  `m-transcode`), media segments (`e-1`) and records the playback reports. `ui_shell` screenshots the player bar
  and the track menu over the clip's first picture.

## Test server (music and Live TV)

`scripts/test-server/up.sh` runs a real Jellyfin (Docker image `jellyfin/jellyfin:10.11.8`) on port 8097 of the
PC with generated content, for testing music and Live TV:
- Music (`make-media.sh`, ffmpeg tones, no copyrighted content): artists "Les Ondes Calmes", "Kilohertz",
  "Artistes variés" (with "Duo Sinus" on some tracks); 5 albums, 30 tracks of 30-90 s in MP3, FLAC, AAC (m4a, two
  discs) and Opus, with tags and covers; a playlist "Soirée tests" made by `setup.py`.
- Live TV: 3 channels ("Playfin Info", "Playfin Sport", "Playfin Cinéma"), each a host ffmpeg encoding a test
  pattern with the clock into live HLS (`$DATA/hls/chN`), served by an nginx container on the Docker network as
  `http://streams/chN/index.m3u8` (no port opened on the PC: the PC's ufw stays as it is; Docker publishes 8097
  itself). An M3U tuner and an XMLTV guide (3 days from the day of `up.sh`, remade on each start).
- Showcase (`SHOWCASE=1 up.sh`, `SHOWCASE=1 down.sh`; data in `~/.local/share/playfin-showcase`, the same ports,
  so stop the other first): `make-showcase.py` fetches real content once instead: 5 free albums from the Internet
  Archive (109 tracks, one VBR MP3 per original file, retagged, their covers), 4 channels streamed for free by their
  broadcasters (France 24, Arte, Euronews, TV5Monde Info; M3U entries with their own HTTPS addresses, so the
  console plays them directly) with Wikimedia Commons logos on a white tile, and their real programmes cut from
  xmltvfr.fr's guide (fetched again after 6 h). The server is "Playfin Showcase", in English, library "Music", no
  films. Credits in `scripts/showcase/CREDITS.md`. It is what the README's music and Live TV screenshots show.
- The console reaches it at `<PC address>:8097` (the PC's own Jellyfin, if any, keeps 8096). On the PS4 keyboard
  ":" is on the symbols page (L2+△ key, "@#:").
- `setup.py` (idempotent) names it "Playfin Test", lists its user on the sign-in screen, completes the wizard, creates the user `playfin` / `playfin-test`, adds the library,
  the tuner, the guide and the recordings folder (`/recordings`), refreshes them, makes the playlist.

What the real server answers (fixtures recorded from it in `tests/fixtures/jellyfin/ts/`, API keys redacted):
- A channel's media source is the tuner's URL (`http://streams/...`, unreachable from the console; started with
  `STREAMS=<this PC>:8098 up.sh` the M3U gives the PC's address instead and the console plays the channels
  directly). With
  `EnableDirectPlay: false` and no `hls` container in the direct play profiles, `PlaybackInfo` returns a
  `TranscodingUrl` (`/videos/{id}/master.m3u8?...&LiveStreamId=...`): the server relays the channel (video copied
  when compatible). The live stream closes with `POST /LiveStreams/Close?liveStreamId=` (not under `/LiveTv`).
- User views: "Live TV" (`UserView`, collection type `livetv`), the music library (`CollectionFolder`, `music`),
  "Playlists" (`UserView`, `playlists`) and "Recordings" (`CollectionFolder` without a collection type).
- Jellyfin answers 503 with an HTML page while it starts; `setup.py` waits.
