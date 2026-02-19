param(
    [string]$GameLibsRepo = "",
    [string]$BuildDir = "",
    [switch]$SetupOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$openQ4Root = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))

if ([string]::IsNullOrWhiteSpace($GameLibsRepo)) {
    $GameLibsRepo = Join-Path $openQ4Root "..\OpenQ4-GameLibs"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $GameLibsRepo "builddir"
}

$gameLibsRoot = [System.IO.Path]::GetFullPath($GameLibsRepo)
$gameLibsBuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$gameLibsMesonSetup = Join-Path $gameLibsRoot "tools\build\meson_setup.ps1"
$gameLibsCoreData = Join-Path $gameLibsBuildDir "meson-private\coredata.dat"
$gameLibsBuildNinja = Join-Path $gameLibsBuildDir "build.ninja"

if (-not (Test-Path $gameLibsRoot)) {
    throw "OpenQ4-GameLibs repository not found at '$gameLibsRoot'. Set OPENQ4_GAMELIBS_REPO or pass -GameLibsRepo."
}

if (-not (Test-Path $gameLibsMesonSetup)) {
    throw "OpenQ4-GameLibs Meson wrapper not found at '$gameLibsMesonSetup'."
}

Write-Host "Building OpenQ4 SDK game libraries from:"
Write-Host "  Repo: $gameLibsRoot"
Write-Host "  BuildDir: $gameLibsBuildDir"

if ((Test-Path $gameLibsCoreData) -and (Test-Path $gameLibsBuildNinja)) {
    & $gameLibsMesonSetup setup --reconfigure $gameLibsBuildDir $gameLibsRoot
} else {
    & $gameLibsMesonSetup setup --wipe $gameLibsBuildDir $gameLibsRoot --backend ninja --buildtype release --vsenv
}
$setupExit = [int]$LASTEXITCODE
if ($setupExit -ne 0) {
    exit $setupExit
}

if ($SetupOnly) {
    exit 0
}

& $gameLibsMesonSetup compile -C $gameLibsBuildDir
$compileExit = [int]$LASTEXITCODE
if ($compileExit -ne 0) {
    exit $compileExit
}

Write-Host "OpenQ4-GameLibs build complete."
