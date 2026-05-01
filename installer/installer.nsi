; Paimbnails Mod Installer for Geometry Dash
; NSIS Script

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

; General
Name "Paimbnails"
OutFile "Paimbnails-Installer.exe"
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
Page custom DetectGDPage CreateDetectGDPage LeaveDetectGDPage
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

; Variables
Var GDFolder
Var FoundGD
Var FolderInputCtrl

; Custom page: Auto-detect Geometry Dash
Function DetectGDPage
  ; Try to find Geometry Dash automatically
  StrCpy $FoundGD "0"
  
  ; Check 1: LocalAppData\GeometryDash (most common for Geode)
  IfFileExists "$LOCALAPPDATA\GeometryDash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "$LOCALAPPDATA\GeometryDash"
    StrCpy $FoundGD "1"
    Goto done_detect

  ; Check 2: Steam default path (x64)
  IfFileExists "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  ; Check 3: Steam default path (x86) - Common for 32-bit installs
  IfFileExists "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  ; Check 3b: SteamLibrary on x86
  IfFileExists "$PROGRAMFILES32\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "$PROGRAMFILES32\SteamLibrary\steamapps\common\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  ; Check 4: Custom Steam library paths (common locations)
  IfFileExists "$PROGRAMFILES64\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "$PROGRAMFILES64\SteamLibrary\steamapps\common\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  IfFileExists "C:\Games\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "C:\Games\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  IfFileExists "D:\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "D:\SteamLibrary\steamapps\common\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  IfFileExists "D:\Steam\steamapps\common\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "D:\Steam\steamapps\common\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  IfFileExists "E:\SteamLibrary\steamapps\common\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "E:\SteamLibrary\steamapps\common\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  ; Check: Program Files (x86) without Steam subfolder - direct install
  IfFileExists "C:\Program Files (x86)\Geometry Dash\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "C:\Program Files (x86)\Geometry Dash"
    StrCpy $FoundGD "1"
    Goto done_detect

  ; Check: Epic Games
  IfFileExists "$LOCALAPPDATA\GeometryDashEpic\GeometryDash.exe" 0 +3
    StrCpy $GDFolder "$LOCALAPPDATA\GeometryDashEpic"
    StrCpy $FoundGD "1"
    Goto done_detect

done_detect:
FunctionEnd

Function CreateDetectGDPage
  !insertmacro MUI_HEADER_TEXT "Geometry Dash Location" "Select your Geometry Dash installation folder"
  
  nsDialogs::Create 1018
  Pop $0
  
  ${If} $FoundGD == "1"
    ${NSD_CreateLabel} 0 0 100% 20u "Geometry Dash was found automatically!"
    Pop $0
    ${NSD_CreateText} 0 25u 90% 15u "$GDFolder"
    Pop $FolderInputCtrl
  ${Else}
    ${NSD_CreateLabel} 0 0 100% 30u "Geometry Dash was not found automatically.$\r$\nPlease browse to your Geometry Dash folder (where GeometryDash.exe is located)."
    Pop $0
    ${NSD_CreateText} 0 35u 80% 15u ""
    Pop $FolderInputCtrl
    ${NSD_CreateBrowseButton} 85% 35u 15% 15u "..."
    Pop $1
    ${NSD_OnClick} $1 OnBrowseClick
  ${EndIf}
  
  nsDialogs::Show
FunctionEnd

Function OnBrowseClick
  nsDialogs::SelectFolderDialog "Select Geometry Dash Folder" "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash"
  Pop $0
  ${If} $0 != "error"
    StrCpy $GDFolder $0
    ${NSD_SetText} $FolderInputCtrl $GDFolder
  ${EndIf}
FunctionEnd

Function LeaveDetectGDPage
  ${If} $FoundGD != "1"
    ${NSD_GetText} $FolderInputCtrl $GDFolder
    ${If} $GDFolder == ""
      MessageBox MB_OK "Please select your Geometry Dash folder."
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Install Paimbnails Mod"
  SectionIn RO

  ; Determine target folder from auto-detect or manual selection
  ${If} $GDFolder != ""
    StrCpy $INSTDIR "$GDFolder\geode\mods"
  ${EndIf}

  ; Check if geode\mods folder exists (Geode must be installed)
  IfFileExists "$INSTDIR\*.*" install_continue no_geode_folder

  no_geode_folder:
    MessageBox MB_OK "Geode folder not found at:$\r$\n$INSTDIR$\r$\n$\r$\nPlease make sure Geometry Dash with Geode is installed."
    Abort

  install_continue:
  ; Copy the .geode file from installer directory
  SetOutPath "$INSTDIR"
  File "..\build\flozwer.paimbnails2.geode"
  
  ; Also copy resources if needed (the .geode usually includes them, but just in case)
  ; File /r "..\resources\*.*"
  
  ; Verify installation
  IfFileExists "$INSTDIR\flozwer.paimbnails2.geode" install_ok install_fail
  
  install_ok:
    DetailPrint "Paimbnails mod installed successfully!"
    MessageBox MB_OK "Paimbnails has been installed successfully!$\n$\nLocation: $INSTDIR$\n$\nRestart Geometry Dash to use the mod."
    Goto install_done
  
  install_fail:
    MessageBox MB_OK "Installation may have failed. Please check the installation folder."
    Abort
  
  install_done:
SectionEnd

Section "Open Download Page"
  ExecShell "open" "https://github.com/Fl0zWer/Paimbnails/releases/latest"
SectionEnd
