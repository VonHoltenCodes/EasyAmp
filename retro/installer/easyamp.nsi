; EasyAmp retro - installer for Windows 98 SE through XP.
;
; NSIS because its installers still run on Windows 95 and later, and makensis
; runs natively on Linux, so CI builds this with no Windows machine involved.
; ANSI ("Unicode false") is what makes the result load on Windows 9x.
;
;   makensis -DVERSION=0.4.1 installer/easyamp.nsi     (from retro/)

Unicode false
SetCompressor /SOLID lzma
RequestExecutionLevel admin

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!define APPNAME   "EasyAmp"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\EasyAmpRetro"

Name "${APPNAME} ${VERSION}"
OutFile "..\build\EasyAmp-Retro-Setup.exe"
InstallDir "$PROGRAMFILES\EasyAmp"
InstallDirRegKey HKLM "Software\EasyAmpRetro" "InstallDir"
BrandingText "EasyAmp retro ${VERSION} - free and open source"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "EasyAmp"
VIAddVersionKey "FileDescription" "EasyAmp retro installer (Windows 98 SE - XP)"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "VonHoltenCodes"
VIAddVersionKey "LegalCopyright" "MIT License"

!include "MUI2.nsh"
!define MUI_ICON   "..\src\easyamp.ico"
!define MUI_UNICON "..\src\easyamp.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "EasyAmp for Windows 98 SE and XP"
!define MUI_WELCOMEPAGE_TEXT "This sets up EasyAmp: a music player written for the machines that still run.$\r$\n$\r$\nIt plays MP3, FLAC, Ogg Vorbis and WAV, and streams from Plex and Jellyfin.$\r$\n$\r$\nIt is one small program. Setup copies it, adds shortcuts, and can remove every trace again from Add/Remove Programs."
!define MUI_FINISHPAGE_RUN "$INSTDIR\EASYAMP.EXE"
!define MUI_FINISHPAGE_RUN_TEXT "Start EasyAmp"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.TXT"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Read the getting-started notes (Plex, Jellyfin)"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "EasyAmp (required)" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\build\EASYAMP.EXE"
  File "..\web\README.TXT"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\EasyAmpRetro" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "EasyAmp (retro)"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "VonHoltenCodes"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\EASYAMP.EXE"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTKEY}" "URLInfoAbout" "https://www.easyampstereo.com/retro.html"
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Start Menu shortcuts" SecStart
  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\EasyAmp"
  CreateShortCut "$SMPROGRAMS\EasyAmp\EasyAmp.lnk" "$INSTDIR\EASYAMP.EXE"
  CreateShortCut "$SMPROGRAMS\EasyAmp\Getting started.lnk" "$INSTDIR\README.TXT"
  CreateShortCut "$SMPROGRAMS\EasyAmp\Uninstall EasyAmp.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesk
  SetShellVarContext all
  CreateShortCut "$DESKTOP\EasyAmp.lnk" "$INSTDIR\EASYAMP.EXE"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore}  "The player itself and its getting-started notes."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStart} "An EasyAmp folder in the Start Menu."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesk}  "An EasyAmp icon on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetShellVarContext all
  Delete "$SMPROGRAMS\EasyAmp\EasyAmp.lnk"
  Delete "$SMPROGRAMS\EasyAmp\Getting started.lnk"
  Delete "$SMPROGRAMS\EasyAmp\Uninstall EasyAmp.lnk"
  RMDir  "$SMPROGRAMS\EasyAmp"
  Delete "$DESKTOP\EasyAmp.lnk"
  Delete "$INSTDIR\EASYAMP.EXE"
  Delete "$INSTDIR\README.TXT"
  Delete "$INSTDIR\Uninstall.exe"
  ; settings hold server sign-in tokens: ask, and default to keeping them
  IfFileExists "$INSTDIR\EASYAMP.INI" 0 +3
    MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also remove your EasyAmp settings, playlist and saved server sign-ins?" /SD IDNO IDNO +3
      Delete "$INSTDIR\EASYAMP.INI"
      Delete "$INSTDIR\EASYAMP.M3U"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "Software\EasyAmpRetro"
SectionEnd
