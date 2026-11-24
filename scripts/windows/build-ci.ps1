# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
param(
    [string]$EnvironmentFile = 'C:\Users\encos\work\tools\ci-env.cmd',
    [string]$BuildDirectory = 'build-windows-ci',
    [ValidateSet('ON', 'OFF')][string]$StaticMode = 'OFF'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not (Test-Path $EnvironmentFile)) { throw "Missing environment: $EnvironmentFile" }
$environment = & cmd.exe /d /c "call `"$EnvironmentFile`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Failed to load build environment' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
$source = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$common = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc',
    '-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++',
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_INSTALLED_DIR=$env:ENCOS_VCPKG_INSTALLED_DIR",
    '-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic', '-DVCPKG_HOST_TRIPLET=x64-mingw-dynamic')
$plugins = 'ethercat;ethernet;usbSerial;slcan;fake;gsUsb'
Invoke-Checked cmake (@('-S', $source, '-B', $BuildDirectory) + $common + @(
    "-DENCOS_STATIC_MODE=$StaticMode", '-DENCOS_ENABLE_INSTALL=OFF',
    '-DENCOS_BUILD_TESTS=ON', "-DENCOS_PLUGINS_LIST=$plugins;testDevice;testClaimDevice"))
Invoke-Checked cmake @('--build', $BuildDirectory, '--parallel', '4')
Invoke-Checked ctest @('--test-dir', $BuildDirectory, '--output-on-failure', '--timeout', '120')
if ($StaticMode -eq 'OFF') {
    $installBuild = "$BuildDirectory-install"
    $prefix = Join-Path (Get-Location) "$BuildDirectory-prefix"
    Invoke-Checked cmake (@('-S', $source, '-B', $installBuild) + $common + @(
        '-DENCOS_STATIC_MODE=OFF', '-DENCOS_ENABLE_INSTALL=ON',
        '-DENCOS_BUILD_TESTS=OFF', "-DENCOS_PLUGINS_LIST=$plugins", "-DCMAKE_INSTALL_PREFIX=$prefix"))
    Invoke-Checked cmake @('--build', $installBuild, '--parallel', '4')
    Invoke-Checked cmake @('--install', $installBuild)
    foreach ($required in @('include/encos/encos/encos_driver.h', 'lib/cmake/encos_driver/encos_driverConfig.cmake')) {
        if (-not (Test-Path (Join-Path $prefix $required))) { throw "Missing installed file: $required" }
    }
    if (-not (Get-ChildItem $prefix -Recurse -Filter '*Plugin.dll')) { throw 'No installed adapter plugins' }
    if (-not (Get-ChildItem $prefix -Recurse -Filter 'EthercatPlugin.dll')) { throw 'Ethercat plugin was not installed' }
    if (Get-ChildItem $prefix -Recurse -Filter 'Test*Device.dll') { throw 'Test plugin was installed' }
}
