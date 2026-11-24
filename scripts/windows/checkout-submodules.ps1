# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
$ErrorActionPreference = 'Stop'
if (-not $env:GITEA_SUBMODULE_PASSWORD) { throw 'USER_PASSWORD secret is required for private submodules' }
$askpass = Join-Path $env:RUNNER_TEMP ('encos-askpass-' + [guid]::NewGuid() + '.cmd')
@'
@echo off
rem SPDX-FileCopyrightText: 2026 Encos
rem SPDX-License-Identifier: MIT
set "ENCOS_ASKPASS_PROMPT=%~1"
powershell -NoProfile -NonInteractive -Command "if ($env:ENCOS_ASKPASS_PROMPT -match 'Username') { [Console]::Write('bismarckkk') } else { [Console]::Write($env:GITEA_SUBMODULE_PASSWORD) }"
'@ | Set-Content -Encoding ASCII $askpass
$previous = $env:GIT_ASKPASS
try {
    $env:GIT_ASKPASS = $askpass
    $env:GIT_TERMINAL_PROMPT = '0'
    & git -c credential.helper= submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "Submodule checkout failed: $LASTEXITCODE" }
} finally {
    $env:GIT_ASKPASS = $previous
    Remove-Item $askpass -Force
}
