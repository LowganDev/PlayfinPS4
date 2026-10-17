# Building

## Linux (development and tests)

Ubuntu 24.04 packages:

    sudo apt install clang cmake pkg-config libsdl2-dev libgles-dev libegl-dev mesa-utils libcurl4-openssl-dev libssl-dev

    cmake --preset linux-debug
    cmake --build --preset linux-debug
    ./build/linux-debug/playfin

Options: `--frames N`, `--hidden`, `--data-dir DIR`, `--log-level debug|info|warn|error`. Data and log go to
`~/.local/share/playfin/` by default. Keyboard: arrows, Enter (Cross), Escape/Backspace (Circle), T (Triangle),
X (Square), Q/E (L1/R1), 1/3 (L2/R2), M (Options).

## PS4

Needs Docker. The build runs in `xfangfang/pacbrew:250221` (OpenOrbis toolchain, clang 12, CMake 3.28) with the
dependency packages listed in `scripts/ps4/deps.txt`, taken from `build-deps/` when present (built by
`scripts/ps4/build-deps.sh`), else from the `deps` release of the repository.

    scripts/build-ps4.sh

Output: `dist/IV0001-PLFN00000_00-PLFN000000100000.pkg` (title id PLFN00000). The app stores its data and
`playfin.log` in `/data/Playfin/`.

The build ends with `scripts/check-ps4-imports.sh`, which fails if the executable imports a system module by path
(see docs/architecture.md).

Pitfall: the image entrypoint runs `cd /src; bash -c "$@"`, so the build must be passed as one command string.

Before installing on a console: back up your saves and settings, never cut the power during an installation, and
prefer external storage for tests.

## PS4 package settings

- `create-fself` gets the toolchain's default program authority id (`0x3800000000000035`) and auth info
  (capabilities). Without the auth info the app may not map system flexible memory, and the PS4 musl `malloc`
  (which maps 2600 MiB of it on first use, then calls `sceLibcMspaceMalloc` with a null mspace if that failed)
  crashes on the first heap allocation.
- `param.sfo`: APP_TYPE 1, CATEGORY gde, FORMAT obs, SYSTEM_VER 1020, ATTRIBUTE 0x2 (system dialogs follow the
  console "Enter button" setting).
- Diagnostics build: configure with -DPLAYFIN_GPU_PROBE=ON to log the GPU, the firmware shader programs
  (interfaces and a test pattern) and full-frame fill/upload timings at startup.
- Debugging a crash on the console: GoldHEN's kernel log server (`nc <ps4-ip> 3232`, reachable while GoldHEN
  runs) shows the crash report (signal, registers, backtrace of `eboot.bin` addresses). The ELF is linked at 0;
  on the console it is loaded at 0x400000: `addr2line -f -C -e build/ps4/playfin <address - 0x400000>`.
  The app's own lines appear there as `playfin boot: …` (see testing.md).
- The PS4 link uses `scripts/ps4/link.x`, the toolchain's linker script with `.data.*` and `.init_array.*`
  collected (the original left them as orphans: `create-fself` dropped the data and prioritised constructors
  never ran, docs/ps4-notes.md). `build-ps4.sh` then runs `scripts/check-ps4-eboot.py`, which fails when an
  initialised data section of the ELF did not reach eboot.bin (load-time relocation slots ignored).
- Remote testing (tooling outside the repo): Remote Play gives the console's screen and controller input; packages
  are uploaded to `/data/pkg` over FTP and installed from GoldHEN → Debug Settings → Package Installer.

