param(
    [string]$GameLibsRepo = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$openQ4Root = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))

if ([string]::IsNullOrWhiteSpace($GameLibsRepo)) {
    $GameLibsRepo = Join-Path $openQ4Root "..\OpenQ4-GameLibs"
}

$gameLibsRoot = [System.IO.Path]::GetFullPath($GameLibsRepo)
$syncMappings = @(
    @{
        Name = "game sources"
        Source = Join-Path $gameLibsRoot "src\game"
        Destination = Join-Path $openQ4Root "src\game"
        Excludes = @("Callbacks.cpp", "gamesys\Callbacks.cpp")
    }
)

if (-not (Test-Path $gameLibsRoot)) {
    throw "OpenQ4-GameLibs repository not found at '$gameLibsRoot'. Set OPENQ4_GAMELIBS_REPO or pass -GameLibsRepo."
}

$hasChanges = $false
foreach ($mapping in $syncMappings) {
    $sourceDir = [System.IO.Path]::GetFullPath($mapping.Source)
    $destinationDir = [System.IO.Path]::GetFullPath($mapping.Destination)

    if (-not (Test-Path $sourceDir)) {
        throw "GameLibs source path not found: '$sourceDir'."
    }

    Write-Host "Syncing $($mapping.Name):"
    Write-Host "  From: $sourceDir"
    Write-Host "    To: $destinationDir"

    $excludeArgs = @()
    if ($mapping.ContainsKey("Excludes")) {
        $excludeArgs = @($mapping.Excludes)
    }

    if ($excludeArgs.Count -gt 0) {
        & robocopy $sourceDir $destinationDir /MIR /R:2 /W:1 /NFL /NDL /NP /NJH /NJS /NC /NS /XF @excludeArgs
    } else {
        & robocopy $sourceDir $destinationDir /MIR /R:2 /W:1 /NFL /NDL /NP /NJH /NJS /NC /NS
    }
    $robocopyExit = [int]$LASTEXITCODE
    if ($robocopyExit -ge 8) {
        throw "robocopy failed while syncing '$sourceDir' -> '$destinationDir' (exit code $robocopyExit)."
    }

    if ($robocopyExit -ne 0) {
        $hasChanges = $true
    }
}

if ($hasChanges) {
    Write-Host "GameLibs sync complete (changes applied)."
} else {
    Write-Host "GameLibs sync complete (no changes)."
}

$global:LASTEXITCODE = 0
exit 0
