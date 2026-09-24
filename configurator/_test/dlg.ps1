Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class G {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
}
"@
$p = Get-Process -Name ACOCConfigurator -ErrorAction SilentlyContinue | Select-Object -First 1
$pid2 = $p.Id
$saved = [IntPtr]::Zero
$find = [G+EnumProc]{
  param($h,$l)
  $q=0; [void][G]::GetWindowThreadProcessId($h,[ref]$q)
  if ($q -eq $pid2) {
    $sb = New-Object System.Text.StringBuilder 64
    [void][G]::GetClassName($h,$sb,64)
    if ($sb.ToString() -eq "#32770") { $script:saved = $h; return $false }
  }
  return $true
}
[void][G]::EnumWindows($find, [IntPtr]::Zero)
Write-Host "dialog = $saved"
if ($saved -eq [IntPtr]::Zero) { exit }
$show = [G+EnumProc]{
  param($h,$l)
  $c = New-Object System.Text.StringBuilder 128
  $t = New-Object System.Text.StringBuilder 200
  [void][G]::GetClassName($h,$c,128)
  [void][G]::GetWindowText($h,$t,200)
  Write-Host ("  {0,-20} id={1,-6} '{2}'" -f $c.ToString(), [G]::GetDlgCtrlID($h), $t.ToString())
  return $true
}
[void][G]::EnumChildWindows($saved, $show, [IntPtr]::Zero)
