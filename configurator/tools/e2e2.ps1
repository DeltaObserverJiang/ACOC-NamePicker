# 多班级端到端：建第二个班级、分别加学生、导出。
# 用真实鼠标点击——BM_CLICK 会把焦点交回按钮，原地编辑器会提前提交。
# 文件保存为 UTF-8 带 BOM。
param([string]$Proc = "ACOCConfigurator",
      [string]$Save = "R:\NamePicker\configurator\_test\gui_export2.html")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class B {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, IntPtr win);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);

  public static IntPtr FindTopDialog(IntPtr owner, int pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      int wp; GetWindowThreadProcessId(h, out wp);
      if (wp != pid) return true;
      var sb = new System.Text.StringBuilder(64);
      GetClassName(h, sb, 64);
      if (sb.ToString() != "#32770") return true;
      if (GetWindow(h, 4) != owner) return true;
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }

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
[void][B]::ShowWindow($main, 5)
[void][B]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 600

function RealClick($hwnd) {
  $r = New-Object B+RECT
  [void][B]::GetWindowRect($hwnd, [ref]$r)
  [void][B]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2))
  Start-Sleep -Milliseconds 120
  [B]::mouse_event($DOWN, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [B]::mouse_event($UP, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function Ctl($parent, $id) {
  $c = [B]::GetDlgItem($parent, $id)
  if ($c -eq [IntPtr]::Zero) { Write-Error "control $id not found"; exit 1 }
  return $c
}

function AddStudents($page, $list, $names) {
  foreach ($n in $names) {
    RealClick (Ctl $page 6102)
    $ce = [B]::FindWindowEx($list, [IntPtr]::Zero, "EDIT", [IntPtr]::Zero)
    if ($ce -eq [IntPtr]::Zero) { Write-Error "cell editor did not open"; exit 1 }
    [void][B]::SendMessage($ce, $WM_SETTEXT, [IntPtr]::Zero, $n)
    [void][B]::SendMessage($ce, $WM_KEYDOWN, [IntPtr]13, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
  }
}

RealClick (Ctl $main 1002)                 # 切到名单页
$page = Ctl $main 901
$list = Ctl $page 6200
$cls  = Ctl $page 6400

# 第一个班级：改名并加人
[void][B]::SendMessage((Ctl $page 6001), $WM_SETTEXT, [IntPtr]::Zero, "高二(3)班")
Start-Sleep -Milliseconds 200
AddStudents $page $list @("林知遥", "陈慕白", "周砚清")
Write-Host "class 1 students = $([B]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero))"

# 第二个班级
RealClick (Ctl $page 6410)                 # 新建
[void][B]::SendMessage((Ctl $page 6001), $WM_SETTEXT, [IntPtr]::Zero, "高三(1)班")
Start-Sleep -Milliseconds 200
AddStudents $page $list @("沈聿", "顾南枝", "苏行止")
Write-Host "class 2 students = $([B]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero))"
Write-Host "class rows = $([B]::SendMessage($cls, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero))"

# 切回第一个班级，确认学生表跟着换
RealClick (Ctl $page 6413)                 # 上移：把高三(1)班换到首位
Write-Host "after swap, rows = $([B]::SendMessage($cls, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero))"

if (Test-Path $Save) { Remove-Item $Save -Force }
RealClick (Ctl $main 2001)
Start-Sleep -Milliseconds 2500

$dlg = [B]::FindTopDialog($main, $p.Id)
if ($dlg -eq [IntPtr]::Zero) { Write-Error "save dialog did not appear"; exit 1 }
$box = [B]::FindFileNameBox($dlg)
if ($box -eq [IntPtr]::Zero) { Write-Error "filename box not found"; exit 1 }
[void][B]::SendMessage($box, $WM_SETTEXT, [IntPtr]::Zero, $Save)
Start-Sleep -Milliseconds 400
$ok = [B]::GetDlgItem($dlg, 1)
if ($ok -ne [IntPtr]::Zero) {
  [void][B]::SendMessage($ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
} else {
  [void][B]::SendMessage($dlg, 0x0111, [IntPtr]1, [IntPtr]::Zero)
}
Start-Sleep -Milliseconds 2500

$done = [B]::FindTopDialog($main, $p.Id)
if ($done -ne [IntPtr]::Zero) {
  [void][B]::SendMessage($done, 0x0111, [IntPtr]1, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 600
}

if (Test-Path $Save) {
  Write-Host ("exported -> {0} ({1} bytes)" -f $Save, (Get-Item $Save).Length)
} else {
  Write-Error "export produced no file"
  exit 1
}
