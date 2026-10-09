Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!ifndef PRODUCT_VERSION
!error "Build with scripts/installer/build_installer.ps1"
!endif
!ifdef VERIFICATION_ID
!define PRODUCT_NAME "Forest 专注森林 Verification-${VERIFICATION_ID}"
!define PRODUCT_REGKEY "Software\ForestFocus-Verification-${VERIFICATION_ID}"
!define UNINSTALL_REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ForestFocus-Verification-${VERIFICATION_ID}"
!define OWNER_MARKER ".forest-install-verification-${VERIFICATION_ID}"
!define OWNER_MARKER_VALUE "ForestFocus-Installed-Verification-${VERIFICATION_ID}-v1"
!else
!define PRODUCT_NAME "Forest 专注森林"
!define PRODUCT_REGKEY "Software\ForestFocus"
!define UNINSTALL_REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ForestFocus"
!define OWNER_MARKER ".forest-install"
!define OWNER_MARKER_VALUE "ForestFocus-Installed-v1"
!endif
Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"
!ifdef VERIFICATION_ID
InstallDir "$LOCALAPPDATA\Programs\ForestFocus-Verification-${VERIFICATION_ID}"
!else
InstallDir "$LOCALAPPDATA\Programs\ForestFocus"
!endif
InstallDirRegKey HKCU "${PRODUCT_REGKEY}" "InstallDir"
ShowInstDetails show
ShowUninstDetails show
Var ValidateMode
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\forest.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动 Forest 专注森林"
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE CheckDestination
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

!macro PathGuard PREFIX
Function ${PREFIX}CheckPath
    Exch $0
    Push $1
    Push $2
    path_loop:
    System::Call 'kernel32::GetFileAttributesW(w r0)i .r1'
    IntCmp $1 -1 path_parent
    IntOp $1 $1 & 0x400
    IntCmp $1 0 path_parent path_parent path_rejected
    path_parent:
    ${GetParent} $0 $2
    StrCmp $2 "" path_done
    StrCmp $2 $0 path_done
    StrCpy $0 $2
    Goto path_loop
    path_rejected:
    IfSilent +2
    MessageBox MB_ICONSTOP "安装路径包含链接或重解析点。请选择普通的独立目录。"
    SetErrorLevel 2
    Abort
    path_done:
    Pop $2
    Pop $1
    Pop $0
FunctionEnd
!macroend
!insertmacro PathGuard ""
!insertmacro PathGuard "un."

Function .onInit
    StrCpy $ValidateMode 0
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/VALIDATE" $1
    IfErrors +2
    StrCpy $ValidateMode 1
FunctionEnd

Function CheckDestination
    System::Call 'kernel32::GetFullPathNameW(w "$INSTDIR", i ${NSIS_MAX_STRLEN}, w .r0, p 0) i .r1'
    IntCmp $1 0 invalid_destination invalid_destination
    IntCmp $1 ${NSIS_MAX_STRLEN} invalid_destination 0 invalid_destination
    StrCpy $INSTDIR $0
    ${GetRoot} $INSTDIR $0
    StrCmp $INSTDIR $0 invalid_destination
    StrCmp $INSTDIR "$0\" invalid_destination
    StrCmp $INSTDIR "$WINDIR" invalid_destination
    StrCmp $INSTDIR "$PROGRAMFILES" invalid_destination
    StrCmp $INSTDIR "$LOCALAPPDATA" invalid_destination
    StrCmp $INSTDIR "$LOCALAPPDATA\Forest" invalid_destination
    StrLen $0 "$LOCALAPPDATA\Forest\"
    StrCpy $1 $INSTDIR $0
    StrCmp $1 "$LOCALAPPDATA\Forest\" invalid_destination
    Push $INSTDIR
    Call CheckPath
    Push "$INSTDIR\${OWNER_MARKER}"
    Call CheckPath
    IfFileExists "$INSTDIR\forest.portable" invalid_destination
    IfFileExists "$INSTDIR\${OWNER_MARKER}" known_install
    FindFirst $0 $1 "$INSTDIR\*"
    empty_check:
    StrCmp $1 "" empty_done
    StrCmp $1 "." empty_next
    StrCmp $1 ".." empty_next
    FindClose $0
    Goto invalid_destination
    empty_next:
    FindNext $0 $1
    Goto empty_check
    empty_done:
    FindClose $0
    Return
    known_install:
    FileOpen $0 "$INSTDIR\${OWNER_MARKER}" r
    FileRead $0 $1
    FileClose $0
    StrCmp $1 "${OWNER_MARKER_VALUE}$\r$\n" check_running invalid_destination
    check_running:
    Push "$INSTDIR\forest.exe"
    Call CheckPath
    IfFileExists "$INSTDIR\forest.exe" +2
    Goto invalid_destination
    ClearErrors
    FileOpen $0 "$INSTDIR\forest.exe" a
    IfErrors invalid_destination
    FileClose $0
    Return
    invalid_destination:
    IfSilent +2
    MessageBox MB_ICONSTOP "请选择空目录或本安装器创建的 Forest 目录，并先退出应用。不能覆盖便携版、用户数据目录或其他文件。"
    SetErrorLevel 2
    Abort
FunctionEnd

Section "安装主程序"
    Call CheckDestination
    ClearErrors
    !include "${INPUTS_DIR}\install-files.nsh"
    IfErrors install_failed
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    IfErrors install_failed
    FileOpen $0 "$INSTDIR\${OWNER_MARKER}" w
    IfErrors install_failed
    FileWrite $0 "${OWNER_MARKER_VALUE}$\r$\n$ValidateMode$\r$\n"
    FileClose $0
    IfErrors install_failed
    StrCmp $ValidateMode 1 install_done
    WriteRegStr HKCU "${PRODUCT_REGKEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REGKEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKCU "${UNINSTALL_REGKEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_REGKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKCU "${UNINSTALL_REGKEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_REGKEY}" "NoRepair" 1
    SetShellVarContext current
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\forest.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\卸载.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\forest.exe"
    Goto install_done
    install_failed:
    SetErrorLevel 3
    Abort
    install_done:
SectionEnd

Function un.onInit
    Push $INSTDIR
    Call un.CheckPath
    Push "$INSTDIR\${OWNER_MARKER}"
    Call un.CheckPath
    FileOpen $0 "$INSTDIR\${OWNER_MARKER}" r
    FileRead $0 $1
    FileRead $0 $ValidateMode
    FileClose $0
    StrCmp $1 "${OWNER_MARKER_VALUE}$\r$\n" +3
    SetErrorLevel 2
    Abort
FunctionEnd

Section "Uninstall"
    !include "${INPUTS_DIR}\uninstall-files.nsh"
    StrCmp $ValidateMode "1$\r$\n" keep_registration
    ReadRegStr $0 HKCU "${PRODUCT_REGKEY}" "InstallDir"
    StrCmp $0 $INSTDIR 0 keep_registration
    SetShellVarContext current
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\卸载.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
    DeleteRegKey HKCU "${UNINSTALL_REGKEY}"
    DeleteRegKey HKCU "${PRODUCT_REGKEY}"
    keep_registration:
    Delete "$INSTDIR\${OWNER_MARKER}"
    Delete "$INSTDIR\Uninstall.exe"
    DetailPrint "仅移除安装清单中的文件；用户数据与其他文件已保留。"
    RMDir "$INSTDIR"
SectionEnd
