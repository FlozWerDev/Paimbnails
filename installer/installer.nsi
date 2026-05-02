; Paimbnails Mod Installer for Geometry Dash
; Minimal installer - copies .geode to geode\mods

!include "MUI2.nsh"

Name "Paimbnails"
OutFile "Paimbnails-Installer.exe"
InstallDir "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash\geode\mods"
RequestExecutionLevel admin

VIProductVersion "2.3.1.0"
VIAddVersionKey "ProductName" "Paimbnails"
VIAddVersionKey "CompanyName" "FlozWer"
VIAddVersionKey "FileDescription" "Paimbnails - Thumbnails for Geometry Dash"
VIAddVersionKey "FileVersion" "2.3.1"
VIAddVersionKey "LegalCopyright" "FlozWer"

!define MUI_ICON "paimbnails.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE
BrandingText "Paimbnails by FlozWer"

; Banner images
!define MUI_WELCOMEFINISHPAGE_BITMAP "banner.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "banner.bmp"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"

Var GDPath

Function .onInit
    ; Try to detect geode\mods automatically
    StrCpy $GDPath "$PROGRAMFILES32\Steam\steamapps\common\Geometry Dash\geode\mods"
    IfFileExists $GDPath 0 tryNext
        StrCpy $INSTDIR $GDPath
        Return
    tryNext:
    StrCpy $GDPath "$PROGRAMFILES64\Steam\steamapps\common\Geometry Dash\geode\mods"
    IfFileExists $GDPath 0 trySteamLibrary
        StrCpy $INSTDIR $GDPath
        Return
    trySteamLibrary:
    StrCpy $GDPath "C:\SteamLibrary\steamapps\common\Geometry Dash\geode\mods"
    IfFileExists $GDPath 0 tryD
        StrCpy $INSTDIR $GDPath
        Return
    tryD:
    StrCpy $GDPath "D:\SteamLibrary\steamapps\common\Geometry Dash\geode\mods"
    IfFileExists $GDPath 0 tryE
        StrCpy $INSTDIR $GDPath
        Return
    tryE:
    StrCpy $GDPath "E:\SteamLibrary\steamapps\common\Geometry Dash\geode\mods"
    IfFileExists $GDPath 0 done
        StrCpy $INSTDIR $GDPath
    done:
FunctionEnd

Function .onVerifyInstDir
    ; Verify this is a geode\mods folder by checking parent is Geometry Dash
    IfFileExists "$INSTDIR\..\GeometryDash.exe" 0 check2
        Return
    check2:
    IfFileExists "$INSTDIR\..\..\GeometryDash.exe" 0 invalid
        Return
    invalid:
        MessageBox MB_OK "Please select the geode\\mods folder inside your Geometry Dash installation.$\r$\nExample: C:\\Program Files (x86)\\Steam\\steamapps\\common\\Geometry Dash\\geode\\mods"
        Abort
FunctionEnd

Section "Install Paimbnails"
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "..\build\flozwer.paimbnails2.geode"
SectionEnd
