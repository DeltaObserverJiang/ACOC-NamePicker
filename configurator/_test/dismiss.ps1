# 点掉安装/卸载流程里弹出的模态框。
# 必须用真实鼠标：对这个 MessageBox 发 WM_COMMAND/IDOK 或 BM_CLICK 都不起作用，
# 窗口照样停在那儿，而它是模态的，会一直卡住父进程不让它退出。
param([int]$TargetPid = 0, [string]$Proc = "uninstall")

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class M {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);

  public static IntPtr FindDialog(int want) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      int pid; GetWindowThreadProcessId(h, out pid);
      if (pid != want) return true;
      var sb = new StringBuilder(64);
      GetClassName(h, sb, 64);
      if (sb.ToString() != "#32770") return true;
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }

  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);

  // 模态框上那个确定按钮的控件 id 是 2
  public static void ClickOk(IntPtr dlg) {
    IntPtr b = GetDlgItem(dlg, 2);
    if (b == IntPtr.Zero) b = GetDlgItem(dlg, 1);
    RECT r; GetWindowRect(b, out r);
    SetForegroundWindow(dlg);
    System.Threading.Thread.Sleep(250);
    SetCursorPos((r.L + r.R) / 2, (r.T + r.B) / 2);
    System.Threading.Thread.Sleep(200);
    mouse_event(0x0002, 0, 0, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);
  }
}
"@

if ($TargetPid -eq 0) {
  $p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { Write-Host "process gone"; exit 0 }
  $TargetPid = $p.Id
}

for ($i = 0; $i -lt 10; $i++) {
  $dlg = [M]::FindDialog($TargetPid)
  if ($dlg -eq [IntPtr]::Zero) {
    if ($i -eq 0) { Write-Host "no dialog" } else { Write-Host "dismissed" }
    exit 0
  }
  [M]::ClickOk($dlg)
  Start-Sleep -Milliseconds 500
}
if ([M]::FindDialog($TargetPid) -eq [IntPtr]::Zero) {
  Write-Host "dismissed"
} else {
  Write-Host "STILL OPEN"
}
