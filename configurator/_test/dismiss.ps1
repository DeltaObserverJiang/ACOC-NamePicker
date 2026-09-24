# Click OK on whatever modal message box the setup/uninstall flow put up.
param([int]$TargetPid = 0, [string]$Proc = "uninstall")

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class M {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@

if ($TargetPid -eq 0) {
  $p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { Write-Host "process gone"; exit 0 }
  $TargetPid = $p.Id
}
$want = $TargetPid
$script:dlg = [IntPtr]::Zero
$cb = [M+EnumProc]{
  param($h, $l)
  $q = 0; [void][M]::GetWindowThreadProcessId($h, [ref]$q)
  if ($q -eq $want -and [M]::IsWindowVisible($h)) {
    $s = New-Object System.Text.StringBuilder 64
    [void][M]::GetClassName($h, $s, 64)
    if ($s.ToString() -eq "#32770") { $script:dlg = $h; return $false }
  }
  return $true
}
[void][M]::EnumWindows($cb, [IntPtr]::Zero)
if ($script:dlg -eq [IntPtr]::Zero) { Write-Host "no dialog"; exit 0 }
[void][M]::SendMessage($script:dlg, 0x0111, [IntPtr]1, [IntPtr]::Zero)
Write-Host "dismissed $script:dlg"
