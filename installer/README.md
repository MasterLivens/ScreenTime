# ScreenTime 安装包（NSIS）

本目录只负责 **打包 / 安装 / 卸载**，不包含任何 C 源码。程序本体在上一级目录（`src\`、`ScreenTime.exe`）。

## 目录内容

| 文件 | 说明 |
|---|---|
| `installer\ScreenTime.nsi` | NSIS 安装脚本（**UTF-8 无 BOM**，中文界面，必须用 `/INPUTCHARSET UTF8` 编译） |
| `installer\make_installer.ps1` | 打包脚本（**纯 ASCII**，PowerShell 5.1 可直接跑；所有输出为英文） |
| `dist\ScreenTimeSetup-<版本>.exe` | 安装包产物（默认安装到 `%LOCALAPPDATA%\ScreenTime`） |
| `dist\ScreenTime-<版本>-portable.zip` | 免安装便携版（exe + dll + res，解压即用；配置文件 `config.dat` 会在首次运行时自动生成） |

## 打包（重新打包）

C 代码改完、`ScreenTime.exe` 重新编译好之后，在项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer\make_installer.ps1
```

可选参数：

```powershell
# 只做安装包，不做便携版 zip
powershell -NoProfile -ExecutionPolicy Bypass -File installer\make_installer.ps1 -SkipPortable
```

脚本会依次：

1. 检查 `makensis.exe`、设置 `NSISDIR`；
2. 读 `VERSION`（读不到就用 `1.1.1`），并换算成 `X.X.X.X` 供版本资源使用；
3. 逐个检查要打包的源文件，**缺任何一个就报错退出**（错误信息里写明缺哪个路径）；
4. 调 makensis 编译，`-1`/非 0 退出码直接报错；
5. 用 `7z l` 打印安装包内容，并断言 6 类文件都在、且 **不含 config.ini / config.dat**；
6. 生成便携版 zip（`Compress-Archive`）；
7. 打印产物完整路径与大小（MB），最后一行打印**可直接复制的 makensis 命令**。

等价的 makensis 命令（版本 1.1.1 时）：

```powershell
& 'D:\w64devkit\bin\makensis.exe' /INPUTCHARSET UTF8 `
  /DVERSION=1.1.1 /DPRODUCT_VERSION=1.1.1.0 `
  /DPAYLOAD_SRC=D:\Work\ScreenTime `
  /DAWTK_SRC=D:\Work\AWTK\awtk-master `
  /DOUT_FILE=D:\Work\ScreenTime\dist\ScreenTimeSetup-1.1.1.exe `
  D:\Work\ScreenTime\installer\ScreenTime.nsi
```

可用 `/D` 覆盖的宏：`VERSION`、`PRODUCT_VERSION`、`PAYLOAD_SRC`、`AWTK_SRC`、`OUT_FILE`、`ICON_FILE`（可选图标）。

### 依赖（本机已实测的路径）

- NSIS：`D:\w64devkit\bin\makensis.exe`，Include/Stubs/Plugins 在 `D:\w64devkit\share\nsis\`
- 打 ZIP：PowerShell 自带 `Compress-Archive`
- 验证包内容：`C:\Program Files\7-Zip\7z.exe`（没有则跳过该步并告警）

## 安装包装了什么

按用户安装（`RequestExecutionLevel user`，HKCU），**不需要管理员权限**，也不会影响其他用户。

默认安装目录 `%LOCALAPPDATA%\ScreenTime`（安装向导里可以改）：

| 安装后相对路径 | 来源 |
|---|---|
| `ScreenTime.exe` | `ScreenTime.exe` |
| `awtk.dll` | `awtk-master\build_mingw\awtk.dll` |
| `SDL2.dll` | `awtk-master\build_mingw\third_party\SDL2\SDL2.dll` |
| `res\assets\default\raw\styles\default.bin` | 同路径 |
| `res\assets\default\raw\images\x1..x4\cursor*.png` | 同路径（每个目录 3 个） |

**不含任何配置文件**：`config.ini` 不会被安装（项目根目录下那个隐藏的 `config.ini` 只是在开发时用），程序首次运行会在安装目录里生成加密的 `config.dat`。

### 界面流程（简体中文）

欢迎 → 安装目录 → 安装选项（`开机自动启动` 默认勾选、`安装完成后立即运行` 默认勾选）→ 安装进度 → 完成（完成页可再勾选「立即运行」）。

静默安装：`ScreenTimeSetup-1.1.1.exe /S`（使用默认选项；`/D=路径` 可指定目录，必须是最后一个参数且不加引号）。

### 写入的注册表项（全部在 HKCU）

| 键 | 值 |
|---|---|
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` | 字符串 `ScreenTime` = `"<安装目录>\ScreenTime.exe" --config "<安装目录>\config.dat"`（选项页取消勾选则不写；安装时会先删除旧值再按选择写入） |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\ScreenTime` | `DisplayName`=屏幕使用时间管理、`DisplayVersion`=<VERSION>、`Publisher`、`InstallLocation`、`DisplayIcon`、`UninstallString`、`QuietUninstallString`、`NoModify`=1、`NoRepair`=1、`EstimatedSize`(KB) |

开始菜单快捷方式（当前用户）：

- `开始菜单\ScreenTime\ScreenTime.lnk` → `ScreenTime.exe --config "<安装目录>\config.dat"`
- `开始菜单\ScreenTime\卸载 ScreenTime.lnk` → `<安装目录>\uninstall.exe`

### 关于「程序正在运行时安装」

安装向导一开始就会检测 `ScreenTime.exe` 是否正在运行/程序文件是否被占用：先尝试以独占写方式打开目标 exe（程序运行时其映像被锁定，这正是覆盖时 `Access denied` 的原因），再用 `tasklist` 查进程名。检测到就弹窗提示，点「确定」后执行 `taskkill /F /IM ScreenTime.exe` 并继续安装；点「取消」退出安装。安装 Section 里还会再兜底结束一次。

## 卸载

三种方式：

1. 开始菜单 → `ScreenTime` → `卸载 ScreenTime`；
2. Windows 设置 → 应用 → 已安装的应用 → 「屏幕使用时间管理」→ 卸载；
3. 直接运行 `<安装目录>\uninstall.exe`（静默：`uninstall.exe /S`）。

卸载流程：先 `taskkill /F /IM ScreenTime.exe` → 弹窗询问「是否同时删除截图、日志和配置？」：

- 选**是**：`RMDir /r $INSTDIR`（`screenshots\`、`logs\`、`config.dat` 全部删除）；
- 选**否**：只删 `ScreenTime.exe`、`awtk.dll`、`SDL2.dll`、`res\`、`uninstall.exe`，**保留** `screenshots\`、`logs\`、`config.dat`。

之后删除自启键、卸载注册信息键和开始菜单目录。

## 已知限制

- **这是 64 位安装包**：本机 NSIS 只带了 `amd64-unicode` 的 Stub 和插件（没有 `x86-*`），makensis 输出标注为 `amd64-unicode`，只能在 64 位 Windows 10/11 上运行（程序本体也是 w64devkit 编的 64 位 exe，一致）。
- **没有真正做过安装验证**：当前会话的文件沙箱禁止写工作区以外的路径（`%LOCALAPPDATA%`、开始菜单、HKCU 都会被拒），所以只验证到「编译通过 + 7z 能列出全部 payload + 解包后逐文件 SHA256 与源文件一致」。安装/卸载的实际行为需要你在真实机器上跑一次确认。
- **安装包没有自定义图标**：项目里没有 `ScreenTime.ico`，默认用 NSIS 自带图标。想要图标就把 `ScreenTime.ico` 放到 `installer\` 目录后再打包（脚本会自动加 `/DICON_FILE=`），或者编译时手动加 `/DICON_FILE=<路径>`。注意 `D:\Work\AWTK\awtk-master\win32_res\awtk.ico` 是 AWTK 自己的图标，没有被采用。
