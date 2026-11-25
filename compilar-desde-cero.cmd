@echo off
setlocal
title Paimbnails - Compilacion desde cero
rem Por defecto: doble de tareas que procesadores logicos.
rem Personalizar: compilar-desde-cero.cmd -Hilos 36

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0compilar-desde-cero.ps1" %*
set "build_exit_code=%errorlevel%"

echo.
if not "%build_exit_code%"=="0" echo La compilacion termino con un error. Revisa el mensaje anterior.
if /I not "%~1"=="-Comprobar" pause
exit /b %build_exit_code%
