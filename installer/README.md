# ScreenTime 安装包（Inno Setup 7）

本目录只负责 **打包 / 安装 / 卸载**，不包含任何 C 源码。程序本体在上一级目录（`src\`、`ScreenTime.exe`）。

## 文件

| 文件 | 说明 |
|---|---|
| `installer\ScreenTime.iss` | Inno Setup 脚本（**UTF-8 带 BOM**，中文界面；改这个文件改安装包行为） |
| `installer\make_installer.ps1` | 打包脚本（**纯 ASCII**，见下面"几个坑"） |
| `installer\ScreenTime.ico` | 可选：放一个图标进来，安装程序/快捷方式就会用它（没有就用 Inno 默认图标） |
| `dist\ScreenTimeSetup-<版本>.exe` | 安装包产物 |
| `dist\ScreenTime-<版本>-portable.zip` | 免安装绿色包（exe + 2 个 DLL + 主题 + 指针） |

工具位置：`C:\Program Files\Inno Setup 7\ISCC.exe`（命令行编译器，7.1.0）。
中文界面用的是 Inno 自带的 `Languages\ChineseSimplified.isl`，不需要额外下载语言包。

## 默认包：一个包自适应 GL / 软件渲染

`-Backend auto`（默认）打出来的包装好后是：

```
{app}\ScreenTime.exe         启动器（探测 OpenGL，然后拉起 gl 或 soft 后端）
{app}\gl\ScreenTime.exe      AWTK OpenGL 构建
{app}\soft\ScreenTime.exe    AWTK 软件渲染构建（没有 OpenGL 时用）
{app}\config.dat / logs\ / screenshots\    两个后端共用
```

```powershell
# 默认：合体包（缺的后端 exe / 启动器会自动编译）
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1
# 只带一个后端的包（排查或特殊场合用）
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1 -Backend gl   -PayloadExe out\gl\ScreenTime.exe
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1 -Backend soft -PayloadExe out\soft\ScreenTime.exe
```

`-Backend auto` 需要 `out\awtk_soft\awtk.dll`（先跑 `.\build_awtk_soft.ps1`）。
三种包的 `AppId` 相同、都装到同一目录，所以互相覆盖安装就是普通升级，个人数据不受影响；
`.iss` 的 `[InstallDelete]` 会顺手清掉另一种布局的残留（根目录的 `awtk.dll`/`res`，或 `gl\`/`soft\`）。

## 怎么打包

```powershell
cd D:\Work\ScreenTime
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1
# 只要安装包、不要绿色包：
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1 -SkipPortable
```

脚本做的事：

1. 检查 `ISCC.exe` 是否存在、`.iss` 是否存在；
2. 读 `VERSION`（读不到用脚本里的默认值），换算成 `X.X.X.Y` 供版本资源使用；
3. 检查 payload 是否齐全：`ScreenTime.exe`、`awtk.dll`、`SDL2.dll`、`default.bin`、
   `images\x1..x4\cursor*.png`（缺一个就报错退出）；确认**没有**把 `config.ini`/`config.dat` 打进去；
4. 检查 `.iss` 是否是 **UTF-8 带 BOM**；**不带 BOM 就自动生成一份带 BOM 的副本再编译**
   （在 `build\ScreenTime.generated.iss`），不会因为编辑器把 BOM 吃掉而打包失败；
5. 调 ISCC 编译，非 0 退出码直接报错；
6. **校验包内载荷**：解析 ISCC 的 `Compressing: <文件>` 输出，确认 4 个程序文件 + 12 个指针图都在、
   且没有混进任何配置文件；
7. 打绿色包（`Compress-Archive`），并列出压缩包里的条目；
8. 打印产物完整路径、大小，最后一行打印**可直接复制的 ISCC 命令**。

等价的 ISCC 命令（版本 1.1.2 时）：

```powershell
& 'C:\Program Files\Inno Setup 7\ISCC.exe' `
    /DAppVersion=1.1.2 /DAppVersion4=1.1.2.0 `
    /DPayloadDir=D:\Work\ScreenTime `
    /DAwtkDir=D:\Work\AWTK\awtk-master `
    /OD:\Work\ScreenTime\dist `
    D:\Work\ScreenTime\installer\ScreenTime.iss
```

`/D` 定义会覆盖 `.iss` 顶部的默认值；`/O` 指定输出目录，`/F` 指定输出文件名（默认由
`OutputBaseFilename` 决定）。

## 安装包行为

* **按用户安装，不需要管理员权限**（`PrivilegesRequired=lowest`），默认装到 `%LOCALAPPDATA%\ScreenTime`；
  升级时沿用上次的目录（`UsePreviousAppDir=yes`）。
* **只装程序本体 + 界面资源**：`ScreenTime.exe`、`awtk.dll`、`SDL2.dll`、
  `res\assets\default\raw\styles\default.bin`、`res\...\images\xN\cursor*.png`。
  不带任何配置文件——`config.dat` 由程序首次运行自己生成；**升级不会动**
  `screenshots\`、`logs\`、`config.dat`、`config.ini.bak`。
* **附加选项**（都可以不选）：`开机时自动启动`（默认勾选，写
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\ScreenTime`，卸载时自动删除）、
  `创建桌面快捷方式`（默认不勾）。
* **开始菜单**：`ScreenTime` 组下有「屏幕使用时间管理」和「卸载 屏幕时间管理」两个快捷方式。
* **覆盖前先结束正在运行的程序**：`[Code]` 里用 `Exec` + `ewWaitUntilTerminated` 调
  `taskkill /F /IM ScreenTime.exe`（**不用任何管道/输出重定向**，受限环境里也不会卡住）。
* **卸载**：交互式卸载会问一次"是否同时删除截图、日志和配置"（默认按钮是「否」）；
  **静默卸载一律保留**个人数据。
* 装完的完成页可以勾「立即运行」。

### 静默安装 / 卸载

```powershell
# 静默安装（加 /LOG= 就有完整日志，排查问题时很有用）
.\dist\ScreenTimeSetup-1.1.2.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LANG=chinese `
    /LOG="D:\Work\ScreenTime\build\inno_install.log"

# 换目录、不要自启、不要快捷方式
.\dist\ScreenTimeSetup-1.1.2.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART `
    /DIR="D:\some\dir" /NOICONS /TASKS=""

# 静默卸载（保留个人数据）
& "$env:LOCALAPPDATA\ScreenTime\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

常用开关：`/VERYSILENT`（完全无界面）、`/SILENT`（只有进度）、`/SUPPRESSMSGBOXES`、
`/NORESTART`、`/LANG=chinese`、`/DIR="..."`、`/TASKS="autostart"`、`/NOICONS`、`/LOG="..."`。

### 只在验证时用的编译开关

`/DNoUninstallKey` 会设 `Uninstallable=no`：**不写 HKCU 的卸载登记表项、也不生成卸载程序**，
用于在受限环境里只验证"包能否正确解包"。正常打包**不要**加。

## 已验证 / 未验证

* ✅ 打包产物内容正确：解析 ISCC 输出确认 4 个程序文件 + 12 个指针图都打进包里，且不含任何配置/日志。
* ✅ 解包正确：用 `/VERYSILENT /DIR=<测试目录>` 装到临时目录后，逐个文件与源文件**逐字节一致**
  （`ScreenTime.exe`、`awtk.dll`、`SDL2.dll`、`default.bin`），`cursor*.png` 12 个齐全，
  Inno 日志里是 `Installation process succeeded`。
* ✅ 卸载保留数据（`UninstallSilent` 时直接跳过删数据的分支，不会误删）。
* ⚠️ **注册表项与开始菜单快捷方式没有在自动化环境里验证过**：本机这套开发环境给进程下发的是
  低完整性令牌，安装程序写 `HKCU\...\Uninstall\{GUID}_is1` 会被系统拒绝（日志里
  `RegCreateKeyEx 失败；错误代码 5`），Inno 于是正确回滚。**这是环境限制，不是包的问题**——
  正常双击运行（explorer 启动，正常完整性）不会有这个问题。

## 几个坑

* **`.iss` 必须 UTF-8 带 BOM**：里面有中文（`AppName`、自定义消息），没有 BOM 时 Inno 会按 ANSI
  解码，向导里就是乱码。打包脚本会在缺失时自动用带 BOM 的副本编译。
* **`make_installer.ps1` 必须纯 ASCII**：Windows PowerShell 5.1 会把无 BOM 的文件按 ANSI 解码，
  脚本里写中文注释可能因为乱码产生续行反引号，把下一行代码吞进注释（踩过）。
* **不要用 `nsExec` 这类靠命名管道的插件**（早期 NSIS 版踩过）：受限环境下插件取输出会失败，
  甚至让安装程序静默退出。Inno 的 `Exec`/`ExecWait` 不需要管道。
* **`AppId` 不要改**：`{{8F3B6C1E-...}` 决定"这是同一个程序"，改了会变成两套安装记录。
* **旧的 NSIS 脚本已删除**（`installer\ScreenTime.nsi`）：需要时用
  `git show <commit>:installer/ScreenTime.nsi` 从历史里取。
