# Fast Explorer

Fast Explorer is a native Windows file explorer focused on responsive
large-folder navigation. It supports tabs, multi-pane layouts, filtering,
grouping, shell file operations, session restore, dark/light theme toggle,
and high-volume enumeration benchmarks.

## Download

Get the latest release from the
[Releases page](https://github.com/solitasroh/fast-explorer/releases).

1. Download `FastExplorer-<version>-win64.zip`.
2. Extract the archive anywhere.
3. Run `FastExplorer.exe`.

**System requirements**: Windows 10 or Windows 11, x64.

## Highlights

- Owner-data list view for large folders, with background enumeration.
- Stable selection across sorting, filtering, refresh, and tab switches.
- Tabs per pane and up to four-pane layouts.
- Filter popup via `Ctrl+F`; ESC or explicit clear removes the filter.
- Grouping by name, modified date, or type.
- Shell-backed delete, rename, create folder, copy, cut, paste, context menu,
  and drag/drop for normal filesystem locations.
- `shell:ThisPC`, `shell:Network`, `shell:NetHood`, generic `shell:*` /
  `::{CLSID}` Shell namespace locations, UNC shares, mapped drives, and
  Network Shortcuts support where Windows exposes resolvable targets.
- Session restore for paths, shell locations, tabs, panes, layout, ratios,
  view toggles, theme mode, and window placement.
- WinSparkle auto-update with Ed25519 update signature verification.

See [docs/usage-guide.md](docs/usage-guide.md) for shortcuts, shell/network
behavior, benchmark commands, and troubleshooting.

## Building From Source

Requirements:

- Visual Studio 2022 or newer with **Desktop development with C++**.
- Windows 10/11 SDK.
- CMake 3.24 or newer.
- Ninja, or another CMake-supported MSVC generator.

Open a Visual Studio developer environment before configuring/building. Common
paths are:

```pwsh
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake -B build -S . && cmake --build build'
```

If your install is Community or BuildTools, replace `Professional` in the path.
When a specific SDK is needed locally, add for example
`-winsdk=10.0.22621.0` to the `VsDevCmd.bat` call.

## Tests And Benchmarks

```pwsh
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo && cmake --build build'
ctest --test-dir build --output-on-failure
build\core-tests.exe
build\winui_lite_tests.exe
```

Benchmark smoke:

```pwsh
build\FastExplorerBench.exe generate --preset medium --out %TEMP%\fe-medium
build\FastExplorerBench.exe enumerate --path %TEMP%\fe-medium --runs 5
build\FastExplorerBench.exe head-to-head --path %TEMP%\fe-medium --runs 5
```

Use `large-flat` for the 100k-file performance gate.

## Release Process

See [docs/RELEASING.md](docs/RELEASING.md). Do not create tags, releases, or
packages from routine development work unless that release is explicitly
requested.

## License

See individual file headers. WinSparkle is bundled under the MIT license
(see [WinSparkle repository](https://github.com/vslavik/winsparkle)).
