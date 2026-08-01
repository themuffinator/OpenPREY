param(
    [string]$GameLibsRepo = "",
    [string]$BuildDir = "",
    [ValidateSet("plain", "debug", "debugoptimized", "release", "minsize", "custom")]
    [string]$BuildType = "",
    [switch]$SetupOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$openPreyRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))

if ([string]::IsNullOrWhiteSpace($GameLibsRepo)) {
    if (-not [string]::IsNullOrWhiteSpace($env:OPENPREY_GAMELIBS_REPO)) {
        $GameLibsRepo = $env:OPENPREY_GAMELIBS_REPO
    } elseif (-not [string]::IsNullOrWhiteSpace($env:OPENQ4_GAMELIBS_REPO)) {
        $GameLibsRepo = $env:OPENQ4_GAMELIBS_REPO
    } else {
        $GameLibsRepo = Join-Path $openPreyRoot "..\OpenPrey-game"
    }
}

$gameLibsRoot = [System.IO.Path]::GetFullPath($GameLibsRepo)
foreach ($requiredDir in @("src\game", "src\Prey", "src\preyengine")) {
    $requiredPath = Join-Path $gameLibsRoot $requiredDir
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
        throw "OpenPrey-game source directory not found: '$requiredPath'."
    }
}

Write-Host "OpenPrey-game is consumed directly by the openPREY Meson build."
Write-Host "Canonical GameLibs repository: $gameLibsRoot"
Write-Host "Run tools\build\meson_setup.ps1 compile -C builddir to build the unified module."

$global:LASTEXITCODE = 0
exit 0
