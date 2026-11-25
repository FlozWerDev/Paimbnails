@echo off
setlocal

set ROOT=%~dp0..\..
set "CLANG=clang-cl"
where clang-cl >nul 2>&1
if errorlevel 1 (
  echo clang-cl not found on PATH
  exit /b 1
)
set "RAPIDFUZZ="
for %%B in (build build-clang) do (
  for /d %%D in ("%ROOT%\%%B\cpm-cache\rapidfuzz\*") do (
    if exist "%%~D\rapidfuzz\fuzz.hpp" set "RAPIDFUZZ=%%~D"
  )
)
if not defined RAPIDFUZZ (
  echo RapidFuzz headers not found under build\cpm-cache
  exit /b 1
)
set STUBS=%~dp0stubs
set "EXE=%TEMP%\paimon-guide-matcher.exe"

"%CLANG%" /nologo /std:c++20 /EHsc /O1 /W4 /wd4100 ^
  /I"%RAPIDFUZZ%" /I"%STUBS%" ^
  "%~dp0harness.cpp" /Fe:"%EXE%"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

"%EXE%"
exit /b %ERRORLEVEL%
