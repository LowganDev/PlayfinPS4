# Architecture

Playfin is a C++17 application for the PS4 (OpenOrbis) with a Linux build used for development and tests.

## Modules (src/)

| Module | Role | May depend on |
|---|---|---|
| core/ | logging, secret redaction, data-dir paths, file reads and atomic writes, worker pool and main queue, results and errors, UTF and string helpers, boot trace, version | nothing else in Playfin |
| platform/ | SDL window + OpenGL ES 2 context, input actions and text events, system keyboard, system language, data and resource dirs; one backend per target (linux/, ps4/) | core |
| render/ | software rasterizer and its NanoVG backend; the GL canvas and framebuffer capture (`canvas.cpp`, built into the platform library) | core, platform |
| net/ | URLs, HTTP over curl, certificate hashing | core |
| jellyfin/ | JSON access, models, client, servers.json store, discovery | core, net |
| ui/ | toolkit: views, layout, focus, lists, images, translations, Application | core, platform |
| testing/ | UI test script parser, image comparison (Linux test builds only) | core, platform, render |
| player/ | libmpv engine (software renderer), stream URLs, device profile, session logic (stream choice, seeking, reporting, segment buttons, tracks) | core, jellyfin |
| app/ | app loop, gallery, test shell (`app/shell`: session, screens, player screen) | everything above |

## UI toolkit (src/ui)

- Identity ("Projecteur": a dark room with a light from above): tokens in `ui/theme.hpp` (near-black background, Sora,
  radius 12, accent setting with `accentPalette()` / `applyAccent()`, PlayStation blue by default). When the theme
  changes (`themeGeneration()`), the Application calls `View::onThemeChanged()` on the tree so views that copied
  colours take them again. Fonts: Sora Regular/SemiBold/Bold (`FontWeight`), Noto Sans then Noto Sans CJK as
  fallbacks; ru/uk/vi use Noto Sans as the primary face.
- Background: `ui::haloImage` (accent light from above) drawn 1:1 by the Application on its lit rows only
  (`haloLitRows`, 28.5 % of the height); the frame is cleared to the background colour (`clearColor()`), and to
  transparent under a video.
- Focus: Buttons and fields fill with the accent (`drawsOwnFocus`, no ring); shell cards grow to 1.07, rise 6 px and
  glow (a cached `glowImage` sprite drawn around the picture only); lists draw their focused item last. Cover images
  are decoded to exactly their frame size (cut, or scaled up when smaller) so the renderer copies their rows.

- Views own their children; frames are relative to the parent; drawing is local (NanoVG transforms).
- Layout runs only when a view is marked dirty or an animation is running (lists materialize items while they
  scroll). Sizes per axis: Fixed, Fill (weight), Fit (measured; `TextMetrics` makes it testable without GL).
- Focus: `FocusManager` — overrides, then the containers' `navigate()` (lists move by index), then a geometric
  search in the scope (screen or top modal); containers with `rememberFocus` give focus back to their last item.
  Destroyed views remove themselves from the focus and the animator.
- `VScroll` (Leading) brings the focused row to the top, except while no focusable row is above it (titles stay).
- Lists (`HList`, `Grid`) are virtualized: only the visible items plus one screen ahead exist; items are recycled
  and re-bound by a `DataSource`.
- Images: memory LRU (192 MiB) → disk cache (`<data>/cache/images`, 300 MiB, files `<fnv1a64>.img`) → fetch
  (injected by the app). Decode and downscale on workers, at most 4 texture uploads per frame. Pictures over
  4096 px on a side or 8 MP are refused before decoding; a cache file that does not decode is deleted and
  fetched again; work for requests dropped while queued (fast scrolling) is skipped, and nothing is uploaded
  for a request nobody waits for any more.
- `setSource` on a list drops the old item views (they belong to the old source) and moves the focus to the
  new first item when the list had it.
- A `TextField` destroyed while its keyboard is open never receives the result.
- Text: Noto Sans (regular, bold) with Noto Sans CJK as fallback (face chosen from the language); line height at
  least 1.3 × the font size.
- Translations: `resources/i18n/<tag>.json` (14 languages), English fallback, plurals one/few/many/other.
- System keyboard (PS4): `sceImeDialog`, UTF-16 `char16_t` buffers (OpenOrbis `wchar_t` is 4 bytes). Passwords use
  the basic Latin type with the password, no-learning and no-auto-capitalization options (docs/ps4-notes.md). Linux:
  the Application shows an editor fed by SDL text events.

## Rendering (src/render)

- UI frames are drawn on the CPU: `render/nvg_sw` is a NanoVG backend that records calls like `nanovg_gl.h` and
  replays its GLES2 passes at `nvgEndFrame` on `render/raster` (pixel-centre rule with top-left ties, stencil,
  NanoVG's fragment shader, premultiplied blending) into a 1920×1080 RGBA `Surface`. The PS4 firmware cannot
  compile shaders, so NanoVG's GL backend cannot run there (docs/ps4-notes.md).
- Speed: rows are rasterized as spans (corrected to the exact pixel rule), the stencil is skipped when a draw does
  not use it. Convex fills (NanoVG's rounded rectangles) are one `ConvexPolygon` primitive with one span per row,
  not a fan of thin triangles; NanoVG's glyph quads are recognised and drawn as one textured rectangle each.
  Solid colours blend through 256-entry tables (one 32-bit load/store per pixel; a plain store when opaque).
  Unrotated images use integer bilinear filtering, two channels per 32-bit operation (weights in 1/256, within 2
  per channel of the float path); opaque untinted images skip premultiplying and blending, and when they are drawn
  1:1 on whole pixels each row is a `memcpy`. Text uses the same integer filtering on the alpha atlas. The fast
  paths also work under an axis-aligned scissor that cuts the shape: pixels at mask 1 take the fast loop, pixels
  on the scissor's edge go through the general shader, pixels outside are skipped. The constant-colour path draws
  exactly what the general path draws (tests compare bytes). The frame is cut into small horizontal bands (6 per
  thread, 4 threads on the PS4) that the threads take in turn; the result does not depend on the thread count.
  Raster counters are added per draw call (no shared atomics per row). PS4 code is compiled with
  `-march=btver2` (the Jaguar CPU: SSE4.1 rounding instead of libm calls).
- Images: `ImageView` with `Fit::Cover` asks the loader for `ImageShape::Crop`: decoded, box-filtered to cover
  the frame, then cut to exactly the frame's whole-pixel size; the view draws it 1:1 shifted onto whole pixels
  (under half a pixel), so the renderer copies rows. It skips its placeholder under an opaque picture that covers
  it (decoded images record whether every pixel is opaque).
- Console measures (real server): home while the focus moves takes 16.7-18.8 ms per drawn frame (median), details
  about 14 ms. Cycles per pixel on the Jaguar (one thread): clear or opaque fill 2.6, 50 % fill 11, image 1:1 10, scaled image 35-52. Frames
  above 20 ms log a `slow frame:` line (views vs flush time, each thread's busy time, pixels per path, share of
  time per kind of call).
- Presentation: `platform::presentFrame` uploads the frame into one texture and draws one quad; Linux compiles a
  GLSL quad, the PS4 uses a firmware program from the console's `libScePrecompiledShaders.sprx`.
- Frames are redrawn only when `Application::needsDraw()` (input, layout, jobs, images, animations, toast, focus
  ring); views that change every frame (spinner) set `Context::redrawNextFrame` while drawing. Unchanged frames
  show the last texture again without uploading.
- `--renderer gl` (Linux) keeps NanoVG's GLES2 backend for comparisons; `--bench N` draws N frames and logs the
  times; a `frames:` line every 300 frames gives the drawn count and raster/present times.

## App screens (src/app/shell)

The screens of the app.
- `AppSession`: the HTTP client (Mozilla CA list from `resources/certs`), `servers.json`, `settings.json`
  (`app/settings`: accent, maximum bitrate, language, recent searches; atomic write through `core/file_io` like
  servers.json; a damaged file is kept as `.bad`), the per-library sort/filter choice of the session, and the
  `Client` of the current server and user (shared pointer, readable from workers). It also fetches images for the
  `ImageLoader`, through `Client::fetch` (the token only goes to the current server).
- Settings apply at startup before the first frame (`app.cpp`): the accent through `ui::applyAccent`, the language
  instead of the console's when set. A language changed in the settings applies at the next start (fonts and
  strings are loaded once).
- Tab screens (home, search, settings) start with the `NavBar` (logo, three `NavTab`s, "server · user" and the
  avatar); a tab replaces the whole stack (`openTab`), so ○ on a tab screen does nothing. Libraries, details, the
  player, the licences page are pushed. `VScroll::setHeader(navBar)` keeps the bar on screen for the first row.
- Home refreshes Resume and Next up in place when shown again (rows keep their focus); a row that appears or
  empties changes at the next visit. Library: `library_query` maps sort (name, date added, year, rating, order)
  and filter (all, unwatched, favourites) to `ItemQuery`; pages of an older listing are dropped (generation).
  Details: genres, favourite/watched toggles (`setFavorite`/`setPlayed`), the first season with unwatched episodes,
  previous/next episode (`Application::replaceTop`, deferred), reload when shown again (back from the player).
  Search: three queries (Movie, Series, Episode; 24 each), recent searches in settings. Profiles: `ProfileCard`
  (the user's picture from `Client::userImageUrl`, else the initial), "Another user", Quick Connect.
- Transitions (`ui::Application`): a pushed screen fades in and rises 24 px (40 px for details, `enterRise`) over
  350 ms; the screen revealed by pop and a replaced screen fade in over 250 ms; `ui::revealChildren` brings rows in
  35 ms apart (fade + 30 px). `View::opacity`/`shiftY` are applied in `drawChildren` (alpha multiplied down the
  tree). Off in script test mode (`setTransitions(false)`), on in unit tests.
- Start: home when the last server has a usable address and the last user a token; otherwise the servers screen
  (discovery, then "Add a server"). A refused token sends the user back to the users screen and is removed.
- Screens derive from `ShellScreen`: `run(job, done)` runs the job on the worker pool and `done` on the main
  thread, dropped if the screen is gone (cancel token); inline in test mode for deterministic screenshots.
  `done` may replace or pop the screen and never touches it afterwards.
- Flow: servers → add server → certificate (trust on first use) → profiles → sign-in (password or Quick Connect)
  → home (resume, next up, libraries, latest per library) → library grid (paged, sorted, filtered) → details.
  The OPTIONS/Triangle menu on home and the Account rows of the settings switch user or server, or sign out.

### Added in 2.0

- Hints bar (`ui/hints.{hpp,cpp}`, drawn by `Application::draw` over a fade at the bottom left): `hintItems`
  (pure, tested) turns the focus chain's labelled bindings into entries in a fixed order: "Navigate" (the d-pad)
  when the focus scope has two focusable views and allows it (`View::navigationHint`), directions with a label of
  their own (← A–Z, ↑↓ volume), ✕, △, □, the shoulder pairs (one entry for L2/R2 when one is labelled), OPTIONS,
  touchpad, then ◯: the screen's own Back binding when it has one, else "Back" when a screen or a dialog can be
  closed. A binding's optional `shown` predicate hides its label (the player's labels show with its bar, Pause or
  Play by state); a second binding of the same button with a `shown` and a handler that returns false gives that
  button another label. `View::hidesHints` on the scope hides the bar (screensaver). Entries with the same label
  merge (← → and L1 R1 in the player). A `VScroll` in the Nearest policy keeps a focused row `nearestBottomMargin`
  (136) above the bottom, clear of the hints (seen on the console: the settings' last row half under them);
  dialogs set it to their own spacing.
- Player buttons: `Application::held(action)` follows ActionDown / ActionUp (the platform repeats directions only);
  `Application::repeating()` tells such a repeat from a press. ← / → and L1 / R1 seek alike: the platform's
  repeats are ignored and `player::ButtonRepeat` (400 ms, then every 200 ms, on time: a late tick catches up, at
  most 5) repeats while held, polled in the player's tick. Frame-based repeats made the seeking depend on the
  frame rate, so slower while a video plays and not the same for the arrows and L1 / R1 (seen on the console:
  +95 s against +225 s for 3 s held). `seekStep` grows with the hold (10, 30, 60, 120 s). L2 / R2 go through
  `player::HoldToSkip` (3 s; the hold panel shows its progress; released early, nothing): the previous or next
  episode (`Extras::previous` / `next`, from `jellyfin::adjacentEpisodes` over every season's episodes:
  `Client::episodes(seriesId, "")`; specials only around a special), or channel on Live TV. Test scripts hold a button with
  `down <Action>` / `up <Action>`.
- Live TV direct (`player_screen.cpp`): `chooseStream` keeps `sourceUrl` (`directSourceUrl`: endless, Protocol
  Http, an http(s) Path); the player tries it first on an engine of its own (`engineOptionsDirect`: no Jellyfin
  header, FFmpeg reconnect options), falls back to the server's relay on an engine error or with no picture after
  8 s, and opens a live stream again after 15 s without a new picture (`watchLive`). The live stream PlaybackInfo
  opened is closed as before.
- XMLTV guide on the console: `jellyfin::XmltvReader` (streaming, plain or gzip told by the first bytes, keeps
  the programmes of [now - 6 h, now + 4 days]) and `app::TvGuide` (owned by AppSession: downloads
  `Settings::xmltvUrl` on the transfer thread at start and when the address changes; matches a server channel to
  a guide channel by `channelKey` of its name, then by number; `fill` gives channels without a programme on now
  the guide's, the Live TV guide list takes the guide's programmes when the server has none). Programmes from it
  have no id: no recording is offered for them.
- Music visualizer: the PS4 FFmpeg has no filters, so mpv cannot hand out the sound. `player::AudioAnalysis`
  decodes the track a second time on a thread of its own (libavformat / libavcodec / libswresample, the player's
  header and TLS settings, interruptible) into `player::SpectrumBuilder` (mono 22 050 Hz, Hann window of 1024, FFT,
  32 log-spaced bands from 40 Hz to 10 kHz, -70..0 dB to 0..255, 30 frames a second). `MusicPlayer::spectrum`
  starts it on first use for the current track; the Now Playing `Visualizer` draws the frame at the playback
  position (interpolated) and asks for the next frame while playing. Test mode waits for the whole short track.
  Linux links FFmpeg through pkg-config (`playfin_av`); the PS4 has it in libmpv's static libraries.
- Live search (`search_screen.cpp`): a key grid (letters, digits, ' - ., space, delete, clear; □ deletes) types
  into the field; `Application::after` (frame-time timers, cancelled by their owner, ShellScreen cancels its own
  when it goes) starts the search 0.4 s after the last key. The field's ✕ still opens the system keyboard.
  Typed searches join the recent ones when a result is opened; confirmed ones at once.

- Details (`browse_screens.cpp`): `Item::people` and `Item::mediaSources` come with a single item
  (`/Items/{id}` answers every field); `qualityChips` (resolution, `VideoRangeType`, codec, channels),
  `directorsLine`, `endsAtText`; a version chosen (`makePlayer(…, mediaSourceId)`); the cast as Poster cards of
  `ItemType::Person` items (`Item::role` under the name), a person (`person_screen.cpp`: `/Items/{id}` for the
  biography, `/Items?personIds=` films and series).
- Libraries: ← in the first column (a binding of the grid, before navigation) opens the A–Z card; the index of a
  letter is the count of names before it (`nameLessThan`, `limit=0`), the pages up to it are loaded
  (`continueJump`), then `VirtualList::itemView(index)` is focused. △ opens `openPeek` (`peek.cpp`, a modal whose
  details arrive later, dropped when it closed). `ImageView` breathes its placeholder while a picture is on its
  way (`redrawNextFrame`, one plain fill).
- Home: `orderLibraries` (settings `libraryOrder`, `library_order_screen.cpp`: ✕ takes, ↑ / ↓ move, ✕ puts down);
  `ClockLabel` in the nav bar, refreshed by the app's loop when the minute changes (`ClockLabel::tick`); the
  touchpad (`Action::Touchpad`, SDL's touchpad button) replaces Home; the downloads in a row.
- Search: six kinds (`searchKinds()`: films, series, episodes, people, music, channels), L2 / R2 keep one (100 of
  it).
- Music: tabs Genres (`/MusicGenres`, a genre opens its albums with `genreIds`) and Songs (`Audio` items of the
  library; a song plays with the songs listed after it); L2 / R2 move between tabs; `MusicPlayer::setVolume`.
- Live TV: `filterChannels` / `channelFilters` (`guide.cpp`, pure): favourites (`UserData.IsFavorite`, △ toggles
  `/UserFavoriteItems/{id}`), recently watched (settings `recentChannels`, 8), the kind of the programme on now
  (`IsMovie`, `IsSports`, `IsNews`, `IsKids`), radios (`ChannelType`); a search by name or number.
- Downloads (`downloads.{hpp,cpp}`, owned by `AppSession` like the music): `downloads.json` and the files in the
  data folder's `downloads/` (`<id>.<container>`, `<id>.jpg`), one at a time on `AppSession::transfers()`, a
  thread of its own (never the worker pool: the PS4 has 2 workers, and a film takes hours)
  (`Client::downloadItem`: `/Items/{id}/Download` through `HttpClient::download` into a `core::FileSink`); free
  space checked against the version's `Size`, then again during the transfer (once the length is known, then
  every 64 MB: episodes have no size beforehand), 512 MB kept free; progress posted to the main thread once per
  percent (per megabyte without a length); a failed or refused one says why. A lost connection stops the queue
  (the others stay queued). At exit `AppSession::stopTransfers()` cancels the transfer and joins its thread
  before the main queue goes; the part file left is removed at the next start. The player plays a finished download from its file without the
  server (`engineOptionsLocal`, no PlaybackInfo, no report) and keeps where it was left in the list. Pictures of
  downloads are `file://` URLs that `AppSession::fetchImage` reads from `downloads/` only.
- Software update: built in 2.0 then kept for 2.1 (branch `2.1`: `app/update.{hpp,cpp}`, the settings row, the
  settings tab's dot, `core::packagePath` for `/data/pkg`); 2.0 ships without it (no Internet to test it on the
  console before the release).
- PS4 accounts: `platform::consoleUserId()` (`sceUserServiceGetInitialUser`); `servers.json` keeps a
  `ConsoleProfile` (server, user) per account and an optional shared sign-in; `startTarget` starts each account on
  its own user, gives an installation from before 2.0 to the first account that starts it, else the shared
  sign-in, else the servers screen. Saved users and their tokens stay common to the console's accounts; choosing
  one always updates the account's profile (another account may have chosen the same user last).
- Screensaver (`screensaver.{hpp,cpp}`): after `kIdleMs` (5 min) without input, no modal and no video, a modal of
  random films and series with a backdrop (`sortBy=Random`), one every 12 s with the title, synopsis and time;
  any button closes it (each action bound).

## Threading

- The UI, all OpenGL calls and SDL event handling run on the main thread only.
- Network and disk work runs on a worker pool (`core::WorkerPool`, 2 workers on PS4); results are posted to
  the main queue (`core::MainQueue`) and applied between frames. No UI object is touched from a worker.
- The main queue is created before the pool so it outlives it: workers still post while the pool joins.

## Rules

- Every file the app writes goes through `core::dataPath()`: it refuses any path leaving the data dir
  (`/data/Playfin` on PS4).
- Log lines go through `core::redact()`: tokens, API keys and passwords never reach the log file.
- Errors: functions return `false`/`std::nullopt` and log the reason; startup failures end with exit code 1.
  Programming errors abort through `assert`.
- Rendering: OpenGL ES 2 on both targets (Mesa on Linux, piglet on PS4), so tests on Linux use the console's
  rendering path. Logical resolution 1920×1080, scaled to the drawable size.
- Input: devices are translated to `platform::Action`; held directions repeat after 400 ms, then every 80 ms.

## PS4 specifics

- `SDL_Init` runs before any system service query: it loads the piglet and system modules (reading the system
  language earlier crashed the old app with CE-34878-0).
- The splash screen is hidden once the window and GL context exist (`sceSystemServiceHideSplashScreen`).
- Packaging (`cmake/ps4_pkg.cmake`): `create-fself` → `param.sfo` (PkgTool.Core) → `create-gp4` → `pkg_build`.
  `param.sfo` versions are "XX.YY" (0.1.0 → 00.01); ATTRIBUTE 0x2 makes system dialogs follow the console's
  Enter button setting.
- System modules must be imported by bare name (`libScePad.so`, not a path): linking the OpenOrbis stub libraries
  through pkg-config's `IMPORTED_TARGET` passes their full path, and lld records `/opt/pacbrew/...` as the module
  to load, which the console cannot find. On PS4, SDL2 and the Sce libraries are linked by name (`playfin_sdl2` in
  CMakeLists.txt); `scripts/check-ps4-imports.sh` runs after every PS4 build and fails on any path.
- `playfin.log` is opened as soon as the data folder exists (inside `platform::init`, before the window), so a
  window or GL failure at startup is written to `/data/Playfin/playfin.log`.
- Free space on PS4: the OpenOrbis musl `statvfs` is a stub (it calls `statfs` into a buffer of its own and
  never fills the caller's), so it always reads 0. Its `statfs` is `open` + the system library's `fstatfs` and
  fills FreeBSD's `struct statfs` (`f_bavail` blocks of `f_bsize` bytes): `Downloads::freeBytes` uses that on PS4.
  It returns the descriptor it opened (15 on the console), not 0: only a negative value is a failure. The first
  call is logged (`downloads: statfs ...`); measured on the test console: 4096-byte blocks, 389 GB free of 444.
- curl on PS4 is the static `libcurl.a` of `scripts/ps4` (no pkg-config file): linked by name with mbed TLS, zlib
  and SceNet (`cmake/dependencies.cmake`). IPv4 only, no TCP keepalive, DNS-only share between handles.
- The OpenOrbis `sys/socket.h` has FreeBSD values for socket-level options (`SOL_SOCKET` 0xffff, `SO_BROADCAST`
  0x20) but Linux values for TCP-level ones (`TCP_KEEPIDLE` 4 = `TCP_NOPUSH` on the console): never set TCP-level
  options on PS4. `discovery.cpp` asserts the socket-level values at compile time.
- No `thread_local` object with a destructor: it needs `__cxa_thread_atexit_impl`, which no PS4 system module
  provides (create-fself refuses the eboot). curl handles are borrowed from a mutex-protected pool instead.
- The PS4 libc++ has no float overloads of `fabs`/`fmin`/`fmax` (calls convert to double and fail the
  `-Wimplicit-float-conversion` check): use `std::min`/`std::max` and explicit casts.
- `orbis/_types/common_dialog.h` uses `size_t` without including `<cstddef>`: include it first.
- PS4 sandbox: `mkdir` on an existing system folder (`/`, `/data`) fails with a permission error, not `EEXIST`:
  `core::makeDirs` never calls `mkdir` on a folder that exists. Without a usable data folder the app still runs
  (no data root: `dataPath()` returns nothing, so nothing is written).
- PS4 data folder permissions: on PS4 `core::makeDirs` creates folders as 0777 and chmods them (the umask would
  give 0755), and opens up an existing data folder; with 0755, files cannot be created in `/data/Playfin`.
- PS4 C library (PacBrew musl port): never `fopen(..., "a")` — append mode makes `fcntl` calls that crashed the
  app on the console; open with "r+" (or "w" when missing) and seek to the end instead.
- PS4 time: `clock_gettime` is defined in the executable (`platform/ps4/clock_ps4.cpp`) to translate Linux clock ids
  to FreeBSD ones (docs/ps4-notes.md); never rely on the kernel's own `clock_gettime` with musl's constants.
- PS4 sockets: `socket()`/`socketpair()` are wrapped at link time (`--wrap`, `platform/ps4/socket_ps4.cpp`) to drop the
  Linux-valued SOCK_CLOEXEC/SOCK_NONBLOCK flags musl's headers carry (docs/ps4-notes.md). Values of musl's headers that
  reach the PS4 kernel unchanged must be checked against FreeBSD's (clock ids, socket flags, TCP keepalive options).
- PS4 DNS: musl's resolver calls `sceNetGetDnsInfo`; libSceNet is loaded at startup (`backend::postInit`), otherwise
  the first host name lookup calls an unresolved stub and the system kills the app (docs/ps4-notes.md).

## Player (src/player, app/shell/player_screen.cpp)

- `player::Engine` wraps libmpv: `vo=libmpv`, no hardware decoding, its software render API converts each picture
  to rgb0 at the video's own size (swscale fast paths: `sws-fast`, `fast-bilinear`), never waiting for the
  display time (`BLOCK_FOR_TARGET_TIME=0`). mpv's threads demux, decode (`vd-lavc-threads=4` on the PS4: FFmpeg
  counts one CPU there) and play the sound (`ao=sdl` on the PS4). Subtitles are rendered by mpv (libass, fonts
  from `resources/fonts`, Noto Sans by default; no system font provider, so the PS4 libass carries
  `scripts/ps4/libass/fallback.patch`: a character missing from the requested and default fonts comes from any
  loaded font that has it, Noto Sans CJK for Japanese, Korean and Chinese). The `Authorization` header goes through `http-header-fields`; no token in URLs.
- Presentation: two layers. The player screen registers as the `ui::VideoSource`; the frame loop asks it for a
  new picture every frame, clears the UI surface to transparent (the root's background too) and the presenter
  draws the video letterboxed (GPU-scaled, linear) and then the UI over it (premultiplied blending). Test
  screenshots use `render::compositeForTest`.
- Jellyfin: `Client::playbackInfo` (device profile from `player::deviceProfileJson`), `mediaSegments` (404 =
  none), `reportStart/Progress/Stopped`. `player::chooseStream` picks direct play
  (`/Videos/{id}/stream?static=true`) or the server's HLS transcode (`api_key` removed); external subtitles are
  added with `sub-add`.
- Session logic is pure and unit-tested: `SeekAccumulator` (one seek 300 ms after a burst, clamped), `Reporter`
  (start, progress every 10 s and on events, stop once), `buttonAt` + `ButtonTimer` (skip intro / credits, 8 s
  on screen), `NextCard` (the next episode in the last 10 s, cancelled until sought back, plays on at the end),
  `audioTracks` / `subtitleTracks` (mpv ids: embedded first, external after), `needsRestart` (audio in a
  transcode, burned-in subtitles).
- The player screen: ✕ pause, ← / → ±10 s (30 s held), L1 / R1 ±60 s, L2 / R2 ±5 min, ↑ / ↓ bar, △ / Options
  tracks, volume and picture, ○ back (stop reported). The next-episode card takes the focus (✕ now, ◯ cancel).
  The picture: `VideoLayer::fit` and `scale` from the settings; `render::videoBox` places it for both presenters
  (Fill goes past the screen, the viewport cuts it). Reports run on the worker pool and outlive the screen. In test mode the first picture is shown
  synchronously and paused (deterministic screenshots).


## Music and Live TV

- Music plays in the background: `app::MusicPlayer` (owned by `AppSession`, created on first use) holds a
  `player::Queue` (pure: album order, shuffle with the current track first, repeat off/all/one, previous restarts
  after 3 s) and an `Engine` with `audioOnly` (`vid=no`: album art is not a video track). One playback session per
  track (PlaybackInfo with the audio profile, `/Audio/{id}/stream?static=true`, Start/Progress/Stopped). The frame
  loop calls `AppSession::updateMusic()`; listeners (Now playing, the nav bar chip) are told after a change, once
  per frame at most. A track that fails is skipped; a queue where every track fails stops. The music stops before
  a video or a channel starts, and when the server or the user changes (the stop is reported to the old server).
- Screens: music library (albums / artists / playlists, square 260×260 covers), album / artist / playlist
  (`TrackRow`s, disc headings), Now playing (cover, seek bar ← / → ±10 s, controls, up next; L1 / R1 previous /
  next), `NowPlayingChip` in the nav bar.
- Live TV (the `livetv` view): on now (channels with the programme on now), guide (channels left, the focused
  channel's programmes right, by local day; `app/shell/guide` is pure), recordings (recorded, scheduled). A later
  programme can be recorded (`createTimer`: the server's defaults posted back) or cancelled. Home has "À la
  télévision" (`/LiveTv/Programs/Recommended?isAiring=true`) and no "latest" row for Live TV or playlists.
- Live playback: `PlaybackRequest::liveTv` asks `AutoOpenLiveStream` with direct play disabled, so the server
  relays the channel as HLS (the tuner's own URL may be unreachable from the console); the player in live mode has
  no seeking (← / → show the bar), L1 / R1 change channel (wrapping, `replaceTop`), the bar shows the programme on
  now; the live stream is closed once on leaving (`POST /LiveStreams/Close`). Reports carry `LiveStreamId`.
- Local time: `AppSession::now()` / `utcOffsetMinutes()` (fixed in tests); PS4: system TIME_ZONE + SUMMERTIME.
- Profiles also list the users saved on the console (with a token) that the server hides from its public list.
