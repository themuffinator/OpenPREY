param(
    [string]$GameLibsRepo = ""
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
if (-not (Test-Path $gameLibsRoot)) {
    throw "OpenPrey-game repository not found at '$gameLibsRoot'. Set OPENPREY_GAMELIBS_REPO or pass -GameLibsRepo."
}

if (Test-Path (Join-Path $openPreyRoot "src\game")) {
    Write-Warning "openPREY consumes canonical game sources directly from OpenPrey-game; the in-repo src\\game mirror is obsolete."
}

Write-Host "sync_gamelibs.ps1 is deprecated. No files were copied."
Write-Host "Using OpenPrey-game at: $gameLibsRoot"

$global:LASTEXITCODE = 0
exit 0
