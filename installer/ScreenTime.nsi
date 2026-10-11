;=============================================================================
; ScreenTime (屏幕使用时间管理) - NSIS 安装脚本
;
; 编译方式见同目录 README.md，或直接运行：
;   pwsh -File installer\make_installer.ps1
;
; 本文件必须以 UTF-8 无 BOM 保存，编译时带 /INPUTCHARSET UTF8。
; 可通过 /D 覆盖的宏：VERSION、PRODUCT_VERSION、PAYLOAD_SRC、AWTK_SRC、OUT_FILE
;=============================================================================

Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinMessages.nsh"

;--------------------------------------------------------------- 编译期参数 ---
!ifndef VERSION
  !define VERSION "1.1.0"
!endif
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "1.1.0.0"
!endif
!ifndef PAYLOAD_SRC
  !define PAYLOAD_SRC "D:\Work\ScreenTime"
!endif
!ifndef AWTK_SRC
  !define AWTK_SRC "D:\Work\AWTK\awtk-master"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "${PAYLOAD_SRC}\dist\ScreenTimeSetup-${VERSION}.exe"
!endif

;--------------------------------------------------------------- 产品信息 ----
!define PRODUCT_NAME "屏幕使用时间管理"
!define PRODUCT_PUBLISHER "ScreenTime"
!define APP_EXE "ScreenTime.exe"
!define APP_ARGS "--config"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ScreenTime"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"

Name "${PRODUCT_NAME}"
OutFile "${OUT_FILE}"

; 可选图标：build script 发现 installer\ScreenTime.ico 时会传 /DICON_FILE=...
; 项目里目前没有自带 .ico，所以默认不设置 Icon（使用 NSIS 默认图标）。
!ifdef ICON_FILE
  Icon "${ICON_FILE}"
  UninstallIcon "${ICON_FILE}"
!endif

; 按用户安装（HKCU / %LOCALAPPDATA%），不需要管理员权限
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\ScreenTime"
; 升级安装时复用上次的安装目录（键不存在则用上面的默认值）
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"

ShowInstDetails show
ShowUninstDetails show
ManifestDPIAware true

VIProductVersion "${PRODUCT_VERSION}"
; 简体中文资源
VIAddVersionKey /LANG=2052 "ProductName"      "${PRODUCT_NAME}"
VIAddVersionKey /LANG=2052 "FileDescription"  "${PRODUCT_NAME} 安装程序"
VIAddVersionKey /LANG=2052 "CompanyName"      "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=2052 "LegalCopyright"   "${PRODUCT_PUBLISHER}"
VIAddVersionKey /LANG=2052 "FileVersion"      "${VERSION}"
VIAddVersionKey /LANG=2052 "ProductVersion"   "${VERSION}"
VIAddVersionKey /LANG=2052 "OriginalFilename" "ScreenTimeSetup-${VERSION}.exe"
VIAddVersionKey /LANG=2052 "InternalName"     "ScreenTimeSetup"
; 英文（中性）资源
VIAddVersionKey "ProductName"      "${PRODUCT_NAME}"
VIAddVersionKey "FileDescription"  "${PRODUCT_NAME} Setup"
VIAddVersionKey "CompanyName"      "${PRODUCT_PUBLISHER}"
VIAddVersionKey "LegalCopyright"   "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileVersion"      "${VERSION}"
VIAddVersionKey "ProductVersion"   "${VERSION}"
VIAddVersionKey "OriginalFilename" "ScreenTimeSetup-${VERSION}.exe"
VIAddVersionKey "InternalName"     "ScreenTimeSetup"

;------------------------------------------------------------------ 变量 ------
Var AutoStartCtl        ; 选项页：开机自动启动 复选框句柄
Var RunNowCtl           ; 选项页：安装完成后立即运行 复选框句柄
Var WantAutostart       ; 1 = 写自启键
Var WantRun             ; 1 = 完成页默认勾选“立即运行”
Var IsRunning           ; 1 = 检测到程序正在运行
Var TaskListOut

;--------------------------------------------------------------- 界面文字 -----
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "欢迎使用「${PRODUCT_NAME}」安装向导"
!define MUI_WELCOMEPAGE_TEXT "本向导将引导您完成「${PRODUCT_NAME}」${VERSION} 的安装。$\r$\n$\r$\n程序安装到当前用户目录，不需要管理员权限，也不会影响其他用户。$\r$\n$\r$\n点击「下一步」继续。"

!define MUI_DIRECTORYPAGE_TEXT_TOP "请选择「${PRODUCT_NAME}」的安装位置。$\r$\n$\r$\n默认位置为当前用户的本地应用数据目录；如需修改，请点击「浏览」。"
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "目标文件夹"

!define MUI_FINISHPAGE_TITLE "「${PRODUCT_NAME}」安装完成"
!define MUI_FINISHPAGE_TEXT "「${PRODUCT_NAME}」${VERSION} 已安装到：$\r$\n$INSTDIR$\r$\n$\r$\n首次运行时会自动生成加密的配置文件 config.dat。$\r$\n卸载程序已加入开始菜单：开始菜单 → ScreenTime → 卸载 ${PRODUCT_NAME}。"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "立即运行「${PRODUCT_NAME}」"
!define MUI_FINISHPAGE_RUN_FUNCTION RunApp
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_PAGE_CUSTOMFUNCTION_SHOW FinishPageShow

;------------------------------------------------------------------ 页面 ------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
Page custom OptionsPageCreate OptionsPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; 界面固定为简体中文
!insertmacro MUI_LANGUAGE "SimpChinese"

;============================================================== 自定义函数 ====

; 检测 ScreenTime.exe 是否正在运行（或被占用），结果放入 $IsRunning
; 两种手段：
;   1) 尝试以追加方式打开目标 exe —— 程序在运行时其映像被独占，写入会失败，
;      这正是“覆盖 exe 报 Access denied”的根本原因，最可靠。
;   2) tasklist 查进程名 —— 覆盖从别的目录启动的实例（进程枚举被限制时可能失败，
;      失败只会退化为“不提示”，随后 Section 里的 taskkill 仍会结束它）。
Function CheckRunningApp
  StrCpy $IsRunning 0

  IfFileExists "$INSTDIR\${APP_EXE}" 0 check_tasklist
  ClearErrors
  FileOpen $0 "$INSTDIR\${APP_EXE}" a
  IfErrors 0 lock_ok
  StrCpy $IsRunning 1
  Goto check_tasklist
lock_ok:
  FileClose $0

check_tasklist:
  nsExec::ExecToStack '"$SYSDIR\tasklist.exe" /FI "IMAGENAME eq ${APP_EXE}" /NH /FO CSV'
  Pop $0                 ; nsExec 退出码
  Pop $TaskListOut       ; 命令输出
  StrCpy $1 $TaskListOut 1
  ; 找到进程时 CSV 输出以引号开头，未找到时是本地化的提示文本
  ${If} $1 == '"'
    StrCpy $IsRunning 1
  ${EndIf}
FunctionEnd

; 强制结束正在运行的程序（失败也无所谓）
Function KillApp
  nsExec::ExecToStack '"$SYSDIR\taskkill.exe" /F /IM ${APP_EXE}'
  Pop $0
  Pop $1
  Sleep 600
FunctionEnd

; 启动主程序
Function RunApp
  Exec '"$INSTDIR\${APP_EXE}" ${APP_ARGS} "$INSTDIR\config.dat"'
FunctionEnd

;----------------------------------------------------------- 安装选项页 -----
Function OptionsPageCreate
  ${If} $WantAutostart == ""
    StrCpy $WantAutostart ${BST_CHECKED}
  ${EndIf}
  ${If} $WantRun == ""
    StrCpy $WantRun ${BST_CHECKED}
  ${EndIf}

  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 24u "请选择安装选项："
  Pop $0

  ${NSD_CreateCheckBox} 0 30u 100% 12u "开机自动启动（登录 Windows 后自动运行）"
  Pop $AutoStartCtl
  ${NSD_SetState} $AutoStartCtl $WantAutostart

  ${NSD_CreateCheckBox} 0 48u 100% 12u "安装完成后立即运行"
  Pop $RunNowCtl
  ${NSD_SetState} $RunNowCtl $WantRun

  ${NSD_CreateLabel} 0 76u 100% 60u "说明：$\r$\n· 开机自动启动会写入当前用户的注册表启动项（HKCU），随时可在上面的复选框或系统设置中取消。$\r$\n· 安装程序会保留已有的 config.dat（个人配置），不会覆盖您的使用记录。"

  nsDialogs::Show
FunctionEnd

Function OptionsPageLeave
  ${NSD_GetState} $AutoStartCtl $WantAutostart
  ${NSD_GetState} $RunNowCtl $WantRun
FunctionEnd

; 完成页：让“立即运行”复选框跟随选项页的选择
Function FinishPageShow
  ${If} $WantRun == ${BST_CHECKED}
    SendMessage $mui.FinishPage.Run ${BM_SETCHECK} ${BST_CHECKED} 0
  ${Else}
    SendMessage $mui.FinishPage.Run ${BM_SETCHECK} ${BST_UNCHECKED} 0
  ${EndIf}
FunctionEnd

;--------------------------------------------------------------- 初始化 -----
Function .onInit
  ; 静默安装时使用默认选项：自启 + 安装后运行
  StrCpy $WantAutostart ${BST_CHECKED}
  StrCpy $WantRun ${BST_CHECKED}

  ; 安装前必须先结束正在运行的程序，否则 exe 被占用会覆盖失败
  Call CheckRunningApp
  StrCmp $IsRunning "1" oninit_running oninit_done

oninit_running:
  IfSilent oninit_silent
  MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "检测到「${PRODUCT_NAME}」正在运行（或程序文件正被占用）。$\r$\n$\r$\n安装程序需要先结束该程序，否则无法覆盖 ${APP_EXE}。$\r$\n$\r$\n点击「确定」结束该程序并继续安装；点击「取消」退出安装。" /SD IDOK IDCANCEL oninit_cancel
  Call KillApp
  Goto oninit_done

oninit_silent:
  Call KillApp
  Goto oninit_done

oninit_cancel:
  Abort

oninit_done:
FunctionEnd

;================================================================ 安装 ========
Section "${PRODUCT_NAME}" SecMain
  SetShellVarContext current      ; 开始菜单使用当前用户
  SetOverwrite on

  ; 再兜底结束一次（用户可能在向导期间又启动了程序）
  Call KillApp

  DetailPrint "正在安装程序文件..."
  SetOutPath "$INSTDIR"
  File "${PAYLOAD_SRC}\${APP_EXE}"
  File "${AWTK_SRC}\build_mingw\awtk.dll"
  File "${AWTK_SRC}\build_mingw\third_party\SDL2\SDL2.dll"

  DetailPrint "正在安装界面资源..."
  SetOutPath "$INSTDIR\res\assets\default\raw\styles"
  File "${AWTK_SRC}\res\assets\default\raw\styles\default.bin"

  SetOutPath "$INSTDIR\res\assets\default\raw\images\x1"
  File /nonfatal "${AWTK_SRC}\res\assets\default\raw\images\x1\cursor*.png"
  SetOutPath "$INSTDIR\res\assets\default\raw\images\x2"
  File /nonfatal "${AWTK_SRC}\res\assets\default\raw\images\x2\cursor*.png"
  SetOutPath "$INSTDIR\res\assets\default\raw\images\x3"
  File /nonfatal "${AWTK_SRC}\res\assets\default\raw\images\x3\cursor*.png"
  SetOutPath "$INSTDIR\res\assets\default\raw\images\x4"
  File /nonfatal "${AWTK_SRC}\res\assets\default\raw\images\x4\cursor*.png"

  ; 卸载程序
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; ------------------------------------------------------- 开始菜单快捷方式
  DetailPrint "正在创建开始菜单快捷方式..."
  CreateDirectory "$SMPROGRAMS\ScreenTime"
  CreateShortCut "$SMPROGRAMS\ScreenTime\ScreenTime.lnk" "$INSTDIR\${APP_EXE}" \
                 '${APP_ARGS} "$INSTDIR\config.dat"' "$INSTDIR\${APP_EXE}" 0 SW_SHOWNORMAL
  CreateShortCut "$SMPROGRAMS\ScreenTime\卸载 ScreenTime.lnk" "$INSTDIR\uninstall.exe" \
                 "" "$INSTDIR\uninstall.exe" 0 SW_SHOWNORMAL

  ; ------------------------------------------------------------- 开机自启动
  DetailPrint "正在配置启动项..."
  ; 先删掉旧值，再按用户选择决定是否写入
  DeleteRegValue HKCU "${RUN_KEY}" "ScreenTime"
  ${If} $WantAutostart == ${BST_CHECKED}
    WriteRegStr HKCU "${RUN_KEY}" "ScreenTime" \
                '"$INSTDIR\${APP_EXE}" ${APP_ARGS} "$INSTDIR\config.dat"'
  ${EndIf}

  ; --------------------------------------------------------- 卸载注册信息
  DetailPrint "正在写入卸载信息..."
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr   HKCU "${UNINST_KEY}" "Publisher"       "${PRODUCT_PUBLISHER}"
  WriteRegStr   HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr   HKCU "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
  WriteRegStr   HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr   HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify"        1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair"        1

  ; 已安装大小（KB）
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" $0

  DetailPrint "安装完成。"
SectionEnd

;=============================================================== 卸载 =========
Function un.onInit
  SetShellVarContext current
FunctionEnd

Section "Uninstall"
  ; 卸载前先结束正在运行的程序
  nsExec::ExecToStack '"$SYSDIR\taskkill.exe" /F /IM ${APP_EXE}'
  Pop $0
  Pop $1
  Sleep 600

  ; 询问是否删除个人数据
  MessageBox MB_YESNO|MB_ICONQUESTION "是否同时删除截图、日志和配置？$\r$\n$\r$\n选择「是」：删除整个安装目录，包括 screenshots、logs 和 config.dat。$\r$\n选择「否」：只删除程序文件，保留 screenshots、logs 和 config.dat。" /SD IDNO IDYES un_full_removal

  ; ---- 选择「否」：只删程序文件，保留个人数据 ----
  DetailPrint "正在删除程序文件（保留个人数据）..."
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\awtk.dll"
  Delete "$INSTDIR\SDL2.dll"
  RMDir /r "$INSTDIR\res"
  Delete "$INSTDIR\uninstall.exe"
  Goto un_cleanup

  ; ---- 选择「是」：连个人数据一起删除 ----
un_full_removal:
  DetailPrint "正在删除整个安装目录..."
  RMDir /r "$INSTDIR"

un_cleanup:
  DetailPrint "正在清理注册表与快捷方式..."
  DeleteRegValue HKCU "${RUN_KEY}" "ScreenTime"
  DeleteRegKey  HKCU "${UNINST_KEY}"
  RMDir /r "$SMPROGRAMS\ScreenTime"

  ; 目录已空则删除（保留数据时该目录仍存在）
  RMDir "$INSTDIR"

  ; 通知资源管理器刷新图标
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
