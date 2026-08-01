@echo off
set "VSDEVCMD="
set "VSROOT="

if not defined OPENPREY_VS_TARGET_ARCH if defined OPENQ4_VS_TARGET_ARCH set "OPENPREY_VS_TARGET_ARCH=%OPENQ4_VS_TARGET_ARCH%"
if not defined OPENPREY_VS_HOST_ARCH if defined OPENQ4_VS_HOST_ARCH set "OPENPREY_VS_HOST_ARCH=%OPENQ4_VS_HOST_ARCH%"

if not defined OPENPREY_VS_TARGET_ARCH (
    if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set "OPENPREY_VS_TARGET_ARCH=arm64"
    ) else if /I "%PROCESSOR_ARCHITECTURE%"=="x86" (
        set "OPENPREY_VS_TARGET_ARCH=x86"
    ) else (
        set "OPENPREY_VS_TARGET_ARCH=x64"
    )
)

if not defined OPENPREY_VS_HOST_ARCH (
    if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set "OPENPREY_VS_HOST_ARCH=arm64"
    ) else if /I "%PROCESSOR_ARCHITECTURE%"=="x86" (
        set "OPENPREY_VS_HOST_ARCH=x86"
    ) else (
        set "OPENPREY_VS_HOST_ARCH=x64"
    )
)

set "VS_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
if /I "%OPENPREY_VS_TARGET_ARCH%"=="arm64" set "VS_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.ARM64"

if defined VSINSTALLDIR (
    set "VSDEVCMD=%VSINSTALLDIR%Common7\Tools\VsDevCmd.bat"
    if exist "%VSDEVCMD%" goto :run_devcmd
)

for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'; if (Test-Path $vswhere) { & $vswhere -latest -prerelease -products * -requires $env:VS_COMPONENT -property installationPath }"`) do set "VSROOT=%%I"

if not defined VSROOT (
    echo [openPREY] No Visual Studio installation with C++ tools was found.
    set "OPENPREY_MSVC_ENV=0"
    exit /b 1
)

set "VSDEVCMD=%VSROOT%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
    echo [openPREY] Could not find VsDevCmd.bat at "%VSDEVCMD%".
    set "OPENPREY_MSVC_ENV=0"
    exit /b 1
)

:run_devcmd
call "%VSDEVCMD%" -arch=%OPENPREY_VS_TARGET_ARCH% -host_arch=%OPENPREY_VS_HOST_ARCH% >nul
if errorlevel 1 (
    echo [openPREY] Failed to initialize Visual Studio developer environment.
    set "OPENPREY_MSVC_ENV=0"
    exit /b 1
)

set "OPENPREY_MSVC_ENV=1"
echo [openPREY] MSVC developer environment ready.
exit /b 0
