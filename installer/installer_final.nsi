; Paimbnails Mod Installer for Geometry Dash
; Bundles the .geode file directly — no runtime PowerShell or downloads

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
VIAddVersionKey "LegalCopyright" "FlozWer"

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
  DetailPrint "Installing Paimbnails mod..."
  
  ; Copy the bundled .geode file directly — no PowerShell, no network calls
  SetOutPath "$INSTDIR"
  File "flozwer.paimbnails2.geode"
  
  ${If} ${FileExists} "$INSTDIR\flozwer.paimbnails2.geode"
    MessageBox MB_OK "Paimbnails installed successfully!$\n$\nLocation: $INSTDIR$\n$\nRestart Geometry Dash to use the mod."
  ${Else}
    MessageBox MB_OK "Installation failed! The mod file could not be copied."
    Abort
  ${EndIf}
SectionEnd
