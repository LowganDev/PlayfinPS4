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

## Graphics

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
