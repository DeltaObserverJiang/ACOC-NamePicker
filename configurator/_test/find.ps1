Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class F {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
}
"@
$p = Get-Process -Name ACOCConfigurator -ErrorAction SilentlyContinue | Select-Object -First 1
$pid2 = $p.Id
$dlg = [IntPtr]::Zero
$find = [F+EnumProc]{
  param($h,$l)
  $q=0; [void][F]::GetWindowThreadProcessId($h,[ref]$q)
  if ($q -eq $pid2) {
    $sb = New-Object System.Text.StringBuilder 64
    [void][F]::GetClassName($h,$sb,64)
    if ($sb.ToString() -eq "#32770") { $script:dlg = $h; return $false }
  }
  return $true
}
[void][F]::EnumWindows($find, [IntPtr]::Zero)
Write-Host "dialog=$dlg"
Write-Host "--- all Edit controls in dialog ---"
$show = [F+EnumProc]{
  param($h,$l)
  $c = New-Object System.Text.StringBuilder 128
  [void][F]::GetClassName($h,$c,128)
  if ($c.ToString() -eq "Edit") {
    $t = New-Object System.Text.StringBuilder 300
    [void][F]::GetWindowText($h,$t,300)
    Write-Host ("  edit hwnd={0} id={1} text='{2}'" -f $h, [F]::GetDlgCtrlID($h), $t.ToString())
  }
  return $true
}
[void][F]::EnumChildWindows($dlg, $show, [IntPtr]::Zero)
foreach ($id in 1152, 1148, 1090, 1144) {
  Write-Host ("GetDlgItem {0} -> {1}" -f $id, [F]::GetDlgItem($dlg, $id))
}
