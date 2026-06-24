; Paimbnails Mod Installer for Geometry Dash
; NSIS Script - Fully Automatic

!include "MUI2.nsh"
!include "LogicLib.nsh"

; General
Name "Paimbnails"
OutFile "Paimbnails-Setup.exe"
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
!define MUI_QUIETWINDOW

; Silent install
RequestExecutionLevel user

; Variables
Var GDFolder

; Auto-install section
Section "Install"
  ; Auto-detect Geometry Dash
  StrCpy $GDFolder ""

  ; Check all possible locations
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

  ; Check if Geometry Dash was found
  ${If} $GDFolder == ""
    MessageBox MB_OK "Geometry Dash not found! Please install Geometry Dash with Geode first."
    Abort
  ${EndIf}

  ; Set target folder
  StrCpy $INSTDIR "$GDFolder\geode\mods"

  ; Check if geode\mods exists
  ${If} ${FileExists} "$INSTDIR\*.*"
    ; Install mod
    SetOutPath "$INSTDIR"
    File "flozwer.paimbnails2.geode"

    ; Verify
    ${If} ${FileExists} "$INSTDIR\flozwer.paimbnails2.geode"
      MessageBox MB_OK "Paimbnails installed successfully!$\n$\nLocation: $INSTDIR$\n$\nRestart Geometry Dash to use the mod."
    ${Else}
      MessageBox MB_OK "Installation failed!"
      Abort
    ${EndIf}
  ${Else}
    MessageBox MB_OK "Geode not found at:$\n$INSTDIR$\n$\nPlease install Geometry Dash with Geode first."
    Abort
  ${EndIf}
SectionEnd
