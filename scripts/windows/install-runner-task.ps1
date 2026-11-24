# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\encos\work\gitea-runner'
if (-not (Test-Path "$root\.runner")) { throw 'Register the repository runner before installing its task' }
if (Get-ScheduledTask -TaskName EncosGiteaRunner -ErrorAction SilentlyContinue) {
    throw 'EncosGiteaRunner already exists; inspect it before replacing it'
}
$action = New-ScheduledTaskAction -Execute 'C:\Windows\System32\cmd.exe' `
    -Argument "/d /c $root\start-runner.cmd" -WorkingDirectory $root
$principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\encos" -LogonType S4U -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName EncosGiteaRunner -Action $action -Principal $principal `
    -Trigger (New-ScheduledTaskTrigger -AtStartup) -Settings $settings | Out-Null
Start-ScheduledTask -TaskName EncosGiteaRunner
