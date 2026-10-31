; ============================================================================
;  ScreenTime —— Inno Setup 7 打包脚本
;
;  编译（在仓库根目录执行，或用 installer\make_installer.ps1）：
;    "C:\Program Files\Inno Setup 7\ISCC.exe" ^
;        /DAppVersion=1.1.1 /DAppVersion4=1.1.1.0 ^
;        /DPayloadDir=D:\Work\ScreenTime ^
;        /DAwtkDir=D:\Work\AWTK\awtk-master ^
;        /O..\dist installer\ScreenTime.iss
;
;  特点（与之前 NSIS 版对齐）：
;   * 按用户安装，不需要管理员权限（PrivilegesRequired=lowest）
;   * 默认装到 %LOCALAPPDATA%\ScreenTime，升级时沿用旧目录
;   * 只装程序本体与界面资源，不含任何配置文件
;     （config.dat 由程序首次运行自己生成；升级不会动 screenshots\logs\config.dat）
;   * 可选「开机自启」（写 HKCU\...\Run）与「安装完立即运行」
;   * 卸载默认保留个人数据，交互式卸载会问一次；静默卸载一律保留
;   * 覆盖前用 taskkill 结束正在运行的程序（ExecWait，不用管道）
; ============================================================================

#ifndef AppVersion
  #define AppVersion "1.1.6"
#endif
#ifndef AppVersion4
  #define AppVersion4 "1.1.6.0"
#endif
#ifndef PayloadDir
  #define PayloadDir "D:\Work\ScreenTime"
#endif
#ifndef AwtkDir
  #define AwtkDir "D:\Work\AWTK\awtk-master"
#endif
; AWTK 的构建目录：GL 版是 build_mingw，软件渲染版（无 OpenGL 的机器用）在仓库的 out\awtk_soft。
; 由 make_installer.ps1 通过 /DAwtkBuild=... 传进来；这里给个默认值保证手工调 ISCC 也能编译。
#ifndef AwtkBuild
  #define AwtkBuild AwtkDir + "\build_mingw"
#endif
; 合体包（/DBundle=1 + /DBundleDir=<staging>）：一个包里同时放 gl\ 和 soft\ 两套后端 +
; 启动器，安装后由启动器在运行时探测 OpenGL 选择后端。
#ifndef BundleDir
  #define BundleDir ""
#endif

#define AppName "屏幕使用时间管理"
#define AppShortName "ScreenTime"
#define AppPublisher "Livens"
#define AppExe "ScreenTime.exe"

[Setup]
; AppId 决定「同一个程序」的识别，升级/卸载都靠它；不要随便改
AppId={{8F3B6C1E-6E1B-4A2E-9C7D-2A5B7C4E9F31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion4}
VersionInfoDescription={#AppName} 安装程序
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

; ---- 安装位置与权限：按用户安装，不需要管理员 ----
DefaultDirName={localappdata}\ScreenTime
DefaultGroupName={#AppShortName}
UsePreviousAppDir=yes
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
AllowNoIcons=yes

; ---- 系统要求：64 位（awtk.dll / SDL2.dll 是 64 位）----
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1sp1

; ---- 产物 ----
OutputDir=..\dist
OutputBaseFilename=ScreenTimeSetup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
DisableWelcomePage=no
SetupLogging=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
CloseApplications=yes
RestartApplications=no
; 程序自己在跑时用这个名字的互斥体（见 src/main.c），Inno 会据此提示关闭
AppMutex=Local\ScreenTimeSingleInstance

; 测试用开关：加上 /DNoUninstallKey 编译时不写卸载登记表项（HKCU\...\Uninstall\...），
; 用于在受限环境里验证"包能否正确解包"；正常打包不要加这个开关。
#ifdef NoUninstallKey
Uninstallable=no
#endif

; 有图标就用图标（把 ScreenTime.ico 放到 installer\ 下即可），没有就用 Inno 默认图标
#if FileExists(AddBackslash(SourcePath) + "ScreenTime.ico")
SetupIconFile=ScreenTime.ico
#endif

[Languages]
Name: "chinese"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "开机时自动启动（仅当前用户，写 HKCU 的 Run 项）"; \
      GroupDescription: "附加选项："; Flags: checkedonce
Name: "desktopicon"; Description: "创建桌面快捷方式"; \
      GroupDescription: "附加选项："; Flags: unchecked

[Files]
#ifdef Bundle
; 合体包：{app}\ScreenTime.exe 是启动器，gl\ 和 soft\ 各放一套后端（由启动器运行时选择）
Source: "{#BundleDir}\*"; DestDir: "{app}"; \
        Flags: ignoreversion recursesubdirs createallsubdirs
#else
; 单后端包：程序本体
Source: "{#PayloadDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AwtkBuild}\awtk.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AwtkBuild}\third_party\SDL2\SDL2.dll"; DestDir: "{app}"; Flags: ignoreversion
; AWTK 界面资源（主题 + 鼠标指针；指针文件缺失不报错）
Source: "{#AwtkDir}\res\assets\default\raw\styles\default.bin"; \
        DestDir: "{app}\res\assets\default\raw\styles"; Flags: ignoreversion
Source: "{#AwtkDir}\res\assets\default\raw\images\x1\cursor*.png"; \
        DestDir: "{app}\res\assets\default\raw\images\x1"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#AwtkDir}\res\assets\default\raw\images\x2\cursor*.png"; \
        DestDir: "{app}\res\assets\default\raw\images\x2"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#AwtkDir}\res\assets\default\raw\images\x3\cursor*.png"; \
        DestDir: "{app}\res\assets\default\raw\images\x3"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#AwtkDir}\res\assets\default\raw\images\x4\cursor*.png"; \
        DestDir: "{app}\res\assets\default\raw\images\x4"; Flags: ignoreversion skipifsourcedoesntexist
#endif

[InstallDelete]
#ifdef Bundle
; 从旧版"单后端"布局升级上来时，根目录的旧后端文件留着只会混淆，装前清掉
Type: files; Name: "{app}\awtk.dll"
Type: files; Name: "{app}\SDL2.dll"
Type: filesandordirs; Name: "{app}\res"
#else
; 从合体包降到单后端包时，清掉不再使用的后端子目录
Type: filesandordirs; Name: "{app}\gl"
Type: filesandordirs; Name: "{app}\soft"
#endif

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; \
      Parameters: "--config ""{app}\config.dat"""; WorkingDir: "{app}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; \
      Parameters: "--config ""{app}\config.dat"""; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; 开机自启（只写当前用户，卸载时自动删掉这个值）
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
      ValueType: string; ValueName: "ScreenTime"; \
      ValueData: """{app}\{#AppExe}"" --config ""{app}\config.dat"""; \
      Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--config ""{app}\config.dat"""; \
          WorkingDir: "{app}"; Description: "立即运行 {#AppName}"; \
          Flags: nowait postinstall skipifsilent

[Code]
{ 覆盖文件前先结束正在运行的程序。用 Exec + ewWaitUntilTerminated，
  不经过任何管道/输出重定向，避免受限环境下卡住。 }
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#AppExe}', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
  NeedsRestart := False;
  Result := '';
end;

{ 卸载：默认保留个人数据；交互式卸载问一次，静默卸载直接保留。 }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    if UninstallSilent then
      Exit;
    if MsgBox('是否同时删除截图、日志和配置？' + #13#10 + #13#10 +
              '选择「是」：删除整个安装目录（包括 screenshots、logs、config.dat）。' + #13#10 +
              '选择「否」：只删除程序文件，保留 screenshots、logs 和 config.dat。',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      DataDir := ExpandConstant('{app}');
      DelTree(DataDir, True, True, True);
    end;
  end;
end;
