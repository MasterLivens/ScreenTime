#include "hooks.h"
#include "appstate.h"

static HHOOK g_kbHook = NULL;
static HHOOK g_msHook = NULL;

static volatile LONG g_hookLocked = 0;
static volatile LONG g_hookBlock  = 1;

void HooksSetLocked(int locked)       { InterlockedExchange(&g_hookLocked, locked ? 1 : 0); }
void HooksSetBlockHotkeys(int block)  { InterlockedExchange(&g_hookBlock, block ? 1 : 0); }

static int AltDown(const KBDLLHOOKSTRUCT *k)
{
    return (k->flags & LLKHF_ALTDOWN) || (GetAsyncKeyState(VK_MENU) & 0x8000);
}

static int CtrlDown(void)
{
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}

static int ShiftDown(void)
{
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
}

static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        InterlockedExchange64(&g_lastInputTick, (LONGLONG)GetTickCount64());

        if (g_hookLocked && g_hookBlock &&
            (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN || wp == WM_KEYUP || wp == WM_SYSKEYUP)) {

            DWORD vk = k->vkCode;

            /* Win 键（含 Win+D / Win+L / Win+Tab） */
            if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS)
                return 1;

            if (AltDown(k)) {
                /* Alt+Tab / Alt+Esc / Alt+F4 / Alt+Space / Alt+Enter */
                if (vk == VK_TAB || vk == VK_ESCAPE || vk == VK_F4 ||
                    vk == VK_SPACE || vk == VK_RETURN)
                    return 1;
            }

            /* Ctrl+Esc 打开开始菜单；但保留 Ctrl+Shift+Esc（任务管理器） */
            if (vk == VK_ESCAPE && CtrlDown() && !ShiftDown())
                return 1;
        }
    }
    return CallNextHookEx(g_kbHook, code, wp, lp);
}

static LRESULT CALLBACK MsProc(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0)
        InterlockedExchange64(&g_lastInputTick, (LONGLONG)GetTickCount64());
    return CallNextHookEx(g_msHook, code, wp, lp);
}

void HooksInstall(void)
{
    if (!g_kbHook)
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, g_hInst, 0);
    if (!g_msHook)
        g_msHook = SetWindowsHookExW(WH_MOUSE_LL, MsProc, g_hInst, 0);
}

void HooksUninstall(void)
{
    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = NULL; }
    if (g_msHook) { UnhookWindowsHookEx(g_msHook); g_msHook = NULL; }
}
