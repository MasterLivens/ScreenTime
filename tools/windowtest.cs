using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace ST
{
    public delegate bool EnumProc(IntPtr h, IntPtr l);

    public static class Win
    {
        [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int max);
        [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
        [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
        [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int idx);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h, string s);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int max);
        [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
        [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
        [DllImport("user32.dll")] public static extern IntPtr GetFocus();

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT { public int Left, Top, Right, Bottom; }

        public static List<IntPtr> Find()
        {
            return FindClass("ScreenTimeStatus");
        }

        public static List<IntPtr> FindClass(string cls)
        {
            var list = new List<IntPtr>();
            EnumWindows((h, l) =>
            {
                var sb = new StringBuilder(256);
                GetClassName(h, sb, 255);
                if (sb.ToString() == cls) list.Add(h);
                return true;
            }, IntPtr.Zero);
            return list;
        }

        public static string Dump(IntPtr h)
        {
            RECT r; GetWindowRect(h, out r);
            return string.Format("hwnd={0} visible={1} size={2}x{3}",
                                 h, IsWindowVisible(h), r.Right - r.Left, r.Bottom - r.Top);
        }
    }
}
