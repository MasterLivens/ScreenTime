# ScreenTime — 屏幕使用时间管理（C / Win32 + AWTK）

限时使用电脑，到点**全屏锁屏强制休息**；休息没结束想提前解锁，必须输入密码。
每分钟截一次屏，交给本地视觉模型识别“当前在干什么”，结果写入日志。
默认**启动后不出现任何窗口**（只在后台计时/锁屏），适合给小朋友用。

界面用 **AWTK** 画（`D:\Work\AWTK\awtk-master` 就地引用，**不往本工作区拷贝任何 AWTK 文件**）；
锁屏用的全屏/置顶/剪裁鼠标、托盘图标、低级键鼠钩子仍然是纯 Win32。

---

## 1. 规则

| 行为 | 说明 |
|---|---|
| 限时使用 | 默认 **45 分钟**（`session_minutes`），可在程序里的「家长设置」界面改 |
| 触发后 | 全屏锁屏 + 倒计时，强制休息默认 **15 分钟**（`break_minutes`） |
| 提前解锁 | 必须输入密码，默认 **12345678**；配置文件里**没有明文**，只有 PBKDF2 哈希 |
| 密码错误 | 每次追加休息时间（默认 +30 秒），并记录日志；**空密码按回车不算错误**（只提示"请输入密码"） |
| 提前解锁惩罚 | 没休息完的分钟数会从**这一次解锁后的**可用时长里扣掉（`penalty=1`，最少保留 5 分钟） |
| 休息结束 | 自动解锁，开始新的计时会话 |
| **休息前提醒** | 距离强制休息还剩 `warn_minutes` 分钟（默认 **5**）时，右下角那个窗口会切成**提醒页**显示倒计时 + 「知道了」按钮；点「知道了」后本次会话不再弹。`warn_minutes=0` 就完全不提醒 |
| **截图开关** | `screenshot_enabled`：**总开关**，关掉就完全不截屏、不识别、不落盘（见下） |
| 截图识别 | 开启后每 60 秒（`screenshot_interval`）截全屏 → 缩放 → base64 → `POST /v1/chat/completions` |
| 结束程序 | **只能从任务管理器结束**（Ctrl+Alt+Del → 任务管理器 → ScreenTime.exe） |
| 启动界面 | 默认 `start_hidden=1`：**启动时不显示任何窗口**，只在后台计时/锁屏 |
| 隐藏窗口 | 点状态窗口的 X 只是**隐藏到系统图标区域**（右下角托盘），计时和锁屏继续运行；单击/双击托盘图标恢复 |
| **配置存储** | 配置以 **AES-256-CBC 加密**保存为 `config.dat`，只能用程序里的「家长设置」界面改，记事本打开是乱码 |

### 1.1 截图有三个层次的开关

| 配置项 | 关掉后的行为 |
|---|---|
| `screenshot_enabled` | **总开关**：完全不截屏，`screenshots\` 不再增长，也不请求模型（省电、省磁盘、完全静默） |
| `save_screenshots` | 照常截屏并送模型识别，但识别完就把图片删掉（只留文字记录） |
| `vision_enabled` | 照常截屏并保存，但不请求模型（只留本地图片，不联网） |

三个都是「家长设置」界面里的开关，改完点保存立即生效（不用重启）。

锁屏期间屏蔽：`Alt+Tab`、`Alt+F4`、`Alt+Esc`、`Alt+Space`、`Win` 键（含 Win+D / Win+L）、`Ctrl+Esc`。
`Ctrl+Alt+Del` 与 `Ctrl+Shift+Esc`（任务管理器）**始终可用**——这是唯一的退出通道。
鼠标被 `ClipCursor` 限制在锁屏范围内，锁屏窗口每 1.5 秒重新置顶，防止其它窗口盖上来。

### 1.2 计时规则（空闲 / 观影）

**不看视频时**

| 情况 | 行为 |
|---|---|
| 有键盘/鼠标操作 | 正常倒计时 |
| 闲置不到 `idle_seconds`（默认 **10 分钟**） | **照常倒计时**（短时间离开也算使用） |
| 闲置满 `idle_seconds` | **暂停计时**（剩余时间冻结，日志写一行"空闲超过 N 分钟，暂停计时"） |
| 暂停后重新操作 | `idle_reset=1`（默认）→ **重新开始计时**（新会话，回到满时长）；`idle_reset=0` → 接着之前的剩余时间继续 |

**看视频时**（`video_detect=1`，默认开启；不截图，靠系统状态判断）

| 情况 | 行为 |
|---|---|
| 窗口化播放视频 | 按上面的规则**正常倒计时**；同时因为"有视频在播"算作有活动，**不会**因为没碰键鼠而被判闲置 |
| **全屏**播放视频 | 进入**观影模式**：**暂停计时 + 不锁屏 + 不弹休息提醒**（电影不能被打断）。状态窗口标题会显示「（观影模式·暂停计时）」 |
| 退出全屏/停止播放 | 自动退出观影模式，恢复计时（剩余时间还是进去时的值） |

判定用的三个信号（都只看系统状态，不联网、不截图）：
`WASAPI 音频会话有声音` / `GPU 视频解码引擎有占用` / `前景窗口铺满显示器`。
全屏 + 有视频在播 → 观影模式。判断结果会连细节写进日志，例如：

```
[15:15:50] 进入观影模式（全屏视频播放中）：暂停计时、不锁屏；剩余 44 分钟｜音频:0 活动会话, 峰值0.000, 硬解:0.0%, 全屏:1, 观影:1
[15:14:54] 退出观影模式，恢复计时；剩余 44 分钟
```

> 注意：全屏的**游戏**如果也在出声/解码视频，同样会被当成观影模式（不锁屏）。这是有意的——
> 全屏场景本来就不该弹锁屏。想只按"看视频"处理，可以设 `video_detect=0` 关掉这套判定。

---

## 2. 构建

### 2.1 依赖

| 依赖 | 本机位置 | 说明 |
|---|---|---|
| MinGW-w64 GCC | `D:\w64devkit\bin\gcc.exe`（GCC 16.2） | 编译 ScreenTime 与应用模块 |
| AWTK 源码 + 已编译的动态库 | `D:\Work\AWTK\awtk-master`（构建目录 `build_mingw`） | 只**就地引用**头文件/导入库/资源，一个字节都不复制进本工作区 |

GCC / Win32 部分没有第三方依赖：GDI/GDI+ 截图存 PNG、WinHTTP 调模型、低级键鼠钩子、CNG（bcrypt）做密码哈希。

### 2.2 先编译 AWTK（只需一次；`build_mingw` 已存在时可跳过）

```powershell
cmake -S D:\Work\AWTK\awtk-master -B D:\Work\AWTK\awtk-master\build_mingw -G Ninja `
  -DCMAKE_C_COMPILER=D:/w64devkit/bin/gcc.exe -DCMAKE_CXX_COMPILER=D:/w64devkit/bin/g++.exe `
  '-DAWTK_TOOLS_NAME=mingw' -DAWTK_DEBUG=OFF -DCMAKE_BUILD_TYPE=Release -DAWTK_BUILD_SHARED=ON `
  -DAWTK_BUILD_DEMOS=OFF -DAWTK_BUILD_TESTS=OFF '-DAWTK_BUILD_CMAKE_FIND_DEMO=ON' `
  '-DCMAKE_POLICY_VERSION_MINIMUM=3.10' '-DDIRECTX=OFF'
cmake --build D:\Work\AWTK\awtk-master\build_mingw
```

产物：`build_mingw\awtk.dll`、`build_mingw\libawtk.dll.a`、`build_mingw\third_party\SDL2\SDL2.dll`。

**没有 OpenGL 的机器（虚拟机 / 远程桌面 / "Microsoft 基本显示适配器"）跑不起来怎么办？**
AWTK 默认用 OpenGL 渲染，这类机器上程序会在创建窗口那一步直接崩掉（日志停在
`阶段：界面：tk_init`）。这种情况下改用**软件渲染**构建（nanovg AGGE，完全不碰显卡驱动）：

```powershell
cd D:\Work\ScreenTime
powershell -NoProfile -ExecutionPolicy Bypass -File .\build_awtk_soft.ps1
```

这条命令把 AWTK 编到工作区里的 `out\awtk_soft\`（**不动** `build_mingw`），之后：

```powershell
# 编程序（软件渲染后端）
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Backend soft -Out out\soft\ScreenTime.exe
# 打软件渲染安装包 -> dist\ScreenTimeSetup-<版本>-soft.exe
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1 -Backend soft -PayloadExe out\soft\ScreenTime.exe
```

说明：AWTK 的 CMake 移植漏掉了 `src/lcd/lcd_sdl2.c`（SCons 会自动带上），所以 AGGE 构建
会在最后链接时缺 `lcd_sdl2_init` / `lcd_sdl2_texture_reset`。`build_awtk_soft.ps1` 会自动调用
`fix_awtk_soft_link.ps1`：单独编译那个源文件、再用同样的命令重新链接 `awtk.dll`
（**不修改 AWTK 源码**）。软件渲染版本不申请高分屏标志，AWTK 报的缩放是 1.00，
所以在 125% 缩放的屏幕上界面会比 GL 版小一圈（但清晰、不模糊）——主力机还是用 GL 版。

### 2.3 编译 ScreenTime

```powershell
cd D:\Work\ScreenTime
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

`build.ps1` 会 dot-source `awtk_env.ps1`（里面写死了 AWTK 的头文件目录、30 个 `WITH_*` 编译宏、导入库路径，
宏列表与 `build_mingw\build.ninja` 里 awtk 目标的一致，保证与 `awtk.dll` 的 ABI 对得上），然后用 gcc 编译 `src\*.c`。

编译宏里包含 `-DAWTK_RES_ROOT="D:/Work/AWTK/awtk-master/res"`（正斜杠）；程序启动时优先用
**exe 同级目录** 的 `res\assets\default\raw\styles\default.bin`，找不到才回退到这个开发路径——
所以开发时直接在工作区跑 exe 也能正常加载主题。

调试版（把密码校验的中间量写进日志，排查用，平时不要开）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Extra '-DST_PWDEBUG'
```

> 运行前需要让系统能找到 `awtk.dll` / `SDL2.dll`。开发时用 `run.ps1`（自动把 AWTK 构建目录加进 PATH）：
> ```powershell
> powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1
> powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1 -Extra '--config','D:\path\my.ini'
> ```
> 等价的手工做法：
> ```powershell
> $env:PATH = 'D:\Work\AWTK\awtk-master\build_mingw;D:\Work\AWTK\awtk-master\build_mingw\third_party\SDL2;' + $env:PATH
> .\ScreenTime.exe
> ```
> 安装后的版本把这两个 DLL 复制到 exe 旁边，就不需要改 PATH。

## 3. 安装 / 卸载

**给别人的安装包**（推荐，普通用户不需要 PowerShell）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\make_installer.ps1
# 产物（默认 -Backend auto：一个包自适应所有机器）：
#   dist\ScreenTimeSetup-1.1.3.exe        4.5 MB，Inno Setup 7 安装包（含卸载程序）
#   dist\ScreenTime-1.1.3-portable.zip    6.0 MB，免安装绿色包
#
# 也可以只打单后端的包（一般不需要）：
#   -Backend gl   -> dist\ScreenTimeSetup-<版本>.exe        （只有 OpenGL 后端）
#   -Backend soft -> dist\ScreenTimeSetup-<版本>-soft.exe   （只有软件渲染后端）
```

**默认包是自适应环境的**：安装后目录长这样——

```
%LOCALAPPDATA%\ScreenTime\
  ScreenTime.exe      ← 启动器（几十毫秒，探测 OpenGL 后拉起下面某个后端，然后自己退出）
  gl\ScreenTime.exe   ← AWTK OpenGL 构建（有可用 OpenGL 3 时用，界面按 DPI 缩放）
  soft\ScreenTime.exe ← AWTK 软件渲染构建（没有 OpenGL 时用，完全不碰显卡驱动）
  config.dat, screenshots\, logs\   ← 两个后端共用同一份配置与数据
```

启动器会建一个隐藏窗口 + 临时 OpenGL 上下文读 `GL_VERSION`：主版本 ≥ 3 就走 `gl\`，
否则（或创建上下文失败）自动落到 `soft\`。探测和启动过程**都不会弹窗/崩溃**，
选了哪个后端写进 `logs\launcher.log`：

```
[2026-10-28 14:15:13] 启动器: OpenGL 探测: GL_VERSION="4.6.0 - Build 32.0.101.7085" -> 可用 GL 后端
[2026-10-28 14:15:13] 启动器: 已启动 gl 后端
```

想手动强制看另一个后端：`ScreenTime.exe --backend soft`（或 `--backend gl`）。
只有"两个后端目录都缺失"时才会弹一次错误提示。

安装包行为：按用户安装（**不需要管理员权限**）、默认装到 `%LOCALAPPDATA%\ScreenTime`、
升级时沿用原目录且**不会动** `screenshots\logs\config.dat`、可选「开机自动启动」与「桌面快捷方式」、
写开始菜单快捷方式、注册到「应用和功能」里可以正常卸载（交互式卸载会问是否连截图/日志/配置一起删，
静默卸载一律保留个人数据）。安装包是 **64 位**的（程序本体与两个 DLL 都是 64 位），32 位 Windows 装不了。

静默安装/卸载与打包脚本细节见 [installer\README.md](D:\Work\ScreenTime\installer\README.md)。

**给本机开发用的脚本**：

```powershell
.\install.ps1                     # 复制到 %LOCALAPPDATA%\ScreenTime + 开机自启 + 启动
.\install.ps1 -NoAutoStart        # 不开自启
.\install.ps1 -SkipBuild          # 只复制（不重新编译）
.\uninstall.ps1                   # 结束进程 + 删除自启项（保留截图和日志）
.\uninstall.ps1 -RemoveFiles      # 连安装目录一起删
```

安装目录长这样（AWTK 的资源根目录就是 exe 所在目录）：

```
%LOCALAPPDATA%\ScreenTime\
  ScreenTime.exe
  awtk.dll                                   # 6.1 MB
  SDL2.dll                                   # 1.9 MB
  res\assets\default\raw\styles\default.bin   # 主题，67 KB
  res\assets\default\raw\images\xN\cursor*.png# 鼠标指针（可选，缺了就退回系统指针）
  config.dat                                 # Hidden，加密配置（首次运行自动生成）
  screenshots\ , logs\
```

安装包和脚本都只复制上面这几个文件（合计约 8 MB），**不会**把 AWTK 的 83 MB `assets\default` 整个搬过来，
也**不再分发任何配置文件**（`config.dat` 由程序首次运行时自己生成）。
中文字体在运行时从 `C:\Windows\Fonts\`（simhei → simfang → simkai → STZHONGS → STSONG）里挑第一个能用的，
用 AWTK 的 stb 字体接口注册成 `default`，所以字体也不用打包。

自启写在 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\ScreenTime`，
值是 `"<安装目录>\ScreenTime.exe" --config "<安装目录>\config.dat"`。

## 4. 配置（加密存储 + 界面里改）

程序目录下的 **`config.dat`**：AES-256-CBC 加密的二进制文件（文件头 `STCF`），首次运行自动生成
（默认密码 `12345678`），文件属性是 Hidden。**用记事本打开只会看到乱码**，改配置只有两条路：

1. 程序里的 **「家长设置」→ 保存**（推荐，所有选项都有界面，见 §4.2）；或
2. 托盘菜单 →「家长设置（需密码）」→ 同一个界面。

改完点「保存」**立即生效**（截图相关开关当轮就生效；改限时/休息会开始一个新的计时会话）。
`config.dat` 里 `key = value` 的键名与含义如下（界面里对应哪些项见括号）：

| 键 | 默认 | 含义 |
|---|---|---|
| `session_minutes` | 45 | 每次限时使用分钟数（限时使用） |
| `break_minutes` | 15 | 触发后强制休息分钟数（强制休息） |
| `warn_minutes` | 5 | 剩余不足多少分钟开始变红提醒（锁屏前提醒） |
| `idle_pause` / `idle_seconds` | 1 / **600** | 闲置多久算"人不在"并暂停计时（默认 10 分钟；0 或 `idle_pause=0` 则一直计时） |
| `idle_reset` | 1 | 1 = 长闲置后回来**重新开始计时**（满时长）；0 = 接着之前的剩余时间继续 |
| `video_detect` | 1 | 1 = 用系统状态检测视频；全屏播放时**暂停计时且不锁屏**，窗口化播放算正常活动 |
| `penalty` | 1 | 提前解锁扣减下次时长 |
| **`screenshot_enabled`** | **1** | **截图总开关：0 = 完全不截图、不识别、不落盘** |
| `screenshot_interval` | 60 | 截图/识别间隔（秒） |
| `save_screenshots` | 1 | 0 = 识别完删掉截图（只留文字记录） |
| `vision_enabled` | 1 | 0 = 不请求模型（只存图） |
| `keep_days` | 7 | 截图保留天数，0 = 永久 |
| `max_image_width` | 1280 | 送模型的截图宽度（多屏整屏等比缩放） |
| `api_url` | `http://192.168.6.220:8177/v1` | OpenAI 兼容地址，自动拼 `/chat/completions` |
| `model` | `qwen3.8-flash-next-iq3_xxs` | 模型名 |
| `api_key` | 空 | 需要时填，会作为 `Authorization: Bearer` |
| `wrong_penalty_seconds` | 30 | 密码错误追加的休息秒数 |
| `block_hotkeys` | 1 | 锁屏期间屏蔽快捷键 |
| `start_hidden` | 1 | 1 = 启动时不显示任何窗口；0 = 启动时显示状态窗口 |
| `show_tray_icon` | 1 | 1 = 隐藏时在右下角显示托盘图标；0 = 完全不可见 |
| `password_hash` / `password_salt` / `pw_iterations` | 自动生成 | 密码的 PBKDF2-HMAC-SHA256 哈希、盐、迭代次数（由「修改密码」界面生成） |

指定别的配置档：`ScreenTime.exe --config D:\path\my.dat`（截图/日志会放在该文件同级的 `screenshots` / `logs`）。

**从旧版升级**：如果目录里还有老版本的明文 `config.ini`，程序第一次启动会自动读它、加密成 `config.dat`，
并把原文件改名为 `config.ini.bak`（不再生效，可自行删除）。原来的设置和密码都会保留。

### 4.1 密码怎么存、怎么换

* 比较密码用 **PBKDF2-HMAC-SHA256（CNG `BCryptDeriveKeyPBKDF2`）+ 16 字节随机盐 + 20000 次迭代**，
  常量时间比较；明文只在内存里存在几毫秒（用完 `SecureZeroMemory` 清零），**永不落盘**。
* **换密码**：程序里 →「家长设置」→ 输密码进入 → 点 **「识别与密码」** → 在「新密码 / 确认新密码」里各输一遍
  （至少 4 位，两次必须一致）→ 点「保存」。界面会立刻生成新的哈希写进 `config.dat`，
  **改完立即生效、不用重启**：存完就能用新密码过家长验证，也能用来解锁。
* 首次运行且没有哈希时，默认密码就是 `12345678`（想换就在上面那一步改，随时能改）。

### 4.2 家长设置（配置界面）

窗口右下角点 **「家长设置」**（或托盘右键 →「家长设置（需密码）」）→ 输入家长密码 → 进入设置界面。
窗口会自动放大到 680×460 逻辑像素的两列表单：

* **左列**：限时使用 / 强制休息 / 锁屏前提醒 / 空闲暂停计时 / 空闲判定秒数 / 提前解锁扣时长 /
  密码错误加时 / 锁屏屏蔽快捷键
  （其中「锁屏前提醒（分钟）」同时控制休息前那个倒计时提醒页，填 0 = 不提醒）
* **右列**：**定时截图** / 截图间隔 / 保存截图文件 / 送模型识别 / 截图保留天数 / 启动时隐藏界面 /
  显示托盘图标 / 送图最大宽度
* 数值项用 `-` / `+` 按钮调（到上下限会自动禁用对应按钮），开关项显示「开 / 关」，点一下切换
* 底部四个按钮：**识别与密码**（子页：接口地址 / 模型 / API 密钥 / 修改密码 / 打开日志目录）、
  **恢复默认**（只重置时间、截图、界面类开关，密码与识别配置保留）、**取消**（丢弃改动、窗口缩回）、
  **保存**（加密写盘并立即生效）

进入设置界面时窗口会临时放大；取消/保存后自动缩回右下角的状态窗口，尺寸不会“带回去”。
锁屏触发时如果正停在设置界面，会自动退出设置界面再锁屏。

## 5. 产物

```
ScreenTime.exe                             # 233 KB（AWTK 在 DLL 里）
awtk.dll / SDL2.dll                        # 运行时依赖（安装时复制到 exe 旁边）
res\assets\default\raw\styles\default.bin  # AWTK 主题
config.dat                                 # Hidden 属性，AES 加密（首次运行自动生成）
screenshots\2026-10-08\15-03-24.png        # 每分钟一张（截图开关关掉就不产生）
logs\activity.log                          # 人读：启动/锁屏/解锁/识别结果
logs\activity.jsonl                        # 机读：JSON 行，便于统计
```

识别结果格式（模型被要求输出三行）：

```
正在使用DeepSeek开发“屏幕使用时间管理”工具，处理代码任务与进程调度问题。
类别：工作
专注程度：高
```

## 6. 界面（AWTK）

界面由 AWTK 绘制：平时是一个 470×240 逻辑像素的窗口，里面六个整窗重叠的页面
（状态 / 家长验证 / 设置表单 / 识别与密码 / 锁屏 / 休息提醒）按需切换；进入设置表单时窗口临时放大到 680×460。
非当前页会被同时设成 `visible=0` 和 `sensitive=0`——AWTK 的命中测试**不看 `visible`**，只把当前页设成
`sensitive=0` 才能避免隐藏页面把鼠标事件吞掉。
**休息提醒不会另开窗口**：它只是把右下角那个窗口的内容换成提醒页（倒计时 + 「知道了」），
这样单窗口模型不变、也不需要任何平台相关代码，将来移植到 Linux 时照旧可用。
提醒期间窗口会临时置顶（`HWND_TOPMOST` + `SWP_NOACTIVATE`，不抢键盘焦点），
关掉提醒或锁屏时恢复普通层级。

* **启动时**：默认没有任何窗口（`start_hidden=1`），右下角有一个托盘图标（蓝黄盾牌）。
  **启动过程完全不闪**：窗口在创建那一刻就被放到屏幕外并隐藏，布局好右下角之后才决定是否显示
  （详见 §8.1 里 SDL 那条坑）；`start_hidden=0` 时则在布局完成后一次性出现在右下角，也不会先闪在别处。
  想换成自己的图标，把一张 `.ico` 放到 exe 同级并命名 `ScreenTime.ico` 即可（标题栏/任务栏图标也会跟着变）。
  把 `show_tray_icon` 设成 0 就连图标都没有；这时想恢复窗口，**再运行一次 `ScreenTime.exe`** 即可
  （单实例分支会给已运行实例发 `WM_SHOWREQ`，把窗口显示出来）。
  注意：托盘图标只在窗口**隐藏**时存在；窗口显示着的时候图标会先撤掉。
* **状态窗口**：剩余时间（大字）、会话进度条（剩余比例，快到时变红）、本次会话时长、将在几点锁屏；
  按钮「立即休息 / 重载配置 / 家长设置」按窗口宽度均分，不会挤在左边。
  窗口里**不显示截图/识别相关信息**，也不写任何“怎么结束进程”的提示。
  窗口固定贴在屏幕**右下角**（距工作区右边 24px、距底边 12px），标题栏只有最小化和关闭按钮（没有最大化）；
  如果分辨率/DPI 变了或者窗口被拖出屏幕，程序会自动把它贴回右下角。
* **点状态窗口的 X**：程序隐藏到系统图标区域（屏幕右下角托盘），任务栏图标消失，计时与锁屏照常继续；
  托盘图标悬停显示剩余时间，**单击或双击托盘图标**恢复窗口，右键菜单有「显示窗口 / 立即休息 / 重载配置 / 家长设置（需密码）」。
  Win11 上托盘图标可能收在 `^` 展开区里。
  X 永远不会退出程序，托盘菜单里也**没有退出项**——结束程序仍然只能通过任务管理器。
* **家长设置页**：在同一个窗口里切换页面（不是新开对话框），有「家长密码」输入框 + 「确定 / 取消」。
  密码框是掩码的（`INPUT_PASSWORD`），回车等于点「确定」，ESC 等于「取消」；验证通过后进入**设置表单**
  （窗口临时放大到 680×460 逻辑像素，两列 16 项 + 「识别与密码」子页，详见 §4.2）。
* **锁屏页**：整屏铺满、置顶（Win32 `SetWindowPos(HWND_TOPMOST)` + 去掉标题栏）、鼠标被 `ClipCursor` 限制在屏幕内，
  红色「强制休息中」+ 蓝色大号倒计时 + 掩码密码框 + 「解锁」按钮，只提示「提前解锁必须输入密码」。
  弹出锁屏时会**主动抢前台和键盘焦点**，直接敲密码就行，不需要先点密码框；
  点到屏幕空白处也不会丢焦点（程序每 0.25 秒把焦点按回密码框）。
  回车或点「解锁」提交密码；**ESC 被吞掉**，锁屏时最小化/移动/关闭窗口的系统命令也被吞掉；
  就算锁屏前窗口被「显示桌面」最小化了，锁屏时也会自动还原成整屏。
  解锁后如果之前是隐藏模式，状态窗口**不会**突然弹出来。
  空密码按回车只提示「请输入密码」，**不算密码错误**（不记次数、不加罚时）。
* **分辨率 / 缩放**：100%～150% 缩放、1920×1080 / 1600×900 / 1366×768 / 1280×720 / 1024×768 / 800×600 / **640×480** 都实测过：
  状态窗口自动贴右下角（`SPI_GETWORKAREA`，含任务栏），锁屏铺满整屏；矮屏（逻辑高度 < 660，约 1024×768 及以下）
  会自动切到紧凑排版（字号与间距整体缩小），避免倒计时和提示行挤在一起。

## 7. 已知边界

* 这是**自律工具，不是安全边界**：能进任务管理器就能结束它；能改文件就能重置它
  （比如直接把 `config.dat` 删掉，程序会按默认值重建，密码退回 `12345678`）。
* `config.dat` 的加密是**防翻看、防随手篡改**级别的：密钥口令编译在 exe 里（每次保存用新的随机盐 + PBKDF2
  派生 AES-256 密钥），所以拿记事本看不到内容、孩子也不能直接改「限时 45 分钟」；但会逆向的人仍能解开。
  它不能替代账户权限——给孩子用标准账户，别用管理员账户。
* 升级时会留下 `config.ini.bak`（老版的明文配置），里面是明文设置与密码哈希。确认迁移成功后**建议删掉它**。
* 单实例：重复启动不会开第二份，而是让已运行实例显示窗口。
* 界面依赖 `awtk.dll`（6.1 MB）与 `SDL2.dll`（1.9 MB）：**必须和 exe 放在一起**，
  另外 exe 同级要有 `res\assets\default\raw\styles\default.bin`；开发时若不复制资源，
  程序会回退到编译期写死的 `D:/Work/AWTK/awtk-master/res`。中文字体从系统字体目录读，不打包。
* **`awtk.dll` 有两种，不能混用**：GL 版（`build_mingw`）需要可用的 OpenGL 3；
  软件渲染版（`out\awtk_soft`）不依赖任何图形驱动，但没有高分屏缩放（AWTK 报 1.00）。
  装错包的表现是：要么启动即崩（日志停在 `阶段：界面：tk_init`），要么界面在 125% 屏上偏小。
* 模型不可达时只记日志（`请求失败（无法连接或超时）`），不影响锁屏计时。
* 多屏：截图与锁屏都覆盖整个虚拟桌面（`SM_XVIRTUALSCREEN` 等）。
* 锁屏期间不截图（截了也只是锁屏画面），日志里也不会出现该时间段的识别记录。
* 截图总开关关闭时，`screenshots\` 不增长、也不发任何请求（只剩锁屏与日志）。
* 每次重启程序都会重新开始计时会话；只想改配置不必重启，用「家长设置 → 保存」即可。

## 8. 源码结构

```
VERSION           版本号（安装包与程序内显示都用它）
build.ps1         gcc 编译 src\*.c -> ScreenTime.exe（-Out 指定输出路径，-Backend gl|soft 选渲染后端）
run.ps1           开发时运行工作区的 exe（把 AWTK 构建目录加进 PATH）
awtk_env.ps1      AWTK 头文件目录 / 编译宏 / 导入库路径（从所选构建的 build.ninja 自动解析，保证 ABI 一致）
build_awtk_soft.ps1   用软件渲染（nanovg AGGE）把 AWTK 编到 out\awtk_soft（给没有 OpenGL 的机器）
fix_awtk_soft_link.ps1  补上 AWTK CMake 漏掉的 src\lcd\lcd_sdl2.c 并重新链接 awtk.dll（被上一个脚本自动调用）
launcher\launcher.c   启动器源码：探测 OpenGL 后拉起 gl\ 或 soft\ 后端（自适应安装包用）
build_launcher.ps1    编译启动器 -> out\launcher\ScreenTime.exe
installer\        Inno Setup 7 安装包：ScreenTime.iss（UTF-8 带 BOM）+ make_installer.ps1（纯 ASCII 打包脚本）+ README.md
dist\             安装包产物：ScreenTimeSetup-<版本>.exe / -soft.exe，ScreenTime-<版本>-portable.zip / -portable-soft.zip
src/main.c        入口、会话计时、截图线程（含截图总开关）、单实例、清理过期截图、配置迁移
src/ui.h          UI 层接口：UiInit / UiRun / UiShutdown / LockBegin / LockEnd / LockIsActive
src/ui_awtk.c     AWTK 界面层：状态页/家长验证/设置表单/识别与密码/锁屏页、托盘、全屏锁屏、Win32<->AWTK 桥接
src/config.c/.h   配置：加密读写（AES-256-CBC）、老明文 ini 迁移、默认值、密码哈希落地
src/crypt.c/.h    CNG/bcrypt 的 AES-256-CBC 加解密 + 文件头格式（magic STCF / 版本 / 迭代次数 / 盐 / IV）
src/passwd.c/.h   PBKDF2-HMAC-SHA256（CNG bcrypt）、随机盐、十六进制编解码、常量时间比较
src/capture.c/.h  GDI 抓屏 + StretchBlt 缩放 + GDI+ 存 PNG
src/vision.c/.h   base64 + WinHTTP POST + 响应 JSON 解析（含 \u 解码）
src/hooks.c/.h    WH_KEYBOARD_LL / WH_MOUSE_LL：输入活动统计 + 锁屏期屏蔽快捷键
src/logger.c/.h   activity.log / activity.jsonl
src/util.c/.h     路径、时间戳、UTF-8/宽字符、Base64、JSON 转义
legacy\           AWTK 之前的纯 Win32 UI 快照（statuswin.c / lock.c / 旧 build.ps1），可独立编译，只作参考
tools/selftest.c      独立自测：截图 -> 模型识别（控制台）
tools/capdebug.c      独立自测：只截图存 PNG，检查抓屏/缩放
tools/pwtest.c        独立自测：PBKDF2 派生/校验，可离线核对 config.ini 里的哈希
tools/hiddenwrite.c   独立自测：隐藏文件用 CREATE_ALWAYS 覆写会被拒绝（err 5）的复现
tools/input.c         测试辅助：向窗口注入鼠标点击/键盘（SendInput；注意 VK 事件必须带扫描码，否则 SDL 收不到）
tools/setmode.c       测试辅助：列出/切换/恢复显示分辨率（配合 test/rescheck.ps1 验证不同分辨率下的布局）
tools/awtkinfo.c      测试辅助：打印某进程顶层窗口的完整几何（含 left/top、是否在工作区内、是否置顶）
test/shot.ps1         测试辅助：整屏截图存 PNG（先 SetProcessDPIAware，拿到物理像素）
test/crop.ps1         测试辅助：从截图里裁一块并放大，便于用肉眼/模型检查界面有没有被裁切
test/rescheck.ps1     测试辅助：逐个分辨率验证"状态窗口贴右下角 + 锁屏铺满 + 密码能解锁"，跑完恢复原分辨率
test/layout.ini       测试用配置：2 小时使用 + 1 分钟休息，关掉截图识别
tools/uidump.c        测试辅助：打印窗口/客户区尺寸、DPI 与子控件尺寸（legacy 版用）
tools/enumwin.c       测试辅助：列出某进程创建的所有顶层窗口（含隐藏窗口）
tools/pwchange.ps1    测试辅助：写入明文密码、重载配置、检查哈希变化与去重
tools/reloadcheck.ps1 测试辅助：连续两次重载配置，观察哈希/盐
legacy\build.ps1      AWTK 之前那版（纯 Win32 窗口 + 无 AWTK 依赖）的编译脚本
```

### 8.1 改 UI 时的几个坑（都踩过了）

* **AWTK 的 `event_func_t` 是 `ret_t (*)(void* ctx, event_t* e)`**：`ctx` 在前，`e` 在后。
* `label_create/button_create/edit_create/view_create` 都是 **5 个参数**（parent, x, y, w, h），
  `color_init(r,g,b,a)` 返回 `color_t`（4 参），`widget_set_style_color(w, "state:name", uint32)`。
* `window_manager()` 返回 `widget_t*`，**不要**强转成 `window_manager_t*`；
  拿 `native_window` 要 `#include "window_manager/window_manager_default.h"` 后取
  `((window_manager_default_t*)window_manager())->native_window`（没有 `native_window()` 函数）。
* **隐藏页面会吞鼠标事件**：`widget_find_target_default()` 只检查 `sensitive/enable`，不看 `visible`
  （而且子控件是**倒序**遍历，最后创建的页面最先命中）。多页面重叠时，非当前页必须 `widget_set_sensitive(p, FALSE)`。
* **密码框要设 `edit_set_input_type(ed, INPUT_PASSWORD)`，而且必须在 `edit_set_text_limit()` 之后**：
  `edit_set_text_limit()` 会把 `input_type` 重置成 `INPUT_TEXT`，而 `edit_on_paint_self()` 每帧都会把掩码清掉，
  除非 `input_type` 是 `INPUT_PASSWORD`。
* **程序设置的焦点不会让编辑框收到文字输入**：SDL 的文字输入走
  `input_method_dispatch_to_widget()`，它要求 `input_method()->widget != NULL`；而 edit 只在
  ① `edit->open_im_when_focused == TRUE` 时获得焦点、或 ② 用户点过编辑框（`text_edit_click()`）这两种情况下
  才 `input_method_request()` 登记自己。所以 `edit_set_open_im_when_focused(ed, TRUE)`（这是默认值，别关掉！），
  程序里 `widget_set_focused()` 之后最好再补一次 `input_method_request(input_method(), ed)`。
  否则表现是：**回车等按键能到，但敲进去的字符一个都不进输入框**——很难查。
  桌面版 SDL 输入法不会弹软键盘，只是 `SDL_StartTextInput()`。
* **点到非焦点控件会清掉 AWTK 的焦点**（`widget_on_pointer_down_children()` 会
  `widget_set_focused_internal(key_target, FALSE)`）：锁屏/家长页里用户随便点一下屏幕，
  之后键盘就再也输不进密码框了。对策：给页面挂 `EVT_POINTER_DOWN` 把焦点抢回编辑框，
  锁屏期间再在定时器里兜底。
* **最小化的窗口锁不住屏**：`SetWindowPos(..., SWP_SHOWWINDOW)` 不会把最小化的窗口还原，
  锁屏就变成"看不见的锁"。锁屏前和锁屏期间都要 `if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);`。
* **托盘图标变成白方块**：`LoadIconW(GetModuleHandleW(NULL), IDI_APPLICATION)` 这种写法拿到的是 **NULL**
  （系统图标必须 `LoadIconW(NULL, IDI_xxx)`），`Shell_NotifyIcon` 仍然返回成功、提示文字也正常，
  只是图标位置画成一个白方块——很容易漏掉。程序现在优先读 exe 同级的 `ScreenTime.ico`
  （`LoadImageW(..., LR_LOADFROMFILE)`），没有就用系统盾牌 `IDI_SHIELD`；标题栏/任务栏图标也一起设了。
* **`SendInput` 注入 VK 事件必须带扫描码**：`ki.wVk = vk` 但 `ki.wScan = 0` 时，SDL 收不到 WM_KEYDOWN
  （表现：自动化测试里"回车没反应"，而手敲是好的）。补上 `MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)` 即可。
* **PowerShell 5.1 读无 BOM 的 UTF-8 会按 ANSI 解码**：脚本里写中文注释时，乱码里可能出现续行反引号，
  把下一行代码吞进注释——症状是"某个变量莫名是 `$null`"、`Test-Path` 报参数为空。
  所有 `.ps1` 一律**只写 ASCII**（本轮 `install.ps1` 就是这么被自己搞坏又修好的）。
* **SDL 会把窗口自己显示出来，所以"启动隐藏"必须抢在创建那一刻**：
  SDL 2.0.9 的 `SDL_CreateWindow` 末尾是 `if (!(flags & SDL_WINDOW_HIDDEN)) SDL_ShowWindow(window);`，
  而 AWTK 传进去的 flags 里没有 `SDL_WINDOW_HIDDEN`——所以"先创建、再 `ShowWindow(SW_HIDE)`"必然闪一下，
  而且只把创建风格里的 `WS_VISIBLE` 摘掉也没用（SDL 随后会显式 Show）。
  做法：`tk_init()` 之前挂一个**线程级 `WH_CBT` 钩子**，在 `HCBT_CREATEWND` 里把 `lpcs->style` 去掉
  `WS_VISIBLE`、`dwExStyle` 临时加上 `WS_EX_TOOLWINDOW`（否则任务栏按钮会闪）、`lpcs->x/y` 挪到 -32000；
  窗口建好后立刻 `ShowWindow(SW_HIDE)` **再**做布局（否则 `PlaceStatusWindow()` 挪到右下角那一下照样会被看到），
  最后才按 `start_hidden` 决定是否 `ShowWindow`。实测：3ms 粒度采样 1100+ 次，窗口一次都没出现在屏幕内。
* **`ConfigSave()` 会清零明文密码字段，所以"改密码"必须先哈希再保存**：
  `ConfigSave()` 写盘成功后会 `SecureZeroMemory(c->password)`（明文不留在内存里），
  如果像最初那样先 `ConfigSave(&g_cfg)` 再 `if (g_cfg.password[0]) ConfigFinalizePassword(...)`，
  明文在到达哈希函数之前就被清掉了——表现是**日志显示"家长设置已保存"但密码根本没变**（静默失效，很难发现）。
  正确顺序：`if (g_cfg.password[0]) ConfigFinalizePassword(...)`（内部派哈希 + 落盘）→ 再 `ConfigSave()` 保存其余设置。
* **CNG 的 AES 用法**：`BCryptOpenAlgorithmProvider(BCRYPT_AES_ALGORITHM)` →
  `BCryptSetProperty(BCRYPT_CHAINING_MODE, BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC))` →
  `BCryptGetProperty(BCRYPT_OBJECT_LENGTH)` → `BCryptGenerateSymmetricKey` →
  `BCryptEncrypt/BCryptDecrypt(..., BCRYPT_BLOCK_PADDING)`（自动 PKCS#7 填充，
  **解密时输出缓冲区要开到密文长度**，否则 padding 校验会失败）。
* **锁屏要主动抢前台**：Windows 前台锁定会让后台进程的 `SetForegroundWindow` 失败，
  用 `AttachThreadInput(当前线程, 前台线程)` + `SetForegroundWindow` + `SetActiveWindow` + `SetFocus` 才稳。
* AWTK 的 `assets_init()` 库里没有实现，**必须由应用提供**：
  `assets_manager_preload(assets_manager(), ASSET_TYPE_STYLE, "default"); tk_init_assets();`
  初始化顺序：`tk_pre_init()` → `tk_init(w,h,APP_DESKTOP,...)` → `assets_init()` → 注册中文字体 →
  `load_cjk_font()` → `window_manager_set_cursor()` → 建 UI → `timer_add()` → `tk_run()` → `tk_exit()`。
* AWTK 自带的字体没有中文，运行时用 `font_stb_create("default", buf, size)` +
  `font_manager_add_font(font_manager(), font)` 把 `C:\Windows\Fonts\simhei.ttf` 注册成 `default`
  （stb_truetype 读不了 `.ttc`）。
* `main_loop_sdl` 收到 `SDL_QUIT` 就会退出 `tk_run()`，所以必须子类化 SDL 窗口
  （`SetWindowLongPtrW(GWLP_WNDPROC, ...)`）把 `WM_CLOSE` / `SC_CLOSE` 吞掉改成“隐藏到托盘”。

## 9. 版本管理（git）

仓库已经初始化好（默认分支 `main`），身份写在**仓库级配置**里（`git config --local`，不动你的全局设置）：

```powershell
cd D:\Work\ScreenTime
git status                     # 看改动
git log --oneline              # 看历史
git add -A; git commit -m "..."  # 提交
```

**提交什么**：`src\`、`legacy\`、`tools\`（源码）、`test\`（脚本与测试用 ini）、`installer\`、
`build.ps1` / `run.ps1` / `awtk_env.ps1` / `install.ps1` / `uninstall.ps1`、`README.md`、`VERSION`、
`.gitignore` / `.gitattributes` —— 一共 83 个文件，整仓约 140 KiB。

**不提交什么**（见 [.gitignore](D:\Work\ScreenTime\.gitignore)）：

| 忽略项 | 原因 |
|---|---|
| `config.dat` / `config.ini` / `config.ini.bak` / `legacy\config.ini` | 里面有密码哈希/盐或加密后的配置，**绝不应该进版本库** |
| `ScreenTime.exe`、`dist\`、`out\`、`build\`、`*.o`、`tools\*.exe` | 构建产物，能重新生成 |
| `logs\`、`screenshots\`、`test\logs\`、`test\screenshots\`、`test\verify\`、`test\*.png` | 运行时数据与验证截图（几十 MB） |

其它约定：

* `legacy\config.ini` 已从仓库移除（旧版程序首次运行会自己生成），磁盘上那份如果还在也可以直接删。
* **AWTK 不进仓库**：`awtk_env.ps1` 里写的是绝对路径 `D:\Work\AWTK\awtk-master`，就地引用其头文件/导入库/资源；
  换机器时改这一个文件、或把 AWTK 源码放在同样的路径即可（详见 §2）。
* `.gitattributes` 统一用 LF 存文本、并把 `*.exe/*.dll/*.png/*.dat/*.zip` 标成二进制，
  避免 Windows/Linux 之间把行尾改来改去、也让 diff 不去碰二进制。
* 从零开始恢复构建环境：装 MinGW-w64（`D:\w64devkit`）→ 按 §2.2 编译 AWTK → `git clone` 本仓库 →
  `.\build.ps1` 编译 → `.\install.ps1` 安装（或直接用 `dist\ScreenTimeSetup-<版本>.exe` 安装）。
