Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class V {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
"@
$p = Get-Process -Name ACOCConfigurator -ErrorAction SilentlyContinue | Select-Object -First 1
$pid2 = $p.Id
$cb = [V+EnumProc]{
  param($h, $l)
  $q = 0
  [void][V]::GetWindowThreadProcessId($h, [ref]$q)
  if ($q -eq $pid2 -and [V]::IsWindowVisible($h)) {
    $c = New-Object System.Text.StringBuilder 200
    $t = New-Object System.Text.StringBuilder 300
    [void][V]::GetClassName($h, $c, 200)
    [void][V]::GetWindowText($h, $t, 300)
    Write-Host ("  {0,-22} '{1}'" -f $c.ToString(), $t.ToString())
  }
  return $true
}
Write-Host "visible top-level windows of ACOCConfigurator:"
[void][V]::EnumWindows($cb, [IntPtr]::Zero)
