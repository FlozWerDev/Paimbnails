@echo off
chcp 65001 >nul
echo ==========================================
echo  Paimbnails Installer Builder
echo ==========================================
echo.

set NSIS_PATH=C:\Program Files (x86)\NSIS\makensis.exe
set NSIS_PATH_2=C:\Program Files\NSIS\makensis.exe

if exist "%NSIS_PATH%" (
    echo Found NSIS at: %NSIS_PATH%
    "%NSIS_PATH%" /V4 installer.nsi
    goto done
)

if exist "%NSIS_PATH_2%" (
    echo Found NSIS at: %NSIS_PATH_2%
    "%NSIS_PATH_2%" /V4 installer.nsi
    goto done
)

echo ERROR: NSIS not found!
echo.
echo Please install NSIS from: https://nsis.sourceforge.io/Download
echo After installation, run this batch file again.
echo.
pause
exit /b 1

:done
echo.
echo Installer built successfully!
echo Output: Paimbnails-Installer.exe
pause
