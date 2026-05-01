; Paimbnails Mod Installer for Geometry Dash
; Downloads latest .geode from GitHub Releases

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

Name "Paimbnails"
OutFile "Paimbnails-Setup.exe"
InstallDir "$LOCALAPPDATA\GeometryDash\geode\mods"
RequestExecutionLevel user

VIProductVersion "2.3.1.0"
VIAddVersionKey "ProductName" "Paimbnails"
VIAddVersionKey "CompanyName" "FlozWer"
VIAddVersionKey "FileDescription" "Paimbnails - Thumbnails for Geometry Dash"
VIAddVersionKey "FileVersion" "2.3.1"

!define MUI_ICON "paimbnails.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

Function .onInit
  StrCpy $INSTDIR "$LOCALAPPDATA\GeometryDash\geode\mods"
  
  ${If} ${FileExists} "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash\geode\mods"
    StrCpy $INSTDIR "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash\geode\mods"
  ${EndIf}
  
  ${If} ${FileExists} "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash\geode\mods"
    StrCpy $INSTDIR "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash\geode\mods"
  ${EndIf}
FunctionEnd

Section "Install"
  DetailPrint "Downloading latest Paimbnails from GitHub..."
  
  ; Download latest .geode from GitHub using PowerShell (handles redirects)
  nsExec::ExecToStack 'powershell -Command "$ProgressPreference = ''SilentlyContinue''; try { Invoke-WebRequest -Uri ''https://github.com/FlozWerDev/Paimbnails/releases/latest/download/flozwer.paimbnails2.geode'' -OutFile ''$TEMP\flozwer.paimbnails2.geode'' -UseBasicParsing -MaximumRedirection 10 } catch { exit 1 }"'
  Pop $0
  
  ${If} $0 == "0"
    DetailPrint "Download successful!"
    CopyFiles "$TEMP\flozwer.paimbnails2.geode" "$INSTDIR\flozwer.paimbnails2.geode"
  ${Else}
    DetailPrint "Download failed, using local file..."
    ; Fallback: use local bundled file
    SetOutPath "$INSTDIR"
    File "flozwer.paimbnails2.geode"
  ${EndIf}
  
  ${If} ${FileExists} "$INSTDIR\flozwer.paimbnails2.geode"
    MessageBox MB_OK "Paimbnails installed successfully!$\n$\nLocation: $INSTDIR$\n$\nRestart Geometry Dash to use the mod."
  ${Else}
    MessageBox MB_OK "Installation failed! Could not download or find the mod file."
    Abort
  ${EndIf}
SectionEnd
