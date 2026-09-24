param([string]$Proc = "ACOCConfigurator")
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class P {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, string win);
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@
$p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
$main = $p.MainWindowHandle
$page = [P]::GetDlgItem($main, 901)
$list = [P]::GetDlgItem($page, 6200)
$btn  = [P]::GetDlgItem($page, 6102)
Write-Host ("fg={0} main={1} btn={2}" -f [P]::GetForegroundWindow(), $main, $btn)
[void][P]::SendMessage($btn, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 120
$n = [P]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
$e = [P]::FindWindowEx($list, [IntPtr]::Zero, "EDIT", $null)
Write-Host ("t=120ms count={0} edit={1}" -f $n, $e)
Start-Sleep -Milliseconds 500
$n2 = [P]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
$e2 = [P]::FindWindowEx($list, [IntPtr]::Zero, "EDIT", $null)
Write-Host ("t=620ms count={0} edit={1}" -f $n2, $e2)
