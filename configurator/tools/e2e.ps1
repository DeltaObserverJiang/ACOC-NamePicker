# Drive the configurator the way a user would: switch to the roster page,
# name the class, add students by clicking with the real mouse, then export.
# Real mouse input matters -- BM_CLICK hands focus back to the button, which
# makes the in-place cell editor commit before we can type into it.
# ASCII only except the roster data; file is saved as UTF-8 with BOM.
param([string]$Proc = "ACOCConfigurator",
      [string]$Save = "R:\NamePicker\configurator\_test\gui_export.html")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class A {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  // win must be a real NULL, not "" -- an empty string only matches untitled windows
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, IntPtr win);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);

  // The save dialog is an owned top-level #32770 belonging to our process.
  public static IntPtr FindTopDialog(IntPtr owner, int pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      int wp; GetWindowThreadProcessId(h, out wp);
      if (wp != pid) return true;
      var sb = new System.Text.StringBuilder(64);
      GetClassName(h, sb, 64);
      if (sb.ToString() != "#32770") return true;
      if (GetWindow(h, 4) != owner) return true;   // GW_OWNER
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }

  // The filename box lives at DUIViewWndClassName -> DirectUIHWND ->
  // ... -> ComboBox -> Edit. The address bar also has an Edit, so start the
  // walk from the DUI subtree and take the first ComboBox-backed Edit found.
  public static IntPtr FindFileNameBox(IntPtr dlg) {
    IntPtr dui = FindWindowEx(dlg, IntPtr.Zero, "DUIViewWndClassName", IntPtr.Zero);
    if (dui == IntPtr.Zero) dui = FindWindowEx(dlg, IntPtr.Zero, "DirectUIHWND", IntPtr.Zero);
    if (dui == IntPtr.Zero) return IntPtr.Zero;
    return FirstComboEdit(dui, 0);
  }

  static IntPtr FirstComboEdit(IntPtr root, int depth) {
    if (depth > 8) return IntPtr.Zero;
    for (IntPtr c = FindWindowEx(root, IntPtr.Zero, "ComboBox", IntPtr.Zero);
         c != IntPtr.Zero;
         c = FindWindowEx(root, c, "ComboBox", IntPtr.Zero)) {
      IntPtr e = FindWindowEx(c, IntPtr.Zero, "Edit", IntPtr.Zero);
      if (e != IntPtr.Zero) return e;
    }
    for (IntPtr c = FindWindowEx(root, IntPtr.Zero, null, IntPtr.Zero);
         c != IntPtr.Zero;
         c = FindWindowEx(root, c, null, IntPtr.Zero)) {
      IntPtr r = FirstComboEdit(c, depth + 1);
      if (r != IntPtr.Zero) return r;
    }
    return IntPtr.Zero;
  }
}
"@

$WM_SETTEXT = 0x000C
$WM_KEYDOWN = 0x0100
$DOWN = 0x0002
$UP   = 0x0004

$p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Error "process $Proc is not running"; exit 1 }
$main = $p.MainWindowHandle
[void][A]::ShowWindow($main, 5)
[void][A]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 600

function RealClick($hwnd) {
  $r = New-Object A+RECT
  [void][A]::GetWindowRect($hwnd, [ref]$r)
  $x = [int](($r.L + $r.R) / 2)
  $y = [int](($r.T + $r.B) / 2)
  [void][A]::SetCursorPos($x, $y)
  Start-Sleep -Milliseconds 120
  [A]::mouse_event($DOWN, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [A]::mouse_event($UP, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function Ctl($parent, $id) {
  $c = [A]::GetDlgItem($parent, $id)
  if ($c -eq [IntPtr]::Zero) { Write-Error "control $id not found"; exit 1 }
  return $c
}

RealClick (Ctl $main 1002)                # switch to the roster page
$page = Ctl $main 901
$list = Ctl $page 6200

[void][A]::SendMessage((Ctl $page 6001), $WM_SETTEXT, [IntPtr]::Zero, "高二(3)班")
Write-Host "class name set"

$names = @("林知遥", "陈慕白", "周砚清", "沈聿", "顾南枝")
foreach ($n in $names) {
  RealClick (Ctl $page 6102)              # "add" -- opens the name cell editor
  $ce = [A]::FindWindowEx($list, [IntPtr]::Zero, "EDIT", [IntPtr]::Zero)
  if ($ce -eq [IntPtr]::Zero) { Write-Error "cell editor did not open"; exit 1 }
  [void][A]::SendMessage($ce, $WM_SETTEXT, [IntPtr]::Zero, $n)
  [void][A]::SendMessage($ce, $WM_KEYDOWN, [IntPtr]13, [IntPtr]::Zero)   # VK_RETURN
  Start-Sleep -Milliseconds 250
  Write-Host "added $n"
}

$cnt = [A]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)   # LVM_GETITEMCOUNT
Write-Host "rows now = $cnt"

if (Test-Path $Save) { Remove-Item $Save -Force }
RealClick (Ctl $main 2001)                # export
Start-Sleep -Milliseconds 2500

# A plain GetSaveFileName dialog is up. Fill its filename box directly and
# press OK -- far steadier than SendKeys against a Chinese-named default.
$dlg = [A]::FindTopDialog($main, $p.Id)
if ($dlg -eq [IntPtr]::Zero) { Write-Error "save dialog did not appear"; exit 1 }
Write-Host "save dialog found: $dlg"

$box = [A]::FindFileNameBox($dlg)
if ($box -eq [IntPtr]::Zero) { Write-Error "filename box not found"; exit 1 }
[void][A]::SendMessage($box, $WM_SETTEXT, [IntPtr]::Zero, $Save)
Start-Sleep -Milliseconds 400
$ok = [A]::GetDlgItem($dlg, 1)                 # the "save" button
if ($ok -ne [IntPtr]::Zero) {
  [void][A]::SendMessage($ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)   # BM_CLICK
} else {
  [void][A]::SendMessage($dlg, 0x0111, [IntPtr]1, [IntPtr]::Zero)       # WM_COMMAND/IDOK
}
Start-Sleep -Milliseconds 2500

# the app then shows a "done" message box; dismiss it
$done = [A]::FindTopDialog($main, $p.Id)
if ($done -ne [IntPtr]::Zero) {
  Write-Host "dismissing result box"
  [void][A]::SendMessage($done, 0x0111, [IntPtr]1, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 600
}

if (Test-Path $Save) {
  Write-Host ("exported -> {0} ({1} bytes)" -f $Save, (Get-Item $Save).Length)
} else {
  Write-Error "export produced no file"
  exit 1
}
