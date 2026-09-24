Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class T {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
}
"@
$p = Get-Process -Name ACOCConfigurator -ErrorAction SilentlyContinue | Select-Object -First 1
$main = $p.MainWindowHandle
$page = [T]::GetDlgItem($main, 901)
$cb = [T+EnumProc]{
  param($h, $l)
  $c = New-Object System.Text.StringBuilder 128
  $t = New-Object System.Text.StringBuilder 128
  [void][T]::GetClassName($h, $c, 128)
  [void][T]::GetWindowText($h, $t, 128)
  Write-Host ("  {0,-16} id={1,-6} vis={2,-6} text='{3}'" -f $c.ToString(), [T]::GetDlgCtrlID($h), [T]::IsWindowVisible($h), $t.ToString())
  return $true
}
Write-Host "descendants of page 901:"
[void][T]::EnumChildWindows($page, $cb, [IntPtr]::Zero)
Write-Host "descendants of main:"
[void][T]::EnumChildWindows($main, $cb, [IntPtr]::Zero)
