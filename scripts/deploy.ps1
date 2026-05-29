<#
.SYNOPSIS
    Build skygraph and produce MO2-ready release artifacts.

.DESCRIPTION
    1. Configures + builds both the SKSE plugin DLL and the standalone viewer
       through the configured CMake preset.
    2. Stages the plugin into the SKSE/Plugins/* layout MO2 expects, then
       packs it as a .rar archive.
    3. Stages the viewer and packs it as a .zip archive.

    The plugin archive is structured so the user can drop the .rar onto MO2's
    left pane and get a single-click install with no manual reshuffling.

.PARAMETER Preset
    CMakePresets.json preset name. Defaults to 'msvc-x64-release'.

.PARAMETER Config
    CMake build configuration (Release / Debug). Defaults to 'Release'.

.PARAMETER Version
    Override the version stamp in archive filenames. If unset, parsed from
    project(... VERSION ...) in the top-level CMakeLists.txt.

.PARAMETER OutDir
    Where to drop the final archives. Defaults to <repo>/dist.

.PARAMETER Clean
    Wipe the build directory before configuring.

.PARAMETER SkipBuild
    Skip the configure+build phase; assume artifacts are already in build/<preset>/bin/<Config>/.

.PARAMETER RarExe
    Explicit path to rar.exe (WinRAR). If unset, the script probes the PATH and
    standard install locations.

.EXAMPLE
    .\scripts\deploy.ps1
    Full clean release build, archives land in dist/.

.EXAMPLE
    .\scripts\deploy.ps1 -SkipBuild
    Re-pack the existing build outputs without recompiling.

.EXAMPLE
    .\scripts\deploy.ps1 -Version 0.2.0-rc1 -OutDir C:\releases
    Custom version stamp + custom output dir.
#>
[CmdletBinding()]
param(
    [string]$Preset = 'msvc-x64-release',
    [string]$Config = 'Release',
    [string]$Version,
    [string]$OutDir,
    [string]$RarExe,
    [string]$VsInstallPath,
    [string]$VsArch = 'amd64',
    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$SkipDevEnv,
    [switch]$ProbeOnly
)

$ErrorActionPreference = 'Stop'
$InformationPreference = 'Continue'

# --- Resolve paths --------------------------------------------------------

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $RepoRoot
try {

if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot 'dist'
}

$BuildDir = Join-Path $RepoRoot "build/$Preset"
$BinDir   = Join-Path $BuildDir "bin/$Config"
$Stage    = Join-Path $RepoRoot 'build/deploy-stage'

# --- Version detection ----------------------------------------------------

if (-not $Version) {
    $cmakeText = Get-Content (Join-Path $RepoRoot 'CMakeLists.txt') -Raw
    if ($cmakeText -match 'project\s*\(\s*skygraph\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $Matches[1]
    } else {
        Write-Warning "Could not parse version from CMakeLists.txt; defaulting to 0.0.0"
        $Version = '0.0.0'
    }
}
Write-Information "==> skygraph deploy v$Version (preset=$Preset config=$Config)"

# --- Locate rar.exe (or WinRAR.exe as a fallback) -----------------------

function Find-WinRarExe {
    # Returns @{ Exe = <path|$null>; Probed = <string[]> }.
    # Prefers rar.exe (clean CLI) over WinRAR.exe (GUI, but accepts the same
    # archive ops). Searches PATH, standard install dirs, and the registry,
    # so the script works regardless of where WinRAR was installed.
    $probed   = New-Object System.Collections.Generic.List[string]
    $exeNames = @('rar.exe', 'WinRAR.exe')

    function Test-RarCandidate($dir) {
        if (-not $dir -or -not (Test-Path $dir -PathType Container)) { return $null }
        foreach ($n in $exeNames) {
            $p = Join-Path $dir $n
            $probed.Add($p) | Out-Null
            if (Test-Path $p -PathType Leaf) { return $p }
        }
        return $null
    }

    # 1. PATH lookup
    foreach ($n in $exeNames) {
        $hit = Get-Command -Name $n -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) {
            $probed.Add("PATH:$n -> $($hit.Source)") | Out-Null
            if (Test-Path $hit.Source) {
                return @{ Exe = $hit.Source; Probed = $probed }
            }
        } else {
            $probed.Add("PATH:$n (not found)") | Out-Null
        }
    }

    # 2. Standard install dirs (covers x64, x86, and the ProgramW6432 mirror)
    $stdDirs = @(
        $env:ProgramFiles,
        ${env:ProgramFiles(x86)},
        $env:ProgramW6432,
        'C:\Program Files',
        'C:\Program Files (x86)',
        'D:\Program Files',
        'D:\Program Files (x86)'
    ) | Where-Object { $_ } | Sort-Object -Unique
    foreach ($base in $stdDirs) {
        $hit = Test-RarCandidate (Join-Path $base 'WinRAR')
        if ($hit) { return @{ Exe = $hit; Probed = $probed } }
    }

    # 3. Registry: HKLM/HKCU \ SOFTWARE \ WinRAR (and WOW6432Node mirror).
    #    WinRAR stores its install dir under the (Default) string value.
    $regKeys = @(
        'HKLM:\SOFTWARE\WinRAR',
        'HKLM:\SOFTWARE\WOW6432Node\WinRAR',
        'HKCU:\SOFTWARE\WinRAR'
    )
    foreach ($k in $regKeys) {
        if (Test-Path $k) {
            $vals = Get-ItemProperty -Path $k -ErrorAction SilentlyContinue
            # Try common property names plus the default value.
            $candidates = @(
                ($vals.'(default)'),
                ($vals.exe64),
                ($vals.exe32),
                ($vals.Path)
            ) | Where-Object { $_ }
            foreach ($c in $candidates) {
                $probed.Add("$k -> $c") | Out-Null
                $dir = if (Test-Path $c -PathType Container) { $c } else { Split-Path -Parent $c }
                $hit = Test-RarCandidate $dir
                if ($hit) { return @{ Exe = $hit; Probed = $probed } }
            }
        } else {
            $probed.Add("$k (key absent)") | Out-Null
        }
    }

    # 4. App Paths -- Windows' canonical "where is this app?" lookup
    $appPaths = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\WinRAR.exe',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\WinRAR.exe'
    )
    foreach ($k in $appPaths) {
        if (Test-Path $k) {
            $v = (Get-ItemProperty -Path $k -ErrorAction SilentlyContinue).'(default)'
            if ($v) {
                $probed.Add("$k -> $v") | Out-Null
                $hit = Test-RarCandidate (Split-Path -Parent $v)
                if ($hit) { return @{ Exe = $hit; Probed = $probed } }
            }
        } else {
            $probed.Add("$k (key absent)") | Out-Null
        }
    }

    # 5. Uninstall key -- last resort, but works when the user did a custom install
    $uninstKeys = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\WinRAR archiver',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\WinRAR archiver'
    )
    foreach ($k in $uninstKeys) {
        if (Test-Path $k) {
            $loc = (Get-ItemProperty -Path $k -ErrorAction SilentlyContinue).InstallLocation
            if ($loc) {
                $probed.Add("$k -> $loc") | Out-Null
                $hit = Test-RarCandidate $loc
                if ($hit) { return @{ Exe = $hit; Probed = $probed } }
            }
        } else {
            $probed.Add("$k (key absent)") | Out-Null
        }
    }

    return @{ Exe = $null; Probed = $probed }
}

if (-not $RarExe) {
    $rarSearch = Find-WinRarExe
    $RarExe = $rarSearch.Exe
    if ($ProbeOnly) {
        Write-Host "=== WinRAR probe trace ==="
        $rarSearch.Probed | ForEach-Object { Write-Host "  $_" }
        Write-Host ""
    }
    if (-not $RarExe -and -not $ProbeOnly) {
        $tried = ($rarSearch.Probed | ForEach-Object { "    $_" }) -join "`n"
        throw @"
Could not locate rar.exe or WinRAR.exe. Probed:
$tried

Pass an explicit path with -RarExe 'C:\Path\To\rar.exe', or install WinRAR
from https://www.win-rar.com/.

If you'd prefer a .zip or .7z plugin archive, use the 'skygraph_package'
CMake target documented in docs/packaging.md.
"@
    }
}
$IsWinRarGui = $false
if ($RarExe) {
    if (-not (Test-Path $RarExe)) {
        throw "-RarExe was '$RarExe' but the file does not exist."
    }
    $RarLeaf = (Split-Path -Leaf $RarExe).ToLower()
    $IsWinRarGui = ($RarLeaf -eq 'winrar.exe')
    Write-Information "    rar binary: $RarExe$(if ($IsWinRarGui) { '  (GUI fallback)' })"
} elseif (-not $ProbeOnly) {
    throw "rar binary not located (should have been caught earlier)."
}

# --- Initialize VS Developer environment --------------------------------

function Initialize-VsDevEnv {
    param([string]$InstallPath, [string]$Arch = 'amd64')
    # Returns @{ Initialized = $bool; Source = <string>; Probed = <string[]> }.
    $probed = New-Object System.Collections.Generic.List[string]

    # Already in a dev shell? VSCMD_VER is set by VsDevCmd.bat.
    if ($env:VSCMD_VER) {
        $probed.Add("VSCMD_VER=$($env:VSCMD_VER) -- already inside a dev shell") | Out-Null
        return @{ Initialized = $true; Source = 'inherited'; Probed = $probed }
    }

    if (-not $InstallPath) {
        # The double-@( ) here forces array context; PowerShell would otherwise
        # unwrap a single-element Where-Object result into a bare string and
        # then $cands[0] would index the *string*, returning the first char.
        $vswhereCandidates = @(@(
            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
            "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
        ) | Where-Object { $_ -and (Test-Path $_) })
        if ($vswhereCandidates.Count -eq 0) {
            $probed.Add('vswhere.exe not found (no Visual Studio Installer present)') | Out-Null
            return @{ Initialized = $false; Source = $null; Probed = $probed }
        }
        $vswhere = [string]$vswhereCandidates[0]
        $probed.Add("vswhere: $vswhere") | Out-Null

        $vsHits = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $vsHits) {
            $probed.Add('vswhere returned no VS install with the C++ x64/x86 workload') | Out-Null
            return @{ Initialized = $false; Source = $null; Probed = $probed }
        }
        $InstallPath = ([string](@($vsHits)[0])).Trim()
    }
    $probed.Add("VS install: $InstallPath") | Out-Null

    $launcher = Join-Path $InstallPath 'Common7\Tools\Launch-VsDevShell.ps1'
    $probed.Add("launcher: $launcher") | Out-Null
    if (-not (Test-Path $launcher)) {
        return @{ Initialized = $false; Source = $null; Probed = $probed }
    }

    # The launcher edits $env:* in the current process, so cmake/ninja/cl
    # will be on PATH for the rest of this script.
    & $launcher -Arch $Arch -HostArch $Arch -SkipAutomaticLocation | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $probed.Add("launcher exited with code $LASTEXITCODE") | Out-Null
        return @{ Initialized = $false; Source = $null; Probed = $probed }
    }
    if (-not $env:VSCMD_VER) {
        $probed.Add('launcher ran but VSCMD_VER is still unset') | Out-Null
        return @{ Initialized = $false; Source = $null; Probed = $probed }
    }
    return @{ Initialized = $true; Source = $launcher; Probed = $probed }
}

if ($SkipDevEnv) {
    Write-Information "==> Skipping VS dev env initialization (-SkipDevEnv)"
} else {
    Write-Information "==> Initializing VS Developer environment..."
    $devEnv = Initialize-VsDevEnv -InstallPath $VsInstallPath -Arch $VsArch
    foreach ($p in $devEnv.Probed) {
        Write-Information "    $p"
    }
    if (-not $devEnv.Initialized) {
        if ($ProbeOnly) {
            Write-Host "VS Dev Env: NOT initialized" -ForegroundColor Yellow
        } else {
            throw @"
Could not initialize the Visual Studio Developer environment. The build
needs cmake / ninja / cl.exe on PATH and the matching MSVC INCLUDE / LIB
env vars set.

Fixes:
  - Install the 'Desktop development with C++' workload in Visual Studio
    Installer (it bundles cmake + ninja).
  - Or run this script from a 'Developer PowerShell for VS 2022' window
    and pass -SkipDevEnv to bypass the auto-initialization.
  - Or pass -VsInstallPath 'C:\Program Files\Microsoft Visual Studio\2022\Community'
    if your VS install lives somewhere unusual.
"@
        }
    } else {
        Write-Information "    -> ready ($($devEnv.Source))"
    }
}

# Sanity-check the tools the build needs.
$missingTools = @()
foreach ($t in 'cmake', 'ninja') {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) {
        $missingTools += $t
    }
}
if ($missingTools.Count -gt 0 -and -not $ProbeOnly) {
    throw "Required tools missing from PATH after dev-env init: $($missingTools -join ', ')"
}

if ($ProbeOnly) {
    Write-Host ""
    if ($RarExe) {
        Write-Host ("FOUND rar  : {0}" -f $RarExe) -ForegroundColor Green
    } else {
        Write-Host  "MISSING rar"                  -ForegroundColor Red
    }
    if ($env:VSCMD_VER) {
        Write-Host ("FOUND vsdev: VSCMD_VER={0}" -f $env:VSCMD_VER) -ForegroundColor Green
    }
    foreach ($t in 'cmake', 'ninja', 'cl') {
        $hit = Get-Command $t -ErrorAction SilentlyContinue
        if ($hit) {
            Write-Host ("FOUND {0,-5}: {1}" -f $t, $hit.Source) -ForegroundColor Green
        } else {
            Write-Host ("MISSING {0}" -f $t) -ForegroundColor Yellow
        }
    }
    if (-not $RarExe -or $missingTools.Count -gt 0) { exit 1 }
    exit 0
}

# --- Build ----------------------------------------------------------------

if ($SkipBuild) {
    Write-Information "==> Skipping build (-SkipBuild)"
} else {
    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Information "==> Cleaning $BuildDir"
        Remove-Item -Recurse -Force $BuildDir
    }

    # Self-heal a half-populated FetchContent state. A failed `git clone`
    # (e.g. bad GIT_TAG) leaves the subbuild dir behind with a stamp file
    # but no source checkout, so subsequent configures skip the download
    # and immediately fail again. Detect that and wipe only the affected
    # subtree, preserving the rest of the (potentially slow) build state.
    $depsDir = Join-Path $BuildDir '_deps'
    if (Test-Path $depsDir) {
        Get-ChildItem -Path $depsDir -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like '*-src' } |
            ForEach-Object {
                $srcDir = $_.FullName
                $cml = Join-Path $srcDir 'CMakeLists.txt'
                if (-not (Test-Path $cml)) {
                    $base = ($_.Name -replace '-src$', '')
                    Write-Information "==> Healing stale FetchContent state for '$base'"
                    foreach ($suffix in '-src', '-build', '-subbuild') {
                        $stale = Join-Path $depsDir ($base + $suffix)
                        if (Test-Path $stale) {
                            Remove-Item -Recurse -Force $stale -ErrorAction SilentlyContinue
                        }
                    }
                }
            }
    }

    Write-Information "==> cmake --preset $Preset"
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }

    Write-Information "==> cmake --build --preset $Preset --config $Config"
    & cmake --build --preset $Preset --config $Config
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }
}

# --- Verify artifacts -----------------------------------------------------

function Find-Artifact([string]$name, [string[]]$probePaths) {
    foreach ($p in $probePaths) {
        if (Test-Path $p) { return (Resolve-Path $p).Path }
    }
    # Fall back to a recursive search under the build dir.
    $hit = Get-ChildItem -Path $BuildDir -Filter $name -Recurse -ErrorAction SilentlyContinue |
           Where-Object { -not $_.PSIsContainer } |
           Sort-Object LastWriteTime -Descending |
           Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

# Canonical locations first (CMAKE_RUNTIME_OUTPUT_DIRECTORY set in top-level
# CMakeLists), then per-target subdirs that add_commonlibsse_plugin may use.
$pluginDll = Find-Artifact 'skygraph.dll' @(
    (Join-Path $BinDir 'skygraph.dll'),
    (Join-Path $BuildDir "plugin/$Config/skygraph.dll"),
    (Join-Path $BuildDir 'plugin/skygraph.dll')
)
$viewerExe = Find-Artifact 'skygraph.exe' @(
    (Join-Path $BinDir 'skygraph.exe'),
    (Join-Path $BuildDir "viewer/$Config/skygraph.exe"),
    (Join-Path $BuildDir 'viewer/skygraph.exe')
)
$pluginCfg = Join-Path $RepoRoot 'plugin/resources/skygraph.json'
$viewerIni = Join-Path $RepoRoot 'viewer/resources/imgui.ini.default'

if (-not $pluginDll) { throw "skygraph.dll not found under $BuildDir" }
if (-not $viewerExe) { throw "skygraph.exe not found under $BuildDir" }
if (-not (Test-Path $pluginCfg)) { throw "missing $pluginCfg" }
if (-not (Test-Path $viewerIni)) { throw "missing $viewerIni" }

Write-Information "    plugin DLL: $pluginDll"
Write-Information "    viewer EXE: $viewerExe"

# --- Stage ----------------------------------------------------------------

# A skygraph.exe launched FROM the stage dir (build/deploy-stage/viewer)
# holds an exclusive lock on its own image and log file, so Windows refuses
# to delete or overwrite it -- which would make Remove-Item below blow up and
# leave the stage half-cleaned. Proactively stop any such instance. We only
# target processes whose image actually lives under $Stage, so a viewer the
# user runs from dist/ or elsewhere is left untouched.
$stageFull = [System.IO.Path]::GetFullPath($Stage)
$staleViewers = @(Get-Process -Name skygraph -ErrorAction SilentlyContinue |
    Where-Object {
        try { $_.Path -and $_.Path.StartsWith($stageFull, [System.StringComparison]::OrdinalIgnoreCase) }
        catch { $false }
    })
foreach ($proc in $staleViewers) {
    Write-Information "==> Stopping staged viewer still running: $($proc.Path) (PID $($proc.Id))"
    try {
        $proc | Stop-Process -Force -ErrorAction Stop
        $proc.WaitForExit(5000) | Out-Null
    } catch {
        Write-Warning "Could not stop PID $($proc.Id): $($_.Exception.Message)"
    }
}

Write-Information "==> Staging at $Stage"
if (Test-Path $Stage) {
    try {
        Remove-Item -Recurse -Force $Stage -ErrorAction Stop
    } catch [System.IO.IOException] {
        # We already tried to stop staged viewers above, so reaching here means
        # something else (another editor, antivirus scan, a viewer copied out
        # of the stage but still cwd'd into it) holds a handle. Re-throw with a
        # clearer hint.
        throw @"
Failed to clean stage dir '$Stage':
  $($_.Exception.Message)

A file under build/deploy-stage is still in use (commonly a running
skygraph.exe or an open log file). Close it and re-run. The build
succeeded, so '-SkipBuild' will let you skip recompilation.
"@
    }
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

# Plugin tree: <stage>/plugin/SKSE/Plugins/{skygraph.dll, skygraph.json}
$pluginStage    = Join-Path $Stage 'plugin'
$pluginSkseDir  = Join-Path $pluginStage 'SKSE/Plugins'
New-Item -ItemType Directory -Force -Path $pluginSkseDir | Out-Null
Copy-Item $pluginDll  -Destination $pluginSkseDir -Force
Copy-Item $pluginCfg  -Destination $pluginSkseDir -Force

# Ship debug symbols alongside the DLL if present (MO2 ignores .pdb but it
# helps when crashes get reported back).
$pluginPdb = Join-Path (Split-Path -Parent $pluginDll) 'skygraph.pdb'
if (Test-Path $pluginPdb) {
    Copy-Item $pluginPdb -Destination $pluginSkseDir -Force
}

# Viewer tree: <stage>/viewer/{skygraph.exe, imgui.ini.default}
$viewerStage = Join-Path $Stage 'viewer'
New-Item -ItemType Directory -Force -Path $viewerStage | Out-Null
Copy-Item $viewerExe -Destination $viewerStage -Force
Copy-Item $viewerIni -Destination $viewerStage -Force

# --- Pack -----------------------------------------------------------------

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$pluginRar = Join-Path $OutDir "skygraph-plugin-v$Version.rar"
$viewerZip = Join-Path $OutDir "skygraph-viewer-v$Version.zip"

if (Test-Path $pluginRar) { Remove-Item $pluginRar -Force }
if (Test-Path $viewerZip) { Remove-Item $viewerZip -Force }

# rar args:
#   a   = add
#   -r  = recurse
#   -m5 = max compression
#   -ep1= strip the first path component so 'SKSE' is the archive root
# We cd into $pluginStage first so the archive contains:
#   SKSE/Plugins/skygraph.dll
#   SKSE/Plugins/skygraph.json
# which is exactly what MO2 expects for a one-click install.
Write-Information "==> Packing plugin -> $pluginRar"
# Quiet-mode flag differs between rar.exe (CLI) and WinRAR.exe (GUI).
$quietFlag = if ($IsWinRarGui) { '-ibck' } else { '-idq' }
Push-Location $pluginStage
try {
    & $RarExe a -r -m5 $quietFlag $pluginRar 'SKSE'
    if ($LASTEXITCODE -ne 0) { throw "rar failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

Write-Information "==> Packing viewer -> $viewerZip"
$sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($sevenZip) {
    Push-Location $viewerStage
    try {
        & $sevenZip.Source a -tzip -mx=9 -bso0 -bsp0 $viewerZip '*'
        if ($LASTEXITCODE -ne 0) { throw "7z failed (exit $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
} else {
    # Fallback to native PowerShell zip (slower compression but no extra deps).
    Compress-Archive -Path (Join-Path $viewerStage '*') `
                     -DestinationPath $viewerZip `
                     -CompressionLevel Optimal
}

# --- Summary --------------------------------------------------------------

function Format-FileSize([long]$bytes) {
    $kib = $bytes / 1024.0
    if ($kib -lt 1024) { return ('{0,7:N1} KiB' -f $kib) }
    return ('{0,7:N2} MiB' -f ($kib / 1024.0))
}

Write-Information ""
Write-Information "==> Done."
foreach ($f in @($pluginRar, $viewerZip)) {
    $sz = (Get-Item $f).Length
    Write-Information ("    {0,-60} {1}" -f $f, (Format-FileSize $sz))
}
Write-Information ""
Write-Information "Plugin install:  drop $pluginRar onto Mod Organizer 2."
Write-Information "Viewer install:  extract $viewerZip anywhere, run skygraph.exe."

} finally {
    Pop-Location
}
