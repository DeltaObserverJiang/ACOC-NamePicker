# 端到端跑一遍「从点名器导入」：开程序 -> 点导入 -> 选文件 -> 回读工作区，
# 再点导出，确认默认文件名已经指回导入的那个文件。
# 文件必须存成 UTF-8 带 BOM，否则 PowerShell 5.1 按 GBK 解出乱码。
param([string]$Exe = "", [string]$Src = "")

$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Exe) { $Exe = Join-Path $Repo "configurator\build\bin\ACOCConfigurator.exe" }
if (-not $Src) { $Src = Join-Path $Repo "configurator\_test\_all.html" }

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class B2 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, StringBuilder l);
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
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);

  public static IntPtr FindTopDialog(IntPtr owner, int pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      if (!IsWindowVisible(h)) return true;
      int wp; GetWindowThreadProcessId(h, out wp);
      if (wp != pid) return true;
      var sb = new StringBuilder(64);
      GetClassName(h, sb, 64);
      if (sb.ToString() != "#32770") return true;
      if (GetWindow(h, 4) != owner) return true;
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }

  static IntPtr DeepFindVisible(IntPtr root, string cls, int depth) {
    if (depth > 14 || root == IntPtr.Zero) return IntPtr.Zero;
    if (Cls(root) == cls && IsWindowVisible(root)) return root;
    for (IntPtr c = FindWindowEx(root, IntPtr.Zero, null, IntPtr.Zero);
         c != IntPtr.Zero;
         c = FindWindowEx(root, c, null, IntPtr.Zero)) {
      IntPtr r = DeepFindVisible(c, cls, depth + 1);
      if (r != IntPtr.Zero) return r;
    }
    return IntPtr.Zero;
  }

  // 在整棵对话框里找第一个「可见的、类名是 cls 且里面有可见 Edit」的控件
  static IntPtr ComboEditIn(IntPtr root, string cls, int depth) {
    if (depth > 14 || root == IntPtr.Zero) return IntPtr.Zero;
    if (Cls(root) == cls && IsWindowVisible(root)) {
      IntPtr e = DeepFindVisible(root, "Edit", 0);
      if (e != IntPtr.Zero) return e;
    }
    for (IntPtr c = FindWindowEx(root, IntPtr.Zero, null, IntPtr.Zero);
         c != IntPtr.Zero;
         c = FindWindowEx(root, c, null, IntPtr.Zero)) {
      IntPtr r = ComboEditIn(c, cls, depth + 1);
      if (r != IntPtr.Zero) return r;
    }
    return IntPtr.Zero;
  }

  // 「文件名」那个输入框。地址栏里也有一个 ComboBoxEx32/ComboBox，
  // 但那是隐藏的，必须按可见性挑，否则会拿到地址栏的空框。
  public static IntPtr FindFileNameBox(IntPtr dlg) {
    IntPtr e = ComboEditIn(dlg, "ComboBoxEx32", 0);
    if (e != IntPtr.Zero) return e;
    e = ComboEditIn(dlg, "ComboBox", 0);
    if (e != IntPtr.Zero) return e;
    // 实在没有就退回到对话框里第一个可见的 Edit
    return DeepFindVisible(dlg, "Edit", 0);
  }

  // 对话框里那个默认按钮，找 id 1 或 id 2 都行
  public static IntPtr FirstButton(IntPtr dlg, params int[] ids) {
    foreach (int i in ids) {
      IntPtr b = GetDlgItem(dlg, i);
      if (b != IntPtr.Zero) return b;
    }
    return FindWindowEx(dlg, IntPtr.Zero, "Button", IntPtr.Zero);
  }

  public static string Cls(IntPtr h) { var sb = new StringBuilder(128); GetClassName(h, sb, 128); return sb.ToString(); }
  public static string Txt(IntPtr h) { var sb = new StringBuilder(512); GetWindowText(h, sb, 512); return sb.ToString(); }

  // GetWindowTextLength 对别的进程里的控件一律返回 0，只能自己给个够大的缓冲
  public static string Text(IntPtr h) {
    var sb = new StringBuilder(2048);
    SendMessage(h, 0x000D, (IntPtr)2048, sb);
    return sb.ToString();
  }
}
"@

$WM_SETTEXT = 0x000C
# 结果同时写进一个 UTF-8 文件：控制台代码页会把中文搅成乱码，读不得
$script:log = New-Object System.Collections.ArrayList
function Note($t) { [void]$script:log.Add($t); Write-Host $t }
$LogPath = Join-Path (Join-Path $Repo "configurator\_test") "import_e2e.txt"

function ClickBtn($hwnd) {
  # BM_CLICK 用 PostMessage 发：处理函数里可能弹模态框，
  # SendMessage 会一直等它返回，而它正等着对话框，双方死锁。
  [void][B2]::PostMessage($hwnd, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 500
}

function RealClick($hwnd) {
  $r = New-Object B2+RECT
  [void][B2]::GetWindowRect($hwnd, [ref]$r)
  [void][B2]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2))
  Start-Sleep -Milliseconds 120
  [B2]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [B2]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function Ctl($parent, $id) {
  $c = [B2]::GetDlgItem($parent, $id)
  if ($c -eq [IntPtr]::Zero) { Write-Error "control $id not found"; exit 1 }
  return $c
}

# 弹出来的 MessageBox 只能用真实鼠标点，发 WM_COMMAND / BM_CLICK 都不认
function DismissBox($main, $pid) {
  $dlg = [B2]::FindTopDialog($main, $pid)
  if ($dlg -eq [IntPtr]::Zero) { return $false }
  RealClick ([B2]::FirstButton($dlg, 2, 1))
  Start-Sleep -Milliseconds 400
  return $true
}

Get-Process -Name "ACOCConfigurator" -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 700
$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds 2200
$main = $proc.MainWindowHandle
if ($main -eq [IntPtr]::Zero) { $main = (Get-Process -Id $proc.Id).MainWindowHandle }
[void][B2]::ShowWindow($main, 5)
[void][B2]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 600
$script:pid2 = $proc.Id

ClickBtn (Ctl $main 1002)                 # 切到班级名单页
Start-Sleep -Milliseconds 400
$page = Ctl $main 901
if (-not [B2]::IsWindowVisible($page)) {
  Write-Error "没切到班级名单页，后面的点击都会落空"
  Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
  exit 1
}
$cls = Ctl $page 6400
$list = Ctl $page 6200

Note ("导入前|班级={0}|学生={1}|班级名={2}" -f `
  [B2]::SendMessage($cls, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero), `
  [B2]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero), `
  [B2]::Text((Ctl $page 6001)))

# --- 点「从点名器导入」 ---
ClickBtn (Ctl $page 6108)
Start-Sleep -Milliseconds 2000

$dlg = [B2]::FindTopDialog($main, $proc.Id)
if ($dlg -eq [IntPtr]::Zero) {
  Write-Host ("诊断：main={0}；本进程可见顶层窗口：" -f $main)
  [B2]::EnumWindows({
    param($h, $l)
    if (-not [B2]::IsWindowVisible($h)) { return $true }
    $wp = 0; [void][B2]::GetWindowThreadProcessId($h, [ref]$wp)
    if ($wp -ne $script:pid2) { return $true }

    Write-Host ("  类={0} 标题='{1}' owner={2}" -f [B2]::Cls($h), [B2]::Txt($h), [B2]::GetWindow($h, 4))
    return $true
  }, [IntPtr]::Zero) | Out-Null
  Write-Error "打开文件对话框没出现"; exit 1
}
$box = [B2]::FindFileNameBox($dlg)
if ($box -eq [IntPtr]::Zero) { Write-Error "找不到文件名输入框"; exit 1 }
[void][B2]::SendMessage($box, $WM_SETTEXT, [IntPtr]::Zero, $Src)
Start-Sleep -Milliseconds 400
ClickBtn ([B2]::FirstButton($dlg, 1, 2))
Start-Sleep -Milliseconds 1800

# 工作区本来就空，不该弹确认框；真弹了就顺手点掉
if ([B2]::FindTopDialog($main, $proc.Id) -ne [IntPtr]::Zero) { [void](DismissBox $main $proc.Id) }
Start-Sleep -Milliseconds 800
if ([B2]::FindTopDialog($main, $proc.Id) -ne [IntPtr]::Zero) { [void](DismissBox $main $proc.Id) }
Start-Sleep -Milliseconds 600

$clsCount = [B2]::SendMessage($cls, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
$stuCount = [B2]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
$clsName = [B2]::Text((Ctl $page 6001))
Note ("导入后|班级={0}|学生={1}|班级名={2}" -f $clsCount, $stuCount, $clsName)

# 切到第二个班级看看学生表有没有跟着换
ClickBtn (Ctl $page 6414)                 # 下移
Start-Sleep -Milliseconds 500
Note ("下移后|班级={0}|学生={1}|班级名={2}" -f `
  [B2]::SendMessage($cls, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero), `
  [B2]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero), `
  [B2]::Text((Ctl $page 6001)))

# --- 点导出，确认默认文件名指回导入的那个文件 ---
ClickBtn (Ctl $main 2001)
Start-Sleep -Milliseconds 2200
$sdlg = [B2]::FindTopDialog($main, $proc.Id)
if ($sdlg -eq [IntPtr]::Zero) { Write-Error "保存对话框没出现"; exit 1 }
$sbox = [B2]::FindFileNameBox($sdlg)
$sname = [B2]::Text($sbox)
Note ("导出默认文件名={0}" -f $sname)
# 保存对话框会直接切到源文件所在目录，文件名框里只显示文件名
$wantName = [System.IO.Path]::GetFileName($Src)
if ($sname -eq $wantName) { Note "源文件记忆=一致" }
else { Note ("源文件记忆=不一致|期望=" + $wantName) }

ClickBtn ([B2]::FirstButton($sdlg, 2, 1))   # 取消，不要真写出文件
Start-Sleep -Milliseconds 600
if ([B2]::FindTopDialog($main, $proc.Id) -ne [IntPtr]::Zero) { [void](DismissBox $main $proc.Id) }

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
[System.IO.File]::WriteAllLines($LogPath, [string[]]$script:log,
  (New-Object System.Text.UTF8Encoding($false)))
Write-Host "DONE"
