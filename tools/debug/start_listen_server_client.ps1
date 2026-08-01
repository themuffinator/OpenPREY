[CmdletBinding()]
param(
    [string]$Map = "game/dmroadhouse",

    [int]$Port = 28110,

    [int]$MaxFPS = 240,

    [ValidateRange(0, 1)]
    [int]$SwapInterval = 0,

    [string]$ClientName = "LoopbackClient",

    [int]$ServerWaitSeconds = 15,

    [int]$ClientSettleSeconds = 5,

    [string]$BasePath = "",

    [string]$SaveRoot = "",

    [switch]$ShowFPS,

    [ValidateRange(0, 2)]
    [int]$ShowFramePacing = 0
)

$ErrorActionPreference = "Stop"

function New-openPREYCommonArgs {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SavePath,

        [Parameter(Mandatory = $true)]
        [string]$LogFileName,

        [Parameter(Mandatory = $true)]
        [string]$InstallDir,

        [string]$BasePath,

        [Parameter(Mandatory = $true)]
        [string]$MaxFPS,

        [Parameter(Mandatory = $true)]
        [string]$SwapInterval,

        [Parameter(Mandatory = $true)]
        [bool]$ShowFPS,

        [Parameter(Mandatory = $true)]
        [int]$ShowFramePacing
    )

    $args = @(
        "+set", "win_allowMultipleInstances", "1",
        "+set", "logFile", "2",
        "+set", "logFileName", $LogFileName,
        "+set", "developer", "1",
        "+set", "com_maxfps", $MaxFPS,
        "+set", "r_swapInterval", $SwapInterval,
        "+set", "r_fullscreen", "0",
        "+set", "g_autoScreenshot", "0",
        "+set", "fs_savepath", $SavePath,
        "+set", "fs_devpath", $InstallDir,
        "+set", "fs_game", "basepr"
    )

    if (-not [string]::IsNullOrWhiteSpace($BasePath)) {
        $args += @("+set", "fs_basepath", $BasePath)
    }

    if ($ShowFPS) {
        $args += @("+set", "com_showFPS", "1")
    }

    if ($ShowFramePacing -gt 0) {
        $args += @("+set", "com_showFramePacing", $ShowFramePacing.ToString())
    }

    return $args
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))
$installDir = Join-Path $workspaceRoot ".install"
$exePath = Join-Path $installDir "openPREY-client_x64.exe"

if ([string]::IsNullOrWhiteSpace($SaveRoot)) {
    $SaveRoot = Join-Path $workspaceRoot ".tmp"
} elseif (-not [System.IO.Path]::IsPathRooted($SaveRoot)) {
    $SaveRoot = Join-Path $workspaceRoot $SaveRoot
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "openPREY client executable not found: $exePath"
}

if (-not (Test-Path -LiteralPath $installDir)) {
    throw "openPREY install directory not found: $installDir"
}

if (-not [string]::IsNullOrWhiteSpace($BasePath) -and -not (Test-Path -LiteralPath (Join-Path $BasePath "base"))) {
    throw "Prey base path must contain base/: $BasePath"
}

if (-not (Test-Path -LiteralPath $SaveRoot)) {
    New-Item -ItemType Directory -Force -Path $SaveRoot | Out-Null
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serverSavePath = Join-Path $SaveRoot ("listen_server_{0}" -f $stamp)
$clientSavePath = Join-Path $SaveRoot ("listen_client_{0}" -f $stamp)
New-Item -ItemType Directory -Force -Path $serverSavePath, $clientSavePath | Out-Null

$maxFPSValue = $MaxFPS.ToString()
$swapIntervalValue = $SwapInterval.ToString()

$serverArgs = New-openPREYCommonArgs `
    -SavePath $serverSavePath `
    -LogFileName "logs/listen-server.log" `
    -InstallDir $installDir `
    -BasePath $BasePath `
    -MaxFPS $maxFPSValue `
    -SwapInterval $swapIntervalValue `
    -ShowFPS $ShowFPS.IsPresent `
    -ShowFramePacing $ShowFramePacing

$serverArgs += @(
    "+set", "net_serverDedicated", "0",
    "+set", "net_port", $Port.ToString(),
    "+seta", "si_pure", "0",
    "+set", "net_serverAllowServerMod", "1",
    "+set", "sv_cheats", "1",
    "+set", "si_gameType", "deathmatch",
    "+spawnServer", $Map
)

$clientArgs = New-openPREYCommonArgs `
    -SavePath $clientSavePath `
    -LogFileName "logs/listen-client.log" `
    -InstallDir $installDir `
    -BasePath $BasePath `
    -MaxFPS $maxFPSValue `
    -SwapInterval $swapIntervalValue `
    -ShowFPS $ShowFPS.IsPresent `
    -ShowFramePacing $ShowFramePacing

$clientArgs += @(
    "+set", "ui_name", $ClientName,
    "+connect", ("127.0.0.1:{0}" -f $Port)
)

Write-Host ("Launching listen server on {0} ({1})" -f $Port, $Map)
Write-Host ("Server savepath: {0}" -f $serverSavePath)
Write-Host ("Client savepath: {0}" -f $clientSavePath)

$serverProcess = Start-Process -FilePath $exePath -WorkingDirectory $installDir -ArgumentList $serverArgs -PassThru

if ($ServerWaitSeconds -gt 0) {
    Start-Sleep -Seconds $ServerWaitSeconds
}

$clientProcess = Start-Process -FilePath $exePath -WorkingDirectory $installDir -ArgumentList $clientArgs -PassThru

if ($ClientSettleSeconds -gt 0) {
    Start-Sleep -Seconds $ClientSettleSeconds
}

$serverProcess.Refresh()
$clientProcess.Refresh()

$result = [pscustomobject]@{
    Timestamp       = $stamp
    Map             = $Map
    Port            = $Port
    MaxFPS          = $MaxFPS
    SwapInterval    = $SwapInterval
    Fullscreen      = $false
    ShowFPS         = $ShowFPS.IsPresent
    ShowFramePacing = $ShowFramePacing
    ServerPID       = $serverProcess.Id
    ClientPID       = $clientProcess.Id
    ServerRunning   = -not $serverProcess.HasExited
    ClientRunning   = -not $clientProcess.HasExited
    ServerSavePath  = $serverSavePath
    ClientSavePath  = $clientSavePath
    ServerLog       = (Join-Path $serverSavePath "basepr\logs\listen-server.log")
    ClientLog       = (Join-Path $clientSavePath "basepr\logs\listen-client.log")
}

$result
