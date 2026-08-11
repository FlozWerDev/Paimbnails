@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set ROOT=%~dp0..\..
set CLANG="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang++.exe"
set RAPIDFUZZ=%ROOT%\build\_deps\rapidfuzz-src
set STUBS=%~dp0stubs

%CLANG% -std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter ^
  -I"%RAPIDFUZZ%" -I"%STUBS%" ^
  "%~dp0conversation_harness.cpp" -o "%~dp0conversation_harness.exe"
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

"%~dp0conversation_harness.exe"
exit /b %ERRORLEVEL%
