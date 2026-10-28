/* ============================================================================
 *  ScreenTime 启动器（launcher）
 *
 *  装同一个目录里放两套后端，启动时按环境自动选一个：
 *      gl\    -> AWTK OpenGL 构建（有可用的 OpenGL 3 时用，界面按 DPI 缩放）
 *      soft\  -> AWTK 软件渲染构建（没有 OpenGL 时用，完全不碰显卡驱动）
 *
 *  探测方式：创建隐藏窗口 + 临时 OpenGL 上下文，读 GL_VERSION 的主版本号；
 *  只有主版本 >= 3 才用 GL 后端。任何一步失败都安全回落到 soft（不会崩）。
 *
 *  命令行：
 *      ScreenTime.exe [--config <路径>] [--backend gl|soft]
 *  其中 --backend 仅供手动排查时强制某个后端；--config 原样传给后端。
 *
 *  选完就拉起后端进程并退出（后端是独立进程，任务管理器里结束它即可）。
 *  决策结果写进 <程序目录>\logs\launcher.log。
 * ========================================================================== */

#include <windows.h>
#include <strsafe.h>
#include <stdio.h>
#include <string.h>

/* opengl32.dll 导出，直接用，省得依赖 GL 头文件 */
__declspec(dllimport) const unsigned char *__stdcall glGetString(unsigned int name);
#define GL_VERSION_STRING 0x1F02

/* ---------------- 小工具 ---------------- */

static void ExeDir(WCHAR *out, int cap) {
  GetModuleFileNameW(NULL, out, (DWORD)cap);
  {
    WCHAR *slash = wcsrchr(out, L'\\');
    if (slash) *slash = 0;
  }
}

static void WriteLog(const WCHAR *exeDir, const char *line) {
  WCHAR path[MAX_PATH];
  HANDLE h;
  DWORD written = 0;
  char stamped[768];
  SYSTEMTIME st;

  StringCchCopyW(path, MAX_PATH, exeDir);
  StringCchCatW(path, MAX_PATH, L"\\logs");
  CreateDirectoryW(path, NULL);                       /* 已存在则失败，忽略 */
  StringCchCatW(path, MAX_PATH, L"\\launcher.log");

  h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;

  SetFilePointer(h, 0, NULL, FILE_END);
  GetLocalTime(&st);
  _snprintf(stamped, sizeof(stamped) - 1, "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
            (int)st.wYear, (int)st.wMonth, (int)st.wDay,
            (int)st.wHour, (int)st.wMinute, (int)st.wSecond, line);
  stamped[sizeof(stamped) - 1] = 0;
  WriteFile(h, stamped, (DWORD)strlen(stamped), &written, NULL);
  CloseHandle(h);
}

/* ---------------- OpenGL 3 探测 ---------------- */

/* 返回 1 表示有可用的 OpenGL 3；info 里写一句可供日志的说明 */
static int ProbeOpenGL3(char *info, size_t cap) {
  WNDCLASSEXW wc;
  HWND hwnd;
  HDC dc;
  PIXELFORMATDESCRIPTOR pfd;
  int pf;
  HGLRC rc = NULL;
  int usable = 0;
  const unsigned char *ver = NULL;

  info[0] = 0;

  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = L"ScreenTimeGLProbe";
  RegisterClassExW(&wc);

  hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 8, 8,
                         NULL, NULL, wc.hInstance, NULL);
  if (hwnd == NULL) {
    StringCchCopyA(info, cap, "OpenGL 探测: 创建窗口失败");
    return 0;
  }

  dc = GetDC(hwnd);
  if (dc == NULL) {
    DestroyWindow(hwnd);
    StringCchCopyA(info, cap, "OpenGL 探测: GetDC 失败");
    return 0;
  }

  memset(&pfd, 0, sizeof(pfd));
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 24;
  pfd.cDepthBits = 16;

  pf = ChoosePixelFormat(dc, &pfd);
  if (pf == 0) {
    StringCchCopyA(info, cap, "OpenGL 探测: ChoosePixelFormat 失败（没有可用的 OpenGL 像素格式）");
  } else if (!SetPixelFormat(dc, pf, &pfd)) {
    StringCchCopyA(info, cap, "OpenGL 探测: SetPixelFormat 失败");
  } else {
    rc = wglCreateContext(dc);
    if (rc == NULL) {
      StringCchCopyA(info, cap, "OpenGL 探测: wglCreateContext 失败（无法创建 GL 上下文）");
    } else if (!wglMakeCurrent(dc, rc)) {
      StringCchCopyA(info, cap, "OpenGL 探测: wglMakeCurrent 失败");
    } else {
      ver = glGetString(GL_VERSION_STRING);
      if (ver == NULL) {
        StringCchCopyA(info, cap, "OpenGL 探测: glGetString(GL_VERSION) 返回空");
      } else {
        int major = 0;
        if (sscanf((const char *)ver, "%d", &major) == 1 && major >= 3) usable = 1;
        _snprintf(info, cap - 1, "OpenGL 探测: GL_VERSION=\"%s\" -> %s",
                  (const char *)ver, usable ? "可用 GL 后端" : "版本低于 3.x，改用软件渲染");
        info[cap - 1] = 0;
      }
    }
  }

  if (rc != NULL) {
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
  }
  ReleaseDC(hwnd, dc);
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  return usable;
}

/* ---------------- 启动后端 ---------------- */

static int RunBackend(const WCHAR *exeDir, const WCHAR *sub, const WCHAR *args) {
  WCHAR exePath[MAX_PATH];
  WCHAR workDir[MAX_PATH];
  WCHAR cmdLine[4096];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  BOOL ok;

  StringCchCopyW(workDir, MAX_PATH, exeDir);
  StringCchCatW(workDir, MAX_PATH, L"\\");
  StringCchCatW(workDir, MAX_PATH, sub);

  StringCchCopyW(exePath, MAX_PATH, workDir);
  StringCchCatW(exePath, MAX_PATH, L"\\ScreenTime.exe");
  if (GetFileAttributesW(exePath) == INVALID_FILE_ATTRIBUTES) return 0;

  StringCchPrintfW(cmdLine, 4096, L"\"%s\" %s", exePath, args ? args : L"");

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  memset(&pi, 0, sizeof(pi));

  ok = CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, workDir, &si, &pi);
  if (!ok) return 0;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 1;
}

/* ---------------- 入口 ---------------- */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow) {
  WCHAR exeDir[MAX_PATH];
  WCHAR configPath[MAX_PATH];
  WCHAR childArgs[4096];
  const WCHAR *force = NULL;
  int useGl;
  char info[512];
  char line[768];

  (void)hInst; (void)hPrev; (void)lpCmdLine; (void)nCmdShow;

  ExeDir(exeDir, MAX_PATH);

  /* --config <路径>：有就原样透传给后端（没给就用 exe 同级的 config.dat） */
  {
    const WCHAR *cmd = GetCommandLineW();
    const WCHAR *p = wcsstr(cmd, L"--config");
    configPath[0] = 0;
    if (p != NULL) {
      int n = 0;
      p += 8;
      while (*p == L' ' || *p == L'\t') p++;
      if (*p == L'"') {
        p++;
        while (*p && *p != L'"' && n < MAX_PATH - 1) configPath[n++] = *p++;
      } else {
        while (*p && *p != L' ' && *p != L'\t' && n < MAX_PATH - 1) configPath[n++] = *p++;
      }
      configPath[n] = 0;
    }
    if (configPath[0] == 0) {
      StringCchCopyW(configPath, MAX_PATH, exeDir);
      StringCchCatW(configPath, MAX_PATH, L"\\config.dat");
    }
  }

  /* --backend gl|soft（仅排查用） */
  {
    const WCHAR *cmd = GetCommandLineW();
    const WCHAR *p = wcsstr(cmd, L"--backend");
    if (p != NULL) {
      p += 9;
      while (*p == L' ' || *p == L'\t') p++;
      if (_wcsnicmp(p, L"gl", 2) == 0) force = L"gl";
      else if (_wcsnicmp(p, L"soft", 4) == 0) force = L"soft";
    }
  }

  if (force != NULL) {
    useGl = (wcscmp(force, L"gl") == 0);
    _snprintf(info, sizeof(info) - 1, "命令行强制后端 %s（跳过探测）", useGl ? "gl" : "soft");
    info[sizeof(info) - 1] = 0;
  } else {
    useGl = ProbeOpenGL3(info, sizeof(info));
  }
  _snprintf(line, sizeof(line) - 1, "启动器: %s", info);
  line[sizeof(line) - 1] = 0;
  WriteLog(exeDir, line);

  StringCchPrintfW(childArgs, 4096, L"--config \"%s\"", configPath);

  if (useGl && RunBackend(exeDir, L"gl", childArgs)) {
    WriteLog(exeDir, "启动器: 已启动 gl 后端");
    return 0;
  }
  if (useGl) WriteLog(exeDir, "启动器: gl 后端启动失败，改用 soft");

  if (RunBackend(exeDir, L"soft", childArgs)) {
    WriteLog(exeDir, "启动器: 已启动 soft 后端（软件渲染）");
    return 0;
  }

  WriteLog(exeDir, "启动器: 两个后端都启动失败！");
  MessageBoxW(NULL,
              L"启动失败：没有找到 gl\\ScreenTime.exe 或 soft\\ScreenTime.exe。\n\n"
              L"安装目录可能不完整，请重新安装，或把 logs\\launcher.log 发给开发者。",
              L"屏幕使用时间管理", MB_ICONERROR | MB_OK);
  return 1;
}
