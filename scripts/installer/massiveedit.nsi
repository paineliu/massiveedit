Unicode true
!include "MUI2.nsh"

!ifndef APP_NAME
  !define APP_NAME "MassiveEdit"
!endif

!ifndef APP_VERSION
  !define APP_VERSION "dev"
!endif

!ifndef APP_STAGE
  !error "APP_STAGE is required. Pass /DAPP_STAGE=<staged-install-dir>"
!endif

!ifndef OUT_FILE
  !error "OUT_FILE is required. Pass /DOUT_FILE=<installer-output-path>"
!endif

!ifndef APP_ICON
  !define APP_ICON "..\..\resources\icons\AppIcon.ico"
!endif

Name "${APP_NAME}"
OutFile "${OUT_FILE}"
InstallDir "$ProgramFiles64\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
Icon "${APP_ICON}"
UninstallIcon "${APP_ICON}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

LangString MsgAppRunningInstall ${LANG_SIMPCHINESE} "${APP_NAME} 正在运行。请先关闭程序，然后点击“重试”继续安装。"
LangString MsgAppRunningInstall ${LANG_ENGLISH} "${APP_NAME} is currently running. Please close it, then click Retry to continue setup."
LangString MsgAppRunningUninstall ${LANG_SIMPCHINESE} "${APP_NAME} 正在运行。请先关闭程序，然后点击“重试”继续卸载。"
LangString MsgAppRunningUninstall ${LANG_ENGLISH} "${APP_NAME} is currently running. Please close it, then click Retry to continue uninstall."

Function CheckAppRunning
  nsExec::ExecToStack 'cmd /C tasklist /FI "IMAGENAME eq massiveedit.exe" /FO CSV /NH | find /I "massiveedit.exe" >nul'
  Pop $0
  Pop $1
  StrCmp $0 "0" 0 not_running
  StrCpy $0 "1"
  Return
not_running:
  StrCpy $0 "0"
FunctionEnd

Function EnsureAppNotRunning
check_loop:
  Call CheckAppRunning
  StrCmp $0 "1" 0 done
  MessageBox MB_ICONEXCLAMATION|MB_RETRYCANCEL "$(MsgAppRunningInstall)" IDRETRY check_loop IDCANCEL cancel
cancel:
  Abort
done:
FunctionEnd

Function un.CheckAppRunning
  nsExec::ExecToStack 'cmd /C tasklist /FI "IMAGENAME eq massiveedit.exe" /FO CSV /NH | find /I "massiveedit.exe" >nul'
  Pop $0
  Pop $1
  StrCmp $0 "0" 0 un_not_running
  StrCpy $0 "1"
  Return
un_not_running:
  StrCpy $0 "0"
FunctionEnd

Function un.EnsureAppNotRunning
un_check_loop:
  Call un.CheckAppRunning
  StrCmp $0 "1" 0 un_done
  MessageBox MB_ICONEXCLAMATION|MB_RETRYCANCEL "$(MsgAppRunningUninstall)" IDRETRY un_check_loop IDCANCEL un_cancel
un_cancel:
  Abort
un_done:
FunctionEnd

Function .onInit
  Call EnsureAppNotRunning
FunctionEnd

Function un.onInit
  Call un.EnsureAppNotRunning
FunctionEnd

Section "Install"
  SetShellVarContext all
  SetOutPath "$INSTDIR"
  File /r "${APP_STAGE}\*.*"

  ; Start Menu root entry only, no folder, no extra uninstall shortcut.
  CreateShortCut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\bin\massiveedit.exe" "" "$INSTDIR\bin\massiveedit.exe" 0

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "Publisher" "${APP_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
  DeleteRegKey HKLM "Software\${APP_NAME}"
SectionEnd
