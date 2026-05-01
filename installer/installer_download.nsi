; Paimbnails Mod Installer for Geometry Dash
; NSIS Script - Auto download from GitHub

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; General
Name "Paimbnails"
OutFile "..\Paimbnails-Setup-Download.exe"
InstallDir "$LOCALAPPDATA\GeometryDash\geode\mods"
RequestExecutionLevel user

; Version info
VIProductVersion "2.3.1.0"
VIAddVersionKey "ProductName" "Paimbnails"
VIAddVersionKey "CompanyName" "FlozWer"
VIAddVersionKey "FileDescription" "Paimbnails - Thumbnails for Geometry Dash"
VIAddVersionKey "FileVersion" "2.3.1"
VIAddVersionKey "LegalCopyright" "FlozWer"

; Interface
!define MUI_ICON "paimbnails.ico"
!define MUI_ABORTWARNING

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

; Variables
Var GDFolder

; Auto-detect function
Function .onInit
  ; Auto-detect Geometry Dash
  StrCpy $GDFolder ""

  ${If} ${FileExists} "$LOCALAPPDATA\GeometryDash\GeometryDash.exe"
    StrCpy $GDFolder "$LOCALAPPDATA\GeometryDash"
  ${ElseIf} ${FileExists} "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash"
  ${ElseIf} ${FileExists} "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash"
  ${ElseIf} ${FileExists} "$PROGRAMFILES64\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "$PROGRAMFILES64\SteamLibrary\steamapps\common\Geometry Dash"
  ${ElseIf} ${FileExists} "$PROGRAMFILES32\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "$PROGRAMFILES32\SteamLibrary\steamapps\common\Geometry Dash"
  ${ElseIf} ${FileExists} "C:\Games\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "C:\Games\Geometry Dash"
  ${ElseIf} ${FileExists} "D:\Steam\steamapps\common\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "D:\Steam\steamapps\common\Geometry Dash"
  ${ElseIf} ${FileExists} "D:\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "D:\SteamLibrary\steamapps\common\Geometry Dash"
  ${ElseIf} ${FileExists} "E:\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe"
    StrCpy $GDFolder "E:\SteamLibrary\steamapps\common\Geometry Dash"
  ${EndIf}

  ; If found, set as default install dir
  ${If} $GDFolder != ""
    StrCpy $INSTDIR "$GDFolder\geode\mods"
  ${EndIf}
FunctionEnd

Section "Install"
  ; Check if geode\mods folder exists
  ${If} ${FileExists} "$INSTDIR\*.*"
    ; Download latest .geode from GitHub
    DetailPrint "Downloading Paimbnails from GitHub..."

    ; Download from latest release
    NSISdl::download "https://github.com/Fl0zWer/Paimbnails/releases/latest/download/flozwer.paimbnails2.geode" "$INSTDIR\flozwer.paimbnails2.geode"

    ; Verify installation
    ${If} ${FileExists} "$INSTDIR\flozwer.paimbnails2.geode"
      DetailPrint "Installation complete!"
    ${Else}
      MessageBox MB_OK "Download failed! Please check your internet connection and try again."
      Abort
    ${EndIf}
  ${Else}
    MessageBox MB_OK "Geode folder not found at:$\n$INSTDIR$\n$\nPlease make sure Geometry Dash with Geode is installed."
    Abort
  ${EndIf}
SectionEnd
