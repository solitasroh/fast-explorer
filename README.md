# Fast Explorer

A keyboard-driven dual-pane file explorer for Windows.

## Download

Get the latest release from the [Releases page](https://github.com/solitasroh/fast-explorer/releases).

1. Download `FastExplorer-<version>-win64.zip`.
2. Extract the archive anywhere (the app is portable — no installer).
3. Run `FastExplorer.exe`.

**System requirements**: Windows 10 or Windows 11, x64.

> The first time you launch the app, Windows SmartScreen may warn because the
> binary is not (yet) signed with an EV certificate. Click **More info →
> Run anyway**. Auto-update payloads are verified independently via Ed25519
> signatures, so even without OS-level signing, tampered updates are rejected.

## Auto-update

Fast Explorer checks for updates once per day on startup, using
[WinSparkle](https://winsparkle.org/). When a new release is published,
the app shows a small dialog letting you install it immediately or skip
the version. Update payloads are signed with an Ed25519 keypair held by
the project maintainer; signatures are verified inside the app before the
update is applied.

Auto-update can be disabled per-user — uncheck the option in the
"Check for Updates" prompt, or delete the registry value at
`HKCU\Software\Fast Explorer Project\Fast Explorer\WinSparkle\CheckForUpdates`.

## Building from source

Requirements: Visual Studio 2022 or newer (with the **Desktop development
with C++** workload), CMake ≥ 3.24, Ninja.

```pwsh
cmake --preset msvc-x64-release
cmake --build --preset release
```

The Release exe lands in `build/FastExplorer.exe` together with
`WinSparkle.dll` (auto-staged by CMake).

Run the unit tests with:

```pwsh
cd build
ctest --output-on-failure
```

## Release process

See [docs/RELEASING.md](docs/RELEASING.md) for the full workflow.

Short version:

1. Bump `project(FastExplorer VERSION x.y.z)` in [CMakeLists.txt](CMakeLists.txt).
2. Commit, then tag: `git tag vX.Y.Z && git push --tags`.
3. The [Release workflow](.github/workflows/release.yml) builds the ZIP,
   signs it, updates `appcast/appcast.xml`, attaches the ZIP to a new
   GitHub Release, and publishes the appcast to the `gh-pages` branch.

## License

See individual file headers. WinSparkle is bundled under the MIT license
(see [WinSparkle repository](https://github.com/vslavik/winsparkle)).
