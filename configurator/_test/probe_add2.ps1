Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Q {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, string win);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,IntPtr e);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
}
"@
$p = Get-Process -Name ACOCConfigurator -ErrorAction SilentlyContinue | Select-Object -First 1
$main = $p.MainWindowHandle
[void][Q]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 300
$page = [Q]::GetDlgItem($main, 901)
$list = [Q]::GetDlgItem($page, 6200)
$btn  = [Q]::GetDlgItem($page, 6102)
$r = New-Object Q+RECT
[void][Q]::GetWindowRect($btn, [ref]$r)
Write-Host ("btn rect {0},{1} - {2},{3}" -f $r.L,$r.T,$r.R,$r.B)
[void][Q]::SetCursorPos([int](($r.L+$r.R)/2), [int](($r.T+$r.B)/2))
Start-Sleep -Milliseconds 150
[Q]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 50
[Q]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)
# sample the editor repeatedly right after the click
foreach ($ms in 30, 80, 200, 500) {
  Start-Sleep -Milliseconds $ms
  $n = [Q]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
  $e = [Q]::FindWindowEx($list, [IntPtr]::Zero, "EDIT", $null)
  $f = [Q]::GetFocus()
  $sb = New-Object System.Text.StringBuilder 128
  if ($f -ne [IntPtr]::Zero) { [void][Q]::GetClassName($f, $sb, 128) }
  Write-Host ("+{0}ms count={1} edit={2} focus={3} cls={4}" -f $ms,$n,$e,$f,$sb.ToString())
}
