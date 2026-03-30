$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-VsDevCmdPath {
    if ($env:VSINSTALLDIR) {
        $candidate = Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Could not locate vswhere.exe."
    }

    $component = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
    # Prefer Visual Studio 2026+ (major 18) when installed.
    $installPathRaw = & $vswhere -latest -prerelease -version "[18.0,19.0)" -products * -requires $component -property installationPath
    $installPath = if ($null -eq $installPathRaw) { "" } else { "$installPathRaw".Trim() }
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        $installPathRaw = & $vswhere -latest -prerelease -products * -requires $component -property installationPath
        $installPath = if ($null -eq $installPathRaw) { "" } else { "$installPathRaw".Trim() }
        if (-not [string]::IsNullOrWhiteSpace($installPath)) {
            Write-Warning "Visual Studio 2026+ was not found. Falling back to latest available toolchain at '$installPath'."
        }
    }

    if ([string]::IsNullOrWhiteSpace($installPath)) {
        throw "No Visual Studio installation with C++ tools was found."
    }

    $vsDevCmd = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        throw "Could not locate VsDevCmd.bat at '$vsDevCmd'."
    }

    return $vsDevCmd
}

function Quote-CmdArg([string]$Value) {
    if ($Value -match '[\s"&<>|()]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Invoke-Meson {
    param(
        [string[]]$MesonArgs,
        [string]$VsDevCmdPath
    )

    if ([string]::IsNullOrWhiteSpace($VsDevCmdPath)) {
        & meson @MesonArgs
        return
    }

    $mesonCmd = "meson " + (($MesonArgs | ForEach-Object { Quote-CmdArg $_ }) -join " ")
    $fullCmd = 'call "' + $VsDevCmdPath + '" -arch=x64 -host_arch=x64 >nul && ' + $mesonCmd
    & $env:ComSpec /d /c $fullCmd
}

function Get-CompileBuildDirInfo {
    param(
        [string[]]$MesonArgs,
        [string]$DefaultBuildDir
    )

    $result = [PSCustomObject]@{
        BuildDir = $DefaultBuildDir
        HasExplicit = $false
    }

    for ($i = 0; $i -lt $MesonArgs.Length; $i++) {
        $arg = $MesonArgs[$i]
        if ($arg -eq "-C" -and ($i + 1) -lt $MesonArgs.Length) {
            $result.BuildDir = $MesonArgs[$i + 1]
            $result.HasExplicit = $true
            break
        }

        if ($arg.StartsWith("-C") -and $arg.Length -gt 2) {
            $result.BuildDir = $arg.Substring(2)
            $result.HasExplicit = $true
            break
        }
    }

    $result.BuildDir = [System.IO.Path]::GetFullPath($result.BuildDir)
    return $result
}

function Test-MesonBuildDirectory {
    param([string]$BuildDir)

    $coreData = Join-Path $BuildDir "meson-private\coredata.dat"
    $ninjaFile = Join-Path $BuildDir "build.ninja"
    return (Test-Path $coreData) -and (Test-Path $ninjaFile)
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))
$defaultBuildDir = Join-Path $repoRoot "builddir"

$rcWrapper = Join-Path $scriptDir "rc.cmd"
if (-not (Test-Path $rcWrapper)) {
    throw "WINDRES wrapper not found at '$rcWrapper'."
}

$env:WINDRES = $rcWrapper

$vsDevCmd = $null
if ($null -eq (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vsDevCmd = Get-VsDevCmdPath
}

$effectiveArgs = @($args)
if ($effectiveArgs.Count -eq 0) {
    throw "No Meson arguments were provided to meson_setup.ps1."
}

$commandName = $effectiveArgs[0].ToLowerInvariant()
$gameLibsRepo = ""
if (-not [string]::IsNullOrWhiteSpace($env:OPENPREY_GAMELIBS_REPO)) {
    $gameLibsRepo = $env:OPENPREY_GAMELIBS_REPO
} elseif (-not [string]::IsNullOrWhiteSpace($env:OPENQ4_GAMELIBS_REPO)) {
    $gameLibsRepo = $env:OPENQ4_GAMELIBS_REPO
}
$syncGameLibsScript = Join-Path $scriptDir "sync_gamelibs.ps1"
$buildGameLibsScript = Join-Path $scriptDir "build_gamelibs.ps1"

$skipGameLibsSync = ($env:OPENPREY_SKIP_GAMELIBS_SYNC -eq "1") -or ($env:OPENQ4_SKIP_GAMELIBS_SYNC -eq "1")
if (@("setup", "compile", "install").Contains($commandName) -and -not $skipGameLibsSync) {
    if (-not (Test-Path $syncGameLibsScript)) {
        throw "GameLibs sync script not found: '$syncGameLibsScript'."
    }

    $syncArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($gameLibsRepo)) {
        $syncArgs += @("-GameLibsRepo", $gameLibsRepo)
    }

    & $syncGameLibsScript @syncArgs
    $syncExit = [int]$LASTEXITCODE
    if ($syncExit -ne 0) {
        exit $syncExit
    }
}

$buildGameLibs = ($env:OPENPREY_BUILD_GAMELIBS -eq "1") -or ($env:OPENQ4_BUILD_GAMELIBS -eq "1")
$skipGameLibsBuild = ($env:OPENPREY_SKIP_GAMELIBS_BUILD -eq "1") -or ($env:OPENQ4_SKIP_GAMELIBS_BUILD -eq "1")
if ($commandName -eq "compile" -and $buildGameLibs -and -not $skipGameLibsBuild) {
    if (-not (Test-Path $buildGameLibsScript)) {
        throw "GameLibs build script not found: '$buildGameLibsScript'."
    }

    $buildArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($gameLibsRepo)) {
        $buildArgs += @("-GameLibsRepo", $gameLibsRepo)
    }

    & $buildGameLibsScript @buildArgs
    $buildExit = [int]$LASTEXITCODE
    if ($buildExit -ne 0) {
        exit $buildExit
    }
}

if ($effectiveArgs.Length -gt 0 -and ($effectiveArgs[0] -eq "compile" -or $effectiveArgs[0] -eq "install")) {
    $isCompile = $effectiveArgs[0] -eq "compile"
    $isInstall = $effectiveArgs[0] -eq "install"
    $buildInfo = Get-CompileBuildDirInfo -MesonArgs $effectiveArgs -DefaultBuildDir $defaultBuildDir

    if ($isCompile -and -not (Test-MesonBuildDirectory $buildInfo.BuildDir)) {
        Write-Host "Meson build directory '$($buildInfo.BuildDir)' is missing or invalid. Running meson setup..."
        $setupArgs = @(
            "setup",
            "--wipe",
            $buildInfo.BuildDir,
            $repoRoot,
            "--backend",
            "ninja",
            "--buildtype",
            "debug",
            "--wrap-mode=forcefallback"
        )
        Invoke-Meson -MesonArgs $setupArgs -VsDevCmdPath $vsDevCmd
        $setupCode = [int]$LASTEXITCODE
        if ($setupCode -ne 0) {
            exit $setupCode
        }
    }

    if (-not $buildInfo.HasExplicit) {
        $remainingArgs = @()
        if ($effectiveArgs.Length -gt 1) {
            $remainingArgs = $effectiveArgs[1..($effectiveArgs.Length - 1)]
        }
        $effectiveArgs = @($effectiveArgs[0], "-C", $buildInfo.BuildDir) + $remainingArgs
    }

    if ($isInstall -and -not ($effectiveArgs -contains "--skip-subprojects")) {
        $effectiveArgs += "--skip-subprojects"
    }
}

Invoke-Meson -MesonArgs $effectiveArgs -VsDevCmdPath $vsDevCmd
$exitCode = [int]$LASTEXITCODE

if ($exitCode -eq 0 -and $effectiveArgs.Length -gt 0 -and $effectiveArgs[0] -eq "install") {
    $legacyModulePatterns = @(
        "game-sp_*.dll", "game-sp_*.lib", "game-sp_*.pdb", "game-sp_*.exp",
        "game-mp_*.dll", "game-mp_*.lib", "game-mp_*.pdb", "game-mp_*.exp",
        "game_sp_*.dll", "game_sp_*.lib", "game_sp_*.pdb", "game_sp_*.exp",
        "game_mp_*.dll", "game_mp_*.lib", "game_mp_*.pdb", "game_mp_*.exp"
    )
    $cleanupDirs = @(
        (Join-Path $repoRoot ".install\basepy"),
        (Join-Path $repoRoot "builddir\basepy")
    )
    foreach ($cleanupDir in $cleanupDirs) {
        if (-not (Test-Path $cleanupDir)) {
            continue
        }
        foreach ($pattern in $legacyModulePatterns) {
            Get-ChildItem -Path $cleanupDir -Filter $pattern -File -ErrorAction SilentlyContinue | ForEach-Object {
                Remove-Item -Path $_.FullName -Force -ErrorAction SilentlyContinue
                Write-Host "Removed legacy split module artifact: $($_.FullName)"
            }
        }
    }
}

exit $exitCode
