@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set ROOT=%~dp0..\..
set CLANG="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang++.exe"

if not exist %CLANG% (
  where cl >nul 2>&1
  if errorlevel 1 (
    echo No clang++ or cl found
    exit /b 1
  )
  cl /nologo /std:c++20 /EHsc /O2 /I"%ROOT%\src" "%~dp0harness.cpp" /Fe:"%~dp0harness.exe"
) else (
  %CLANG% -std=c++20 -O1 -Wall -Wextra -Wno-unused-parameter ^
    -I"%ROOT%\src" ^
    "%~dp0harness.cpp" -o "%~dp0harness.exe"
)
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

"%~dp0harness.exe"
exit /b %ERRORLEVEL%
