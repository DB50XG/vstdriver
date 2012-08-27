!include "x64.nsh"
!include MUI2.nsh
!include WinVer.nsh
; The name of the installer
Name "VST MIDI System Synth"

; The file to write
OutFile "vstmididrv.exe"
; Request application privileges for Windows Vista
RequestExecutionLevel admin
SetCompressor /solid lzma 
;--------------------------------
; Pages
!insertmacro MUI_PAGE_WELCOME
Page Custom LockedListShow
!insertmacro MUI_PAGE_INSTFILES
UninstPage Custom un.LockedListShow
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro DeleteOnReboot Path
  IfFileExists `${Path}` 0 +3
    SetFileAttributes `${Path}` NORMAL
    Delete /rebootok `${Path}`
!macroend
!define DeleteOnReboot `!insertmacro DeleteOnReboot`

Function LockedListShow
 ${If} ${AtLeastWinVista}
  !insertmacro MUI_HEADER_TEXT `File in use check` `Drive use check`
  LockedList::AddModule \vstmididrv.dll
  LockedList::Dialog  /autonext   
  Pop $R0
  ${EndIf}
FunctionEnd
Function un.LockedListShow
 ${If} ${AtLeastWinVista}
  !insertmacro MUI_HEADER_TEXT `File in use check` `Drive use check`
  LockedList::AddModule \vstmididrv.dll
  LockedList::Dialog  /autonext   
  Pop $R0
 ${EndIf}
FunctionEnd
;--------------------------------
Function .onInit
ReadRegStr $R0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth" "UninstallString"
  StrCmp $R0 "" done
  MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
  "The MIDI driver is already installed. $\n$\nClick `OK` to remove the \
  previous version or `Cancel` to cancel this upgrade." \
  IDOK uninst
  Abort
;Run the uninstaller
uninst:
  ClearErrors
  Exec $R0
  Abort
done:
   MessageBox MB_YESNO "This will install the VST MIDI System Synth. Continue?" IDYES NoAbort
     Abort ; causes installer to quit.
   NoAbort:
 FunctionEnd
; The stuff to install
Section "Needed (required)"
  SectionIn RO
  ; Copy files according to whether its x64 or not.
   DetailPrint "Copying driver and synth..."
   ${If} ${RunningX64}
   SetOutPath $WINDIR\SysWow64
   File output\vstmididrv.dll 
   File output\vstmididrvcfg.exe
   File output\vsthost32.exe
   File output\64\vsthost64.exe
   SetOutPath $WINDIR\SysNative
   File output\64\vstmididrv.dll 
   File output\64\vstmididrvcfg.exe
   File output\vsthost32.exe
   File output\64\vsthost64.exe
   ;check if already installed
   StrCpy  $1 "0"
LOOP1:
  ;k not installed, do checks
  IntOp $1 $1 + 1
  ClearErrors
  ReadRegStr $0  HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "midi$1"
  StrCmp $0 "" INSTALLDRIVER1 NEXTCHECK1
  NEXTCHECK1:
  StrCmp $0 "wdmaud.drv" 0  NEXT1
INSTALLDRIVER1:
  WriteRegStr HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "midi$1" "vstmididrv.dll"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDI" "midi$1"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDIDRV" "$0"
  Goto REGDONE1
NEXT1:
  StrCmp $1 "9" 0 LOOP1
REGDONE1:
   ;check if 64-bit already installed
   SetRegView 64
   StrCpy  $1 "0"
LOOP2:
  ;k not installed, do checks
  IntOp $1 $1 + 1
  ClearErrors
  ReadRegStr $0  HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "midi$1"
  StrCmp $0 "" INSTALLDRIVER2 NEXTCHECK2
  NEXTCHECK2:
  StrCmp $0 "wdmaud.drv" 0  NEXT2
INSTALLDRIVER2:
  WriteRegStr HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "midi$1" "vstmididrv.dll"
  SetRegView 32
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDI64" "midi$1"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDIDRV64" "$0"
  Goto REGDONE2
NEXT2:
  StrCmp $1 "9" 0 LOOP2
REGDONE2:
   ${Else}
   SetOutPath $WINDIR\System32
   File output\vstmididrv.dll 
   File output\vstmididrvcfg.exe
   File output\vsthost32.exe

   ;check if already installed
   StrCpy  $1 "0"
LOOP3:
  ;k not installed, do checks
  IntOp $1 $1 + 1
  ClearErrors
  ReadRegStr $0  HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "midi$1"
  StrCmp $0 "" INSTALLDRIVER3 NEXTCHECK3
  NEXTCHECK3:
  StrCmp $0 "wdmaud.drv" 0  NEXT3
INSTALLDRIVER3:
  WriteRegStr HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "midi$1" "vstmididrv.dll"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDI" "midi$1"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDIDRV" "$0"
  Goto REGDONE3
NEXT3:
  StrCmp $1 "9" 0 LOOP3
REGDONE3:
   ${EndIf}

  ; Write the uninstall keys
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth" "DisplayName" "VST MIDI System Synth"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\VST MIDI System Synth"
 ${If} ${RunningX64}
   WriteUninstaller "$WINDIR\SysWow64\vstmididrvuninstall.exe"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth" "UninstallString" '"$WINDIR\SysWow64\vstmididrvuninstall.exe"'
   CreateShortCut "$SMPROGRAMS\VST MIDI System Synth\Uninstall.lnk" "$WINDIR\SysWow64\vstmididrvuninstall.exe" "" "$WINDIR\SysWow64\vstmididrvuninstall.exe" 0
   CreateShortCut "$SMPROGRAMS\VST MIDI System Synth\Configure Driver.lnk" "$WINDIR\SysWow64\vstmididrvcfg.exe" "" "$WINDIR\SysWow64\vstmididrvcfg.exe" 0
   CreateShortCut "$SMPROGRAMS\VST MIDI System Synth\Configure Driver (64-bit).lnk" "$WINDIR\System32\vstmididrvcfg.exe" "" "$WINDIR\System32\vstmididrvcfg.exe" 0
   ${Else}
   WriteUninstaller "$WINDIR\System32\vstmididrvuninstall.exe"
   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth" "UninstallString" '"$WINDIR\System32\vstmididrvuninstall.exe"'
   CreateShortCut "$SMPROGRAMS\VST MIDI System Synth\Uninstall.lnk" "$WINDIR\System32\vstmididrvuninstall.exe" "" "$WINDIR\System32\vstmididrvuninstall.exe" 0
   CreateShortCut "$SMPROGRAMS\VST MIDI System Synth\Configure Driver.lnk" "$WINDIR\System32\vstmididrvcfg.exe" "" "$WINDIR\System32\vstmididrvcfg.exe" 0
   ${EndIf}
   MessageBox MB_OK "Installation complete! Use the driver configuration tool which is in the 'VST MIDI System Synth' program shortcut directory to configure the driver."

SectionEnd
;--------------------------------
; Uninstaller
Section "Uninstall"
   ; Remove registry keys
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
       "MIDI"
  ReadRegStr $1 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDIDRV"
  WriteRegStr HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "$0" "$1"
  DeleteRegKey HKCU "Software\VST MIDI Driver"
  RMDir /r "$SMPROGRAMS\VST MIDI System Synth"
 ${If} ${RunningX64}
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
       "MIDI64"
  ReadRegStr $1 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth\Backup" \
      "MIDIDRV64"
  SetRegView 64
  WriteRegStr HKLM "Software\Microsoft\Windows NT\CurrentVersion\Drivers32" "$0" "$1"
  SetRegView 32
 ${If} ${AtLeastWinVista}
  Delete $WINDIR\SysWow64\vstmididrv.dll
  Delete $WINDIR\SysWow64\vstmididrvuninstall.exe
  Delete $WINDIR\SysWow64\vstmididrvcfg.exe
  Delete $WINDIR\SysNative\vstmididrv.dll
  Delete $WINDIR\SysNative\vstmididrvcfg.exe
${Else}
  MessageBox MB_OK "Note: The uninstaller will reboot your system to remove drivers."
  ${DeleteOnReboot} $WINDIR\SysWow64\vstmididrv.dll
  ${DeleteOnReboot} $WINDIR\SysWow64\vstmididrvuninstall.exe
  ${DeleteOnReboot} $WINDIR\SysWow64\vstmididrvcfg.exe
  Reboot
${Endif}
${Else}
${If} ${AtLeastWinVista}
  Delete $WINDIR\System32\vstmididrv.dll
  Delete $WINDIR\System32\vstmidiuninstall.exe
  Delete $WINDIR\System32\vstmidicfg.exe
${Else}
  MessageBox MB_OK "Note: The uninstaller will reboot your system to remove drivers."
  ${DeleteOnReboot} $WINDIR\System32\vstmididrv.dll
  ${DeleteOnReboot} $WINDIR\System32\vstmididrvuninstall.exe
  ${DeleteOnReboot} $WINDIR\System32\vstmididrvcfg.exe
  Reboot
${Endif}
${EndIf}
DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VST MIDI System Synth"
SectionEnd
