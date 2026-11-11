# PS4 platform notes

What the console, the OpenOrbis toolchain and the PacBrew libraries require, and how Playfin deals with it.

## Startup, memory and exit

- The sandbox refuses `mkdir` on `/` and `/data`: `makeDirs` only creates the folders below the data root. The
  data folder is created 0777 (with 0755 no file could be created in it).
- musl's `malloc` maps 2600 MiB of system flexible memory on first use and calls `sceLibcMspaceMalloc` with a null
  mspace when that fails (crash CE-34878-0 on the first allocation). The fself needs the toolchain's default
  program authority id and auth info (`--paid 0x3800000000000035 --authinfo …`, `cmake/ps4_pkg.cmake`).
- Returning from `main` ends in `_exit`, which the kernel refuses (SIGSYS): the app closes through
  `sceSystemServiceLoadExec("exit")`.
- The OpenOrbis linker script put only `.data` itself in `.data`: `.data.*` (statics of inline functions, musl's
  `.data.__libc` and stdio `FILE`s) and `.init_array.*` were dropped by `create-fself` and read as zero on the
  console. `scripts/ps4/link.x` collects them; `scripts/check-ps4-eboot.py` checks every build.

## Clocks, sockets, names

- musl's headers carry Linux clock ids; the PS4 kernel (FreeBSD) reads `CLOCK_MONOTONIC` (1) as `CLOCK_VIRTUAL`
  (the process's CPU time), so timeouts never expired. `clock_gettime` comes from libkernel (patching musl does
  nothing): `src/platform/ps4/clock_ps4.cpp`, linked into the executable, translates the ids and calls
  `sceKernelClockGettime`.
- musl gives `SOCK_CLOEXEC`/`SOCK_NONBLOCK` their Linux values; the kernel refuses such sockets ("Protocol not
  supported"). The executable is linked with `--wrap=socket --wrap=socketpair` (`socket_ps4.cpp`): the flags are
  removed and non-blocking mode is set with `fcntl`.
- musl's resolver reads the DNS servers with `sceNetGetDnsInfo`: libSceNet must be loaded
  (`ORBIS_SYSMODULE_INTERNAL_NET`, `sceNetInit` in `backend::postInit`), otherwise the first host name lookup
  calls an unresolved stub and the system kills the app.
- musl asks for the A and AAAA records together and waits for both; behind a router that does not answer AAAA,
  every host name took musl's whole timeout (5 s) in FFmpeg (a channel's own stream, measured 2026-11-09; curl
  was spared by `CURLOPT_IPRESOLVE_V4`). The executable is linked with `--wrap=getaddrinfo`
  (`resolver_ps4.cpp`): a caller without a preference gets IPv4 only (43 ms instead of 5 s). It also removed a 2 s
  UI freeze: an engine destroyed while its resolver waited held the main thread.
- FFmpeg's own HTTPS works on the console. On 2026-11-09 it seemed not to: the console had no Internet that
  day (the Freebox answered every name with its own page, 212.27.38.252), which `player::kDirectHttps` (https
  sources through the server on the console) came from. Retested 2026-11-10 with Internet and kDirectHttps true:
  France 24 (picture in 1.3 s), Euronews (4.7 s) and TV5Monde (3.4 s) play straight from their https source;
  Arte falls back to the server after 8 s (its master playlist lists backup variants that do not answer).
  kDirectHttps is true everywhere since 2.1.
- GoldHEN SDK (2026-11-10): `syscall 500` with `GOLDHEN_SDK_CMD_VERSION` answered 0xffffffffffffff00 instead of a
  version (>= 0x100) on the user's console; the probe stopped there and never asked for the sandbox escape. The
  app's own sandbox does not show `/mnt/usb0` (opendir: No such file or directory) while GoldHEN's FTP lists it:
  USB drives are reached through GoldHEN's FTP (127.0.0.1:2121) on that console.
- In-app update (branch `2.1`), 2026-11-10 with Internet: a build calling itself 2.0.0 found 2.0.1 at once,
  downloaded it to `/data/pkg/Playfin-2.0.1.pkg` (51 511 296 bytes, digest checked), GoldHEN's Package Installer
  listed and installed it, and Playfin started as 2.0.1 with its servers and settings kept.
- Retail firmware has no runtime shader compiler (Piglet "OpenGL ES 2.0 Piglet", `GL_SHADER_COMPILER` 0), so
  NanoVG's GLES2 backend cannot run. The system's `libScePrecompiledShaders.sprx` exports compiled shaders
  loadable with `glShaderBinary`; `swordfish/vpBasic` + `swordfish/fpAlphaTexture` draw a texture upright with
  positions -1..1 and texture coordinates 0..1 (v = 0 at the top). Playfin draws everything with its software
  renderer and only presents the finished frame (and the video) through that pair (`present_ps4.cpp`).
- A full 1920x1080 RGBA upload (`glTexSubImage2D`) takes about 2 ms.

## Video and subtitles

- mpv's software renderer must use swscale's fast paths (`sws-fast=yes`, `sws-scaler=fast-bilinear`, 3 ms per
  1080p picture instead of 110 ms) and must not wait for each picture's display time
  (`BLOCK_FOR_TARGET_TIME=0`). FFmpeg counts one CPU on the PS4: `vd-lavc-threads=4`. 1080p H.264, HEVC, VP9 and
  AV1 then play at 24-25 fps; files above 40 Mbit/s or 1080p are transcoded by the server.
- mpv 0.36 is built without Lua: it has no `ytdl` option (`player::optionMayBeMissing`).
- There is no system font provider for libass: fonts come from `resources/fonts`, and
  `scripts/ps4/libass/fallback.patch` lets a character missing from the requested and default fonts come from any
  loaded font that has it (Japanese, Korean and Chinese from Noto Sans CJK).
- A server trusted by its pinned self-signed certificate is streamed with `tls-verify=no` (FFmpeg cannot check a
  pin); servers with a CA-signed certificate are verified against `resources/certs/cacert.pem`.

## System keyboard

- `sceImeDialogInit` refuses the password option (0x04, error 0x80bc0030) unless the type is
  `ORBIS_TYPE_BASIC_LATIN`; with that type password, no learning (0x20) and no auto-capitalization (0x02) are
  accepted. Passwords typed on the PS4 keyboard are therefore ASCII only.
- Without a signed-in user the keyboard cannot open; the app then falls back to its own editor.

## Time

- The local time is the console's time zone plus its summer time setting, as they are now: a programme after a
  summer time change within the guide's three days shows an hour off.

## Player (mpv)

- mpv's `access-references=no` refuses every HLS playlist ("Not opening '…m3u8' due to --access-references=no"):
  FFmpeg's HLS demuxer reopens the playlist and its segments as nested inputs. Seen on the console on 2026-11-08
  (Live TV, relayed or direct, failed since the 1.0.0 hardening); the player now allows references for HLS over
  http(s) only (`player::followsReferences`).
- UI pace: the loop presents every display slot (vsync, 16.6 ms); a software frame of up to about 30 ms still makes
  every other slot, hence a steady 30 frames a second (`ui::FramePacer`). Gradients are the renderer's slowest
  paint: the hints' 1920x104 fade alone cost 12 M cycles a frame (60 cycles a pixel), a backdrop shade 87 M.

## Themes, trickplay and speed (2.2, measured on the console on 2026-11-11)

- Projecteur's home draws a frame in 11 to 14 ms. Ultrachromic and Zesty (a backdrop that follows the focus, with
  blur or tint) take 21 to 25 ms a frame while browsing the home: still 30 frames a second at the median, with one
  or two frames of about 55 ms every 5 s when a new backdrop fades in over the last one (a full-screen blend of two
  layers over the theme's background). Done since, to measure again on the console: theme backdrop layers come
  without a fade (`ImageView::fadeIn`), only the covering layer is drawn, the background image is skipped under a
  screen that covers it (`Screen::coversBackground`: home, library and details once their backdrop is there), and
  the root is no longer painted over a frame already cleared to the background colour. Measured again the same day
  with Zesty on the home: 20 ms a frame at the median, 25 ms at most, no frame dropped while browsing.
- Card shadow (2.2.1, ElegantFin and Finimalism): a shadow sprite drawn all around each card (four rects, 22 px
  feather) took ElegantFin's home from 24-27 ms to 30-34 ms a frame, with frames dropped. Only the band under the
  card and its lower corners are drawn now (14 px feather, 8 px drop; the sprite's sides fade out upwards when it is
  made, so there is no edge where the drawing stops): 25-26 ms a frame, 30 frames a second, as without a shadow.
- Trickplay: Nayde's films have 320 px pictures; the preview shows while a seek is held. A single short press can
  apply its seek before the first sheet (3200 x 1800) has arrived, so no picture showed then: since, the sheet of the
  playing position and the next one are fetched while playing (`TrickplayPreview::follow`); checked on the console:
  one short press shows the picture.
- Speed: 1.5x confirmed by the bar's time (17 s of film in about 11 s). The video's own frame drops are only in the
  debug log (`player: fps ... decoder-drops`), not measured remotely.
- Every theme measured on the console the same day, browsing the home (frame drawn, median, after the fixes):
  Projecteur 20-22 ms, ElegantFin 24-27 ms, Ultrachromic 21-23 ms, Abyss 21-22 ms, Zesty 20 ms, Finimalism 20 ms;
  30 frames a second with no frame dropped. Before the card borders were drawn as an image, ElegantFin and Abyss
  spent 40 to 45 % of a frame stroking a rounded border around every card (ElegantFin fell to 20 frames a second):
  borders are now `ui::ringImage`, made once per card size and drawn over the corners and the thin edges only.
- Full pass on the console the same day (2.2 test build): settings (theme, accent on top and back to the theme's,
  licences with the themes' credits), search typed on the on-screen keyboard, the Downloads tab, a library with
  its quick look, a person's page, a series and its episode, the player (trickplay on a short press, 1.5x on a 1080p
  H.264 episode, 2x on a 1080p HEVC 5.1 file from the USB drive played to its end), music from the USB drive with
  its tags. `playfin.log` without an error or a warning. Remote Play sends R2 as a plain button: the L2 / R2 hold
  (next episode) cannot be checked remotely, only by the unit tests.
- Blurred backdrops (Ultrachromic, Abyss, Zesty) first came from a picture reduced to 1/32 and stretched back:
  blocky on the TV ("144p"). They are now a real blur of about 19 px at 1080p (halved, three box passes each way,
  bilinear back), baked on a worker; smooth on the console, frame times unchanged (Ultrachromic 20-23 ms).
  ElegantFin's sheen went from 22 % to 7 % white on a narrower band: too strong on the TV before.
- Crash (CE-34878-0) on 2026-11-11: on a film's details page, pressing the download button while the film was
  downloading opened the Downloads tab, which replaced the stack and destroyed the details screen still running
  the press (`refreshDownload()` was then called on the freed screen). The tab now opens on the next frame
  (`later`), and nothing of the screen is touched after; found under UBSan by a test that presses that button.
  Rule: a press that replaces the screen it belongs to defers it, and returns.
- Downloads: the file is written in 1 MB blocks (`FileSink` gathers pieces) and curl receives 512 KB blocks for a
  download (16 KB by default). On the console a film downloads at about 4.5 MB/s, the same rate as an FTP upload
  to it: the console's network is the limit (a 6 GB film takes about 22 minutes).

