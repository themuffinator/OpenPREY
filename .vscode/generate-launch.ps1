param(
    [string]$MapManifest = (Join-Path $PSScriptRoot 'prey-maps.json'),
    [string]$OutputPath = (Join-Path $PSScriptRoot 'launch.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$workspaceToken = '${workspaceFolder}'
$clientProgram = "$workspaceToken\.install\openPREY-client_x64.exe"
$dedicatedProgram = "$workspaceToken\.install\openPREY-ded_x64.exe"
$workingDirectory = "$workspaceToken\.install"
$savePath = "$workspaceToken\.home"
$devPath = "$workspaceToken\.install"

function New-BaseArguments {
    param([Parameter(Mandatory = $true)][string]$LogName)

    return @(
        '+set', 'logFile', '2',
        '+set', 'logFileName', $LogName,
        '+set', 'developer', '1',
        '+set', 'r_fullscreen', '0',
        '+set', 's_deviceName', 'default',
        '+set', 'fs_savepath', $savePath,
        '+set', 'fs_devpath', $devPath,
        '+set', 'fs_game', 'basepr'
    )
}

function New-LaunchConfiguration {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(Mandatory = $true)][object[]]$Arguments
    )

    return [ordered]@{
        name = $Name
        type = 'cppvsdbg'
        request = 'launch'
        program = $Program
        args = $Arguments
        cwd = $workingDirectory
        console = 'integratedTerminal'
    }
}

if (-not (Test-Path -LiteralPath $MapManifest -PathType Leaf)) {
    throw "Prey map manifest not found: $MapManifest"
}

$manifest = Get-Content -LiteralPath $MapManifest -Raw | ConvertFrom-Json
$configurations = [System.Collections.Generic.List[object]]::new()

$glArguments = [System.Collections.Generic.List[object]]::new()
foreach ($argument in (New-BaseArguments 'logs/openprey-gl.log')) {
    $glArguments.Add($argument)
}
foreach ($argument in @('+set', 'r_renderApi', 'gl')) {
    $glArguments.Add($argument)
}
$configurations.Add((New-LaunchConfiguration -Name 'Launch openPREY (OpenGL)' -Program $clientProgram -Arguments $glArguments.ToArray()))

$vulkanArguments = [System.Collections.Generic.List[object]]::new()
foreach ($argument in (New-BaseArguments 'logs/openprey-vulkan.log')) {
    $vulkanArguments.Add($argument)
}
foreach ($argument in @('+set', 'r_renderApi', 'vulkan')) {
    $vulkanArguments.Add($argument)
}
$configurations.Add((New-LaunchConfiguration -Name 'Launch openPREY (Vulkan)' -Program $clientProgram -Arguments $vulkanArguments.ToArray()))

foreach ($entry in $manifest.maps) {
    if ($entry.kind -notin @('sp', 'mp')) {
        throw "Unsupported map kind '$($entry.kind)' for '$($entry.name)'."
    }

    $logName = if ($entry.kind -eq 'sp') { 'logs/openprey-sp.log' } else { 'logs/openprey-mp.log' }
    $arguments = [System.Collections.Generic.List[object]]::new()
    foreach ($argument in (New-BaseArguments $logName)) {
        $arguments.Add($argument)
    }

    if ($entry.kind -eq 'sp') {
        foreach ($argument in @('+set', 'si_gameType', 'singleplayer', '+map', [string]$entry.map)) {
            $arguments.Add($argument)
        }
    } else {
        foreach ($argument in @('+set', 'si_gameType', 'deathmatch', '+set', 'si_map', [string]$entry.map, '+devmap', [string]$entry.map)) {
            $arguments.Add($argument)
        }
    }

    $configurations.Add((New-LaunchConfiguration -Name ([string]$entry.name) -Program $clientProgram -Arguments $arguments.ToArray()))
}

$dedicatedArguments = [System.Collections.Generic.List[object]]::new()
foreach ($argument in (New-BaseArguments 'logs/openprey-ded.log')) {
    $dedicatedArguments.Add($argument)
}
foreach ($argument in @(
    '+set', 'si_gameType', 'deathmatch',
    '+set', 'si_map', 'game/dmescher',
    '+spawnServer'
)) {
    $dedicatedArguments.Add($argument)
}
$configurations.Add((New-LaunchConfiguration -Name '(MP Dedicated) dmescher - Keeper Gravity' -Program $dedicatedProgram -Arguments $dedicatedArguments.ToArray()))

$document = [ordered]@{
    version = '0.2.0'
    configurations = $configurations.ToArray()
}

$json = $document | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath($OutputPath), "$json`n", [System.Text.UTF8Encoding]::new($false))
Write-Output "Generated $OutputPath from $MapManifest ($($manifest.maps.Count) map configurations)."
