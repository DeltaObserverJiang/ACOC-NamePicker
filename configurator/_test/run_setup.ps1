# Click a button in a setup/uninstall window and report the result.
# The window is picked by class name + title so stale processes left over from
# an aborted run can never be mistaken for the one we just launched.
param([string]$Proc = "ACOCConfiguratorSetup",
      [string]$Title = "安装 A.C.O.C. 点名系统配置器",
      [int]$TargetPid = 0,
      [int]$Click = 0,
      [int]$Uncheck = 0)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class S {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);

  public static IntPtr FindMain(string title, int wantPid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      int pid; GetWindowThreadProcessId(h, out pid);
      if (wantPid != 0 && pid != wantPid) return true;
      var sb = new StringBuilder(64);
      GetClassName(h, sb, 64);
      if (sb.ToString() != "AcocSetup") return true;
      var t = new StringBuilder(256);
      GetWindowText(h, t, 256);
      if (title != "" && t.ToString() != title) return true;
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }
}
"@

$main = [IntPtr]::Zero
if ($TargetPid -ne 0) { $main = [S]::FindMain($Title, $TargetPid) }
if ($main -eq [IntPtr]::Zero) { $main = [S]::FindMain($Title, 0) }
if ($main -eq [IntPtr]::Zero) { Write-Error "no window titled '$Title'"; exit 1 }
$owner = 0
[void][S]::GetWindowThreadProcessId($main, [ref]$owner)
Write-Host "window=$main pid=$owner"

[void][S]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 300
if ($Uncheck -gt 0) {
  $c = [S]::GetDlgItem($main, $Uncheck)
  [void][S]::SendMessage($c, 0x00F1, [IntPtr]0, [IntPtr]::Zero)   # BM_SETCHECK / UNCHECKED
  Write-Host "unchecked $Uncheck"
}
if ($Click -gt 0) {
  $c = [S]::GetDlgItem($main, $Click)
  if ($c -eq [IntPtr]::Zero) { Write-Error "control $Click not found"; exit 1 }
  # PostMessage: BM_CLICK 是同步的，处理函数里弹模态框会把这里卡死
  [void][S]::PostMessage($c, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
  Write-Host "clicked $Click"
  Start-Sleep -Milliseconds 2500
}
$st = [S]::GetDlgItem($main, 108)
$sb = New-Object System.Text.StringBuilder 200
[void][S]::GetWindowText($st, $sb, 200)
Write-Host "status: $($sb.ToString())"
