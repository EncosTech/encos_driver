# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
param(
    [string]$EnvironmentFile = 'C:\Users\encos\work\tools\ci-env.cmd',
    [string]$BuildDirectory = 'build-windows-node',
    [Parameter(Mandatory = $true)][string]$NodeIncludeDirectory,
    [string]$NodeExecutable = 'C:\Users\encos\work\tools\node-v22.23.4-win-x64\node.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
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
New-Item -ItemType Directory -Force $BuildDirectory | Out-Null
$build = (Resolve-Path $BuildDirectory).Path
$prefix = Join-Path $build 'runtime'
$importDirectory = Join-Path $build 'node-import'
New-Item -ItemType Directory -Force $importDirectory | Out-Null
Push-Location $importDirectory
try {
    Invoke-Checked gendef @($NodeExecutable)
    $apiExports = Get-Content node.def | Where-Object { $_ -match '^\s*(napi_|node_api_)' }
    if (-not $apiExports) { throw 'Node executable exposes no Node-API symbols' }
    @('LIBRARY node.exe', 'EXPORTS') + $apiExports | Set-Content node-api.def -Encoding ascii
    Invoke-Checked dlltool @('-d', 'node-api.def', '-D', 'node.exe', '-l', 'libnode.dll.a')
} finally { Pop-Location }
Invoke-Checked cmake @('-S', $source, '-B', $build, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc',
    '-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++',
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_INSTALLED_DIR=$env:ENCOS_VCPKG_INSTALLED_DIR",
    '-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic', '-DVCPKG_HOST_TRIPLET=x64-mingw-dynamic',
    '-DENCOS_STATIC_MODE=OFF', '-DENCOS_ENABLE_INSTALL=ON', '-DENCOS_BUILD_TESTS=OFF',
    '-DENCOS_BUILD_NODE_BINDINGS=ON', "-DENCOS_NODE_INCLUDE_DIR=$NodeIncludeDirectory",
    "-DENCOS_NODE_IMPORT_LIBRARY=$importDirectory/libnode.dll.a",
    '-DENCOS_PLUGINS_LIST=ethercat;ethernet;usbSerial;slcan;fake;gsUsb',
    "-DCMAKE_INSTALL_PREFIX=$prefix")
Invoke-Checked cmake @('--build', $build, '--parallel', '4')
if (Test-Path $prefix) { Remove-Item $prefix -Recurse -Force }
Invoke-Checked cmake @('--install', $build)

# Bundle non-system DLLs; Npcap remains a system driver dependency.
$bin = Join-Path $prefix 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem $prefix -Recurse -File | Where-Object { $_.Extension -in '.dll', '.exe' } |
    ForEach-Object { $queue.Enqueue($_.FullName) }
$queue.Enqueue((Join-Path $build 'native/encosdriver.node'))
$seen = @{}
$search = @($bin, (Join-Path $env:ENCOS_VCPKG_INSTALLED_DIR 'x64-mingw-dynamic/bin')) + ($env:PATH -split ';')
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    $imports = & objdump -p $binary
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect DLL dependencies: $binary" }
    foreach ($line in $imports) {
        if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
        $name = $matches[1]
        if ($seen.ContainsKey($name)) { continue }
        $seen[$name] = $true
        if ($name -match '^(node\.exe|wpcap\.dll|Packet\.dll|api-ms-|ext-ms-)') { continue }
        if (Test-Path (Join-Path "$env:WINDIR/System32" $name)) { continue }
        $dependency = $null
        foreach ($directory in $search) {
            if ($directory -and (Test-Path (Join-Path $directory $name))) {
                $dependency = Join-Path $directory $name
                break
            }
        }
        if (-not $dependency) { throw "Unresolved DLL $name imported by $binary" }
        $destination = Join-Path $bin $name
        if ([IO.Path]::GetFullPath($dependency) -ne [IO.Path]::GetFullPath($destination)) {
            Copy-Item $dependency $destination -Force
        }
        $queue.Enqueue($destination)
    }
}
Write-Output "Runtime: $prefix"
Write-Output "Addon: $(Join-Path $build 'native/encosdriver.node')"
