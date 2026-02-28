param(
    [string]$GameLibsRepo = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

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

$gameLibsRoot = [System.IO.Path]::GetFullPath($GameLibsRepo)
$syncMappings = @(
    @{
        Name = "game sources"
        Source = Join-Path $gameLibsRoot "src\game"
        Destination = Join-Path $openPreyRoot "src\game"
        Excludes = @("Callbacks.cpp", "gamesys\Callbacks.cpp")
        Optional = $false
    },
    @{
        Name = "Prey gameplay sources"
        Source = Join-Path $gameLibsRoot "src\Prey"
        Destination = Join-Path $openPreyRoot "src\Prey"
        Optional = $true
    },
    @{
        Name = "preyengine shared headers"
        Source = Join-Path $gameLibsRoot "src\preyengine"
        Destination = Join-Path $openPreyRoot "src\preyengine"
        Optional = $true
    }
)

if (-not (Test-Path $gameLibsRoot)) {
    throw "OpenPrey-GameLibs repository not found at '$gameLibsRoot'. Set OPENPREY_GAMELIBS_REPO (or legacy OPENQ4_GAMELIBS_REPO) or pass -GameLibsRepo."
}

$hasChanges = $false
foreach ($mapping in $syncMappings) {
    $sourceDir = [System.IO.Path]::GetFullPath($mapping.Source)
    $destinationDir = [System.IO.Path]::GetFullPath($mapping.Destination)

    if (-not (Test-Path $sourceDir)) {
        if ($mapping.ContainsKey("Optional") -and $mapping.Optional) {
            Write-Host "Skipping optional sync mapping '$($mapping.Name)' (source not found): $sourceDir"
            continue
        }

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

$singleFileMappings = @(
    @{
        Name = "declPreyBeam header"
        Source = Join-Path $gameLibsRoot "src\framework\declPreyBeam.h"
        Destination = Join-Path $openPreyRoot "src\framework\declPreyBeam.h"
        Optional = $true
    }
)

foreach ($mapping in $singleFileMappings) {
    $sourceFile = [System.IO.Path]::GetFullPath($mapping.Source)
    $destinationFile = [System.IO.Path]::GetFullPath($mapping.Destination)

    if (-not (Test-Path $sourceFile)) {
        if ($mapping.ContainsKey("Optional") -and $mapping.Optional) {
            Write-Host "Skipping optional file mapping '$($mapping.Name)' (source not found): $sourceFile"
            continue
        }

        throw "GameLibs source file not found: '$sourceFile'."
    }

    $copyNeeded = $true
    if (Test-Path $destinationFile) {
        $sourceHash = (Get-FileHash -Path $sourceFile -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -Path $destinationFile -Algorithm SHA256).Hash
        if ($sourceHash -eq $destinationHash) {
            $copyNeeded = $false
        }
    } else {
        $destinationParent = Split-Path -Parent $destinationFile
        if (-not (Test-Path $destinationParent)) {
            New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
        }
    }

    if ($copyNeeded) {
        Write-Host "Syncing file '$($mapping.Name)':"
        Write-Host "  From: $sourceFile"
        Write-Host "    To: $destinationFile"
        Copy-Item -Path $sourceFile -Destination $destinationFile -Force
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
