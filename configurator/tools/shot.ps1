# Capture the configurator main window for visual inspection.
param([string]$Proc = "ACOCConfigurator", [string]$Out = "_test\shot.png",
      [int]$Click = 0, [int]$Click2 = 0)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
}
"@

Add-Type -AssemblyName System.Drawing
$p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Error "process $Proc is not running"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Error "no main window handle"; exit 1 }

[void][W]::ShowWindow($h, 5)
[void][W]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400

foreach ($cid in @($Click, $Click2)) {
  if ($cid -le 0) { continue }
  $c = [W]::GetDlgItem($h, $cid)
  if ($c -eq [IntPtr]::Zero) { Write-Host "no control $cid"; continue }
  [void][W]::SendMessage($c, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # BM_CLICK
  Write-Host "clicked control $cid"
  Start-Sleep -Milliseconds 500
}
Start-Sleep -Milliseconds 500

$r = New-Object W+RECT
[void][W]::GetWindowRect($h, [ref]$r)
$w = $r.R - $r.L
$ht = $r.B - $r.T
Write-Host ("window {0}x{1} at ({2},{3})" -f $w, $ht, $r.L, $r.T)

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][W]::PrintWindow($h, $dc, 2)
$g.ReleaseHdc($dc)
$g.Dispose()
$full = Join-Path (Get-Location) $Out
$bmp.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $full"
