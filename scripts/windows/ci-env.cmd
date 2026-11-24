@echo off
rem SPDX-FileCopyrightText: 2026 Encos
rem SPDX-License-Identifier: MIT
call C:\Users\encos\work\tools\encos-env.cmd
if errorlevel 1 exit /b %errorlevel%
set "PATH=C:\Users\encos\work\tools\winlibs-14.2\mingw64\bin;%PATH%"
set "ENCOS_VCPKG_INSTALLED_DIR=C:\Users\encos\work\vcpkg-ci-installed"
