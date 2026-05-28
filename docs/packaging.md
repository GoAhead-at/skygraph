# Building the Skygraph release artifacts

Two release artifacts:

- `skygraph-plugin-vX.Y.Z.rar` -- MO2-ready SKSE plugin (default of `scripts/deploy.ps1`)
- `skygraph-viewer-vX.Y.Z.zip` -- standalone viewer

The recommended path is the **deploy script**. Two manual paths (raw CMake, raw 7z) are documented further down for cases where WinRAR isn't available.

## Recommended: `scripts/deploy.ps1`

One PowerShell command does the whole thing:

```powershell
.\scripts\deploy.ps1
```

What it does:

1. Configures + builds via the `msvc-x64-release` CMake preset.
2. Stages the plugin into the SKSE `Plugins/` tree MO2 expects.
3. Packs the plugin as `dist/skygraph-plugin-vX.Y.Z.rar` (using WinRAR's `rar.exe`).
4. Packs the viewer as `dist/skygraph-viewer-vX.Y.Z.zip` (7z if available, else PowerShell `Compress-Archive`).

The resulting plugin `.rar` has exactly:

```
SKSE/
  Plugins/
    skygraph.dll
    skygraph.json
    skygraph.pdb        (if a Release-with-symbols build was produced)
```

so the user can drop it onto Mod Organizer 2 for a one-click install.

### Prerequisites

- The full build prerequisites from the top-level [README.md](../README.md).
- [**WinRAR**](https://www.win-rar.com/) installed, or `rar.exe` on PATH. The script probes `%ProgramFiles%\WinRAR\rar.exe` and `%ProgramFiles(x86)%\WinRAR\rar.exe` automatically.
- (Optional) [7-Zip](https://www.7-zip.org/) on PATH for smaller viewer archives; otherwise the script falls back to PowerShell's built-in zip.

### Useful flags

```powershell
.\scripts\deploy.ps1 -ProbeOnly              # locate WinRAR + VS dev tools, print, exit
.\scripts\deploy.ps1 -SkipBuild              # re-pack the existing build outputs
.\scripts\deploy.ps1 -SkipDevEnv             # already inside a Developer PowerShell
.\scripts\deploy.ps1 -Clean                  # wipe build/msvc-x64-release first
.\scripts\deploy.ps1 -Version 0.2.0-rc1      # override the version stamp in filenames
.\scripts\deploy.ps1 -OutDir C:\releases     # pack to a non-default dist directory
.\scripts\deploy.ps1 -RarExe 'D:\rar\rar.exe'              # explicit WinRAR path
.\scripts\deploy.ps1 -VsInstallPath 'D:\VS\2022\Community' # explicit VS install
```

Pass `-Verbose` for the underlying tool output.

### Visual Studio Developer environment

The script auto-initializes the VS Developer environment (the thing that puts `cmake`, `ninja`, `cl.exe`, and the MSVC `INCLUDE`/`LIB` env vars on PATH). It finds your VS install via `vswhere.exe` and runs `Launch-VsDevShell.ps1`, so you can call `.\scripts\deploy.ps1` from any normal PowerShell -- no need to open a Developer PowerShell first.

If you *are* already in a Developer PowerShell, pass `-SkipDevEnv` to skip the redundant initialization.

### Troubleshooting

`-ProbeOnly` shows everything the script needs:

```powershell
.\scripts\deploy.ps1 -ProbeOnly
```

It prints:

- Every path tried while looking for WinRAR (PATH, standard install dirs, `HKLM\SOFTWARE\WinRAR`, App Paths, Uninstall entry).
- The VS install location (via `vswhere`).
- The resolved `cmake`/`ninja`/`cl` paths after dev-env init.

If a tool is missing or in a non-standard location, the corresponding `-RarExe` / `-VsInstallPath` flag bypasses the search entirely.

## Alternative: CMake `skygraph_package` target

If you don't have WinRAR but do have 7-Zip, the top-level `CMakeLists.txt` exposes a custom target that produces `.7z` (plugin) + `.zip` (viewer):

```powershell
cmake --preset msvc-x64-release
cmake --build --preset msvc-x64-release --target skygraph_package
```

Outputs in `dist/`. Functionally identical to the deploy script except for the plugin archive container.

## Alternative: fully manual

```powershell
cmake --preset msvc-x64-release
cmake --build --preset msvc-x64-release
cmake --install build/msvc-x64-release --prefix build/stage

# Plugin: anything containing SKSE/Plugins/... at the root works for MO2.
7z a -mx=9 dist/skygraph-plugin-v0.1.0.7z .\build\stage\SKSE

# Viewer: loose files in a zip.
7z a -mx=9 dist/skygraph-viewer-v0.1.0.zip .\build\stage\viewer\*
```

## Plugin archive layout

```
skygraph-plugin-vX.Y.Z.7z
└── SKSE/
    └── Plugins/
        ├── skygraph.dll
        └── skygraph.json   (default config; user-editable post-install)
```

This top-level layout is what Mod Organizer 2 expects -- drop the .7z onto MO2 and it installs in one click.

## Viewer archive layout

```
skygraph-viewer-vX.Y.Z.zip
├── skygraph.exe
└── imgui.ini.default   (copied to imgui.ini on first run)
```

## Versioning

The wire protocol version (gated at handshake) lives in
[`protocol/include/skygraph/protocol/version.h`](../protocol/include/skygraph/protocol/version.h).
Bump `kProtocolMajor` only when the change is wire-breaking. Bump
`kProtocolMinor` for additive changes (new record types, new optional fields).

The product version (in archive filenames) lives in the same file as
`kProductVersion*`. Keep them aligned with the top-level `project(... VERSION ...)`
in `CMakeLists.txt`.
