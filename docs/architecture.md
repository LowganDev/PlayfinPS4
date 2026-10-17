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
  (start, progress every 10 s and on events, stop once), `buttonAt` + `ButtonTimer` (skip intro / credits, next
  episode in the last 30 s, 8 s on screen), `audioTracks` / `subtitleTracks` (mpv ids: embedded first, external
  after), `needsRestart` (audio in a transcode, burned-in subtitles).
- The player screen: ✕ pause, ← / → ±10 s (30 s held), L1 / R1 ±60 s, ↑ / ↓ bar, △ / Options tracks, ○ back
  (stop reported). Reports run on the worker pool and outlive the screen. In test mode the first picture is shown
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
