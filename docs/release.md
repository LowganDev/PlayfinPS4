# Release process

The checklist for a release; release tags are the version number without "v" (`1.0.0`).

## 1. Prepare the version

1. `project(Playfin VERSION x.y.z ...)` in `CMakeLists.txt` (the app's version and the package's APP_VER follow;
   `version_test` checks the app's).
2. `CHANGELOG.md`: the version's section with its date.
3. The PS4 dependency packages: when `scripts/ps4/deps.txt` changed, build them with `scripts/ps4/build-deps.sh`
   and upload the new ones to the `deps` release (see "Dependency packages" below). A changed recipe always gets a
   new `pkgrel`, so a package name never stands for two builds.
4. Run the CI workflow (Actions → CI → Run workflow).

## 2. Check

- `ctest --preset linux-debug` and `ctest --preset linux-asan` green; `cmake --build --preset linux-fuzz` and
  `ctest --preset linux-fuzz` (30 s per target) without findings.
- `scripts/build-ps4.sh`: the imports, eboot and licences checks pass (a library linked without a licence line
  in `resources/licenses/manifest.txt` fails the build).
- Licences: every component of `resources/licenses/manifest.txt` matches the versions in `scripts/ps4/deps.txt`
  and the pacbrew image; `THIRD_PARTY_LICENSES` agrees.
- Security: `docs/security.md` reviewed for what changed since the last release (dependencies' versions
  against their published vulnerabilities).
- On the console (back up saves and settings first, never cut the power during an install, prefer external
  storage): install the package, sign in, home, a library, a film (direct play) and an episode (subtitles, skip
  intro, next episode), a transcode, music in the background, a Live TV channel, settings (accent, language),
  the licences page. `playfin.log` without errors.

## 3. Publish

1. Commit "build: Playfin x.y.z", tag `x.y.z`, push the tag.
2. GitHub release "Playfin x.y.z" on the tag, with:
   - `IV0001-PLFN00000_00-PLFN000000100000.pkg`;
   - the corresponding source, `dist/playfin-x.y.z-sources.tar`, made by `scripts/ps4/source-archive.sh` (run at
     the release's commit): Playfin, every recipe of `scripts/ps4` with its patches and upstream sources, and the
     build image's FriBidi, zlib, libsamplerate and OpenOrbis toolchain (GPL-3.0: crt and `link.x`) with their
     PacBrew recipes, all checked against pinned checksums;
   - the changelog section as notes.
3. For the first public release: make the repository public, then switch CI on (add `push` and `pull_request` to
   `on:` in `.github/workflows/ci.yml`; it runs on `workflow_dispatch` only until then).

## Dependency packages

`scripts/build-ps4.sh` installs the packages listed in `scripts/ps4/deps.txt` from `build-deps/` when they are
there, else from the `deps` release (`PS4_DEPS_URL` overrides it). To publish packages built locally:

```sh
scripts/ps4/build-deps.sh                      # or only the changed ones: scripts/ps4/build-deps.sh libass
gh release upload deps build-deps/ps4-openorbis-<name>-<version>-<pkgrel>-any.pkg.tar.gz -R LowganDev/PlayfinPS4
```

The release is a pre-release on the tag `deps`; it was created once with
`gh release create deps --prerelease --title "PS4 dependencies" --notes "..." -R LowganDev/PlayfinPS4`.
