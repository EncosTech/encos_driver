@echo off
rem SPDX-FileCopyrightText: 2026 Encos
rem SPDX-License-Identifier: MIT
call C:\Users\encos\work\tools\encos-env.cmd
if errorlevel 1 exit /b %errorlevel%
set "PATH=C:\Users\encos\work\tools\node-v22.23.4-win-x64;%PATH%"
set "XDG_CACHE_HOME=C:\Users\encos\work\cache"
cd /d C:\Users\encos\work\gitea-runner
if errorlevel 1 exit /b %errorlevel%
C:\Users\encos\work\gitea-runner\gitea-runner.exe daemon --config config.yaml >> daemon.log 2>&1
exit /b %errorlevel%
