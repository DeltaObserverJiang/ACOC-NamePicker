# Probe the configurator's control tree and list contents.
param([string]$Proc = "ACOCConfigurator")

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class D {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, string win);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
}
"@

$p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Error "not running"; exit 1 }
$main = $p.MainWindowHandle
Write-Host "main=$main"

foreach ($id in 900, 901, 1001, 1002, 2001) {
  $c = [D]::GetDlgItem($main, $id)
  Write-Host ("main child {0}: hwnd={1} vis={2}" -f $id, $c, [D]::IsWindowVisible($c))
}

$page = [D]::GetDlgItem($main, 901)
Write-Host "--- children of page 901 ---"
$child = [IntPtr]::Zero
$sb = New-Object System.Text.StringBuilder 128
while ($true) {
  $child = [D]::FindWindowEx($page, $child, $null, $null)
  if ($child -eq [IntPtr]::Zero) { break }
  [void][D]::GetClassName($child, $sb, 128)
  $id = [D]::SendMessage($child, 0x0473, [IntPtr]::Zero, [IntPtr]::Zero)  # not used, placeholder
  Write-Host ("  hwnd={0} class={1} vis={2} en={3}" -f $child, $sb.ToString(), [D]::IsWindowVisible($child), [D]::IsWindowEnabled($child))
}

$list = [D]::GetDlgItem($page, 6200)
Write-Host "listview=$list"
if ($list -ne [IntPtr]::Zero) {
  $n = [D]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)  # LVM_GETITEMCOUNT
  Write-Host "item count = $n"
  $e = [D]::FindWindowEx($list, [IntPtr]::Zero, "EDIT", $null)
  Write-Host "edit child of list = $e"
}
