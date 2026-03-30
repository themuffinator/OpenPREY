param(
    [string]$GameLibsRepo = "",
    [string]$BuildDir = "",
    [switch]$SetupOnly,
    [switch]$SkipStage
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Add-UniqueString {
    param(
        [System.Collections.ArrayList]$List,
        [string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return
    }

    $normalized = [System.IO.Path]::GetFullPath($Value)
    if (-not ($List -contains $normalized)) {
        [void]$List.Add($normalized)
    }
}

function Get-CommandPath {
    param([string]$Name)

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        return ""
    }

    return $cmd.Source
}

function Invoke-MesonGameLibsBuild {
    param(
        [string]$RepoRoot,
        [string]$BuildOutputDir,
        [switch]$SetupOnlyMode
    )

    $mesonSetup = Join-Path $RepoRoot "tools\build\meson_setup.ps1"
    $coreData = Join-Path $BuildOutputDir "meson-private\coredata.dat"
    $buildNinja = Join-Path $BuildOutputDir "build.ninja"

    Write-Host "Detected Meson wrapper in OpenPrey-GameLibs."
    Write-Host "  Repo: $RepoRoot"
    Write-Host "  BuildDir: $BuildOutputDir"

    if ((Test-Path $coreData) -and (Test-Path $buildNinja)) {
        & $mesonSetup setup --reconfigure $BuildOutputDir $RepoRoot
    } else {
        & $mesonSetup setup --wipe $BuildOutputDir $RepoRoot --backend ninja --buildtype release --vsenv
    }
    $setupExit = [int]$LASTEXITCODE
    if ($setupExit -ne 0) {
        exit $setupExit
    }

    if ($SetupOnlyMode) {
        return
    }

    & $mesonSetup compile -C $BuildOutputDir
    $compileExit = [int]$LASTEXITCODE
    if ($compileExit -ne 0) {
        exit $compileExit
    }
}

function Invoke-LegacyGameLibsBuild {
    param(
        [string]$RepoRoot,
        [switch]$SetupOnlyMode
    )

    $legacySolution = Join-Path $RepoRoot "src\PREY.sln"
    if (-not (Test-Path $legacySolution)) {
        throw "OpenPrey-GameLibs has no Meson wrapper and no legacy PREY.sln at '$legacySolution'."
    }

    Write-Host "Detected legacy OpenPrey-GameLibs project layout (VC solution)."
    Write-Host "  Solution: $legacySolution"

    if ($SetupOnlyMode) {
        Write-Host "Setup-only mode requested; legacy build setup complete."
        return
    }

    $devenv = Get-CommandPath -Name "devenv.com"
    if (-not [string]::IsNullOrWhiteSpace($devenv)) {
        & $devenv $legacySolution /Build "Release|Win32" /Project "Game"
        $legacyExit = [int]$LASTEXITCODE
        if ($legacyExit -ne 0) {
            exit $legacyExit
        }
        return
    }

    $msbuild = Get-CommandPath -Name "msbuild"
    if (-not [string]::IsNullOrWhiteSpace($msbuild)) {
        & $msbuild $legacySolution "/t:Game" "/p:Configuration=Release;Platform=Win32" "/m"
        $legacyExit = [int]$LASTEXITCODE
        if ($legacyExit -ne 0) {
            exit $legacyExit
        }
        return
    }

    throw "Legacy OpenPrey-GameLibs build requires either devenv.com or msbuild in PATH."
}

function Get-DiscoveredGameModules {
    param(
        [string]$RepoRoot,
        [string]$BuildOutputDir
    )

    $results = New-Object System.Collections.ArrayList
    $explicitCandidates = @(
        (Join-Path $BuildOutputDir "basepy\game_x64.dll"),
        (Join-Path $BuildOutputDir "basepy\game_x86.dll"),
        (Join-Path $BuildOutputDir "basepy\game_arm64.dll"),
        (Join-Path $BuildOutputDir "gamex86.dll"),
        (Join-Path $BuildOutputDir "gamex64.dll"),
        (Join-Path $RepoRoot "src\ReleaseDLL\gamex86.dll"),
        (Join-Path $RepoRoot "src\DebugDLL\gamex86.dll"),
        (Join-Path $RepoRoot "src\Game\ReleaseDLL\gamex86.dll"),
        (Join-Path $RepoRoot "src\Game\DebugDLL\gamex86.dll")
    )

    foreach ($candidate in $explicitCandidates) {
        if (Test-Path $candidate) {
            Add-UniqueString -List $results -Value $candidate
        }
    }

    $scanRoots = @($BuildOutputDir, (Join-Path $RepoRoot "src\ReleaseDLL"), (Join-Path $RepoRoot "src\Game\ReleaseDLL"))
    foreach ($root in $scanRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        Get-ChildItem -Path $root -Recurse -File -Filter "game*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
            $name = $_.Name.ToLowerInvariant()
            if ($name.StartsWith("game-sp_") -or $name.StartsWith("game-mp_")) {
                return
            }
            Add-UniqueString -List $results -Value $_.FullName
        }
    }

    return $results
}

function Stage-GameLibModules {
    param(
        [System.Collections.ArrayList]$ModulePaths,
        [string]$OpenPreyRoot
    )

    if ($ModulePaths.Count -eq 0) {
        Write-Host "No companion game module binaries were discovered to stage."
        return
    }

    $stageDirs = @(
        (Join-Path $OpenPreyRoot "builddir\basepy"),
        (Join-Path $OpenPreyRoot ".install\basepy")
    )

    foreach ($stageDir in $stageDirs) {
        if (-not (Test-Path $stageDir)) {
            New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
        }
    }

    foreach ($modulePath in $ModulePaths) {
        $moduleName = [System.IO.Path]::GetFileName($modulePath)
        foreach ($stageDir in $stageDirs) {
            $targetPath = Join-Path $stageDir $moduleName
            Copy-Item -Path $modulePath -Destination $targetPath -Force
            Write-Host "Staged: $targetPath"

            if ($moduleName -ieq "gamex86.dll") {
                $aliasPath = Join-Path $stageDir "game_x86.dll"
                Copy-Item -Path $modulePath -Destination $aliasPath -Force
                Write-Host "Staged alias: $aliasPath"
            } elseif ($moduleName -ieq "gamex64.dll") {
                $aliasPath = Join-Path $stageDir "game_x64.dll"
                Copy-Item -Path $modulePath -Destination $aliasPath -Force
                Write-Host "Staged alias: $aliasPath"
            }
        }
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$openPreyRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))

$gameLibsRepoOverride = ""
if (-not [string]::IsNullOrWhiteSpace($env:OPENPREY_GAMELIBS_REPO)) {
    $gameLibsRepoOverride = $env:OPENPREY_GAMELIBS_REPO
} elseif (-not [string]::IsNullOrWhiteSpace($env:OPENQ4_GAMELIBS_REPO)) {
    $gameLibsRepoOverride = $env:OPENQ4_GAMELIBS_REPO
}

if ([string]::IsNullOrWhiteSpace($GameLibsRepo)) {
    if (-not [string]::IsNullOrWhiteSpace($gameLibsRepoOverride)) {
        $GameLibsRepo = $gameLibsRepoOverride
    } else {
        $GameLibsRepo = Join-Path $openPreyRoot "..\OpenPrey-GameLibs"
    }
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $GameLibsRepo "builddir"
}

$gameLibsRoot = [System.IO.Path]::GetFullPath($GameLibsRepo)
$gameLibsBuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$gameLibsMesonSetup = Join-Path $gameLibsRoot "tools\build\meson_setup.ps1"

if (-not (Test-Path $gameLibsRoot)) {
    throw "OpenPrey-GameLibs repository not found at '$gameLibsRoot'. Set OPENPREY_GAMELIBS_REPO (or legacy OPENQ4_GAMELIBS_REPO) or pass -GameLibsRepo."
}

Write-Host "Building OpenPrey game libraries from:"
Write-Host "  Repo: $gameLibsRoot"
Write-Host "  BuildDir: $gameLibsBuildDir"

if (Test-Path $gameLibsMesonSetup) {
    Invoke-MesonGameLibsBuild -RepoRoot $gameLibsRoot -BuildOutputDir $gameLibsBuildDir -SetupOnlyMode:$SetupOnly
} else {
    Invoke-LegacyGameLibsBuild -RepoRoot $gameLibsRoot -SetupOnlyMode:$SetupOnly
}

if (-not $SetupOnly -and -not $SkipStage) {
    $modules = Get-DiscoveredGameModules -RepoRoot $gameLibsRoot -BuildOutputDir $gameLibsBuildDir
    Stage-GameLibModules -ModulePaths $modules -OpenPreyRoot $openPreyRoot
}

Write-Host "OpenPrey-GameLibs build complete."
