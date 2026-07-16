Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifndef STAGE_DIR
!error "STAGE_DIR is required"
!endif
!ifndef OUTPUT_DIR
!error "OUTPUT_DIR is required"
!endif

!include "MUI2.nsh"
!define PRODUCT_NAME "Forest 专注森林"
!define PRODUCT_VERSION "1.1.0"
!define PRODUCT_REGKEY "Software\ForestFocus"
!define UNINSTALL_REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ForestFocus"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_DIR}\ForestFocus_Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\ForestFocus"
InstallDirRegKey HKCU "${PRODUCT_REGKEY}" "InstallDir"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Section "安装主程序" SecMain
    SetOutPath "$INSTDIR\app"
    File /r "${STAGE_DIR}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "${PRODUCT_REGKEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    SetShellVarContext current
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\app\forest.exe"
SectionEnd

Section "Uninstall"
    SetShellVarContext current
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR\app"
    DeleteRegKey HKCU "${UNINSTALL_REGKEY}"
    DeleteRegKey HKCU "${PRODUCT_REGKEY}"
    RMDir "$INSTDIR"
SectionEnd
