# 端到端跑一遍「点名器更新」：开程序 -> 切到更新页 -> 选一份旧版点名器 ->
# 直接输出最新版 -> 核对输出文件里的班级与人数，并确认名单已回到工作区。
# 文件存成 UTF-8 带 BOM，PowerShell 5.1 才认中文。
param([string]$Exe = "", [string]$Src = "", [string]$OutDir = "")

$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Exe) { $Exe = Join-Path $Repo "configurator\build\bin\ACOCConfigurator.exe" }
if (-not $OutDir) { $OutDir = Join-Path $Repo "configurator\_test" }
if (-not $Src) { $Src = Join-Path $Repo "configurator\_test\old_v04.html" }
$Dst = Join-Path $OutDir "updated_e2e.html"

# 旧版夹具不入库（_test 下的 *.html 本就被忽略），需要时从标签里取一份 v0.4 的成品。
# 必须走 cmd 重定向：PowerShell 的 > 会把 git 的原始字节按控制台编码重新编码，
# 中文名字会当场烂掉。取不到就退而用当前的 index.html（同样是一份合法点名器）。
if (-not (Test-Path $Src)) {
  $Src = Join-Path $Repo "configurator\_test\old_v04.html"
  Start-Process -FilePath "cmd.exe" -Wait -NoNewWindow -ArgumentList @(
    "/c", "git -C `"$Repo`" show v0.4:index.html > `"$Src`"")
  if (-not (Test-Path $Src)) {
    Write-Host "取不到 v0.4 的成品，改用 index.html"
    $Src = Join-Path $Repo "index.html"
  }
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class B4 {
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

  public static IntPtr FindFileNameBox(IntPtr dlg) {
    IntPtr e = ComboEditIn(dlg, "ComboBoxEx32", 0);
    if (e != IntPtr.Zero) return e;
    e = ComboEditIn(dlg, "ComboBox", 0);
    if (e != IntPtr.Zero) return e;
    return DeepFindVisible(dlg, "Edit", 0);
  }

  public static IntPtr FirstButton(IntPtr dlg, params int[] ids) {
    foreach (int i in ids) {
      IntPtr b = GetDlgItem(dlg, i);
      if (b != IntPtr.Zero) return b;
    }
    return FindWindowEx(dlg, IntPtr.Zero, "Button", IntPtr.Zero);
  }

  public static string Cls(IntPtr h) { var sb = new StringBuilder(128); GetClassName(h, sb, 128); return sb.ToString(); }
  public static string Txt(IntPtr h) { var sb = new StringBuilder(512); GetWindowText(h, sb, 512); return sb.ToString(); }
  public static string Text(IntPtr h) {
    var sb = new StringBuilder(2048);
    SendMessage(h, 0x000D, (IntPtr)2048, sb);
    return sb.ToString();
  }
}
"@

$WM_SETTEXT = 0x000C
$script:log = New-Object System.Collections.ArrayList
$script:bad = 0
function Note($t) { [void]$script:log.Add($t); Write-Host $t }
function Check($cond, $what) {
  if ($cond) { Note ("PASS " + $what) } else { $script:bad++; Note ("FAIL " + $what) }
}

$LogPath = Join-Path $OutDir "update_e2e.txt"

function ClickBtn($hwnd) {
  [void][B4]::PostMessage($hwnd, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 500
}

function RealClick($hwnd) {
  $r = New-Object B4+RECT
  [void][B4]::GetWindowRect($hwnd, [ref]$r)
  [void][B4]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2))
  Start-Sleep -Milliseconds 120
  [B4]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [B4]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function Ctl($parent, $id) {
  $c = [B4]::GetDlgItem($parent, $id)
  if ($c -eq [IntPtr]::Zero) { Write-Error "control $id not found"; exit 1 }
  return $c
}

# MessageBox 只认真实鼠标。参数别叫 $pid：那是 PowerShell 的只读自动变量
function DismissBox($main, $owner) {
  $dlg = [B4]::FindTopDialog($main, $owner)
  if ($dlg -eq [IntPtr]::Zero) { return $false }
  RealClick ([B4]::FirstButton($dlg, 2, 1))
  Start-Sleep -Milliseconds 400
  return $true
}

if (Test-Path $Dst) { Remove-Item $Dst -Force }

Get-Process -Name "ACOCConfigurator" -ErrorAction SilentlyContinue |
  Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 700
$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds 2200
$main = $proc.MainWindowHandle
if ($main -eq [IntPtr]::Zero) { $main = (Get-Process -Id $proc.Id).MainWindowHandle }
[void][B4]::ShowWindow($main, 5)
[void][B4]::SetForegroundWindow($main)
Start-Sleep -Milliseconds 700

# --- 切到「点名器更新」 ---
ClickBtn (Ctl $main 1003)
Start-Sleep -Milliseconds 500
$page = Ctl $main 902
Check ([B4]::IsWindowVisible($page)) "切到点名器更新页"
Note ("更新前提示=" + [B4]::Text((Ctl $page 6497)))

# --- 点主按钮，选旧版点名器 ---
ClickBtn (Ctl $page 6500)
Start-Sleep -Milliseconds 2000
$dlg = [B4]::FindTopDialog($main, $proc.Id)
if ($dlg -eq [IntPtr]::Zero) {
  Note "FAIL 打开对话框没出现"
  Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
  [System.IO.File]::WriteAllLines($LogPath, [string[]]$script:log,
    (New-Object System.Text.UTF8Encoding($false)))
  exit 1
}
Note ("第一个对话框标题=" + [B4]::Txt($dlg))
$box = [B4]::FindFileNameBox($dlg)
[void][B4]::SendMessage($box, $WM_SETTEXT, [IntPtr]::Zero, $Src)
Start-Sleep -Milliseconds 400
ClickBtn ([B4]::FirstButton($dlg, 1, 2))
Start-Sleep -Milliseconds 2600

# --- 应当出现保存对话框，且默认文件名带着第一个班级名 ---
$sdlg = [B4]::FindTopDialog($main, $proc.Id)
if ($sdlg -eq [IntPtr]::Zero) {
  Note "FAIL 保存对话框没出现（多半弹了报错框）"
  Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
  [System.IO.File]::WriteAllLines($LogPath, [string[]]$script:log,
    (New-Object System.Text.UTF8Encoding($false)))
  exit 1
}
Note ("第二个对话框标题=" + [B4]::Txt($sdlg))
$sbox = [B4]::FindFileNameBox($sdlg)
$sname = [B4]::Text($sbox)
Note ("默认输出文件名=" + $sname)
Check ($sname -like "*_点名系统_v0.5.html") "默认文件名带班级名与版本号"
[void][B4]::SendMessage($sbox, $WM_SETTEXT, [IntPtr]::Zero, $Dst)
Start-Sleep -Milliseconds 400
ClickBtn ([B4]::FirstButton($sdlg, 1, 2))
Start-Sleep -Milliseconds 1800

# --- 完成提示 ---
$doneDlg = [B4]::FindTopDialog($main, $proc.Id)
if ($doneDlg -ne [IntPtr]::Zero) { Note ("完成提示标题=" + [B4]::Txt($doneDlg)) }
Check (DismissBox $main $proc.Id) "出现并关掉「更新完成」提示"
Start-Sleep -Milliseconds 600
Note ("更新后提示=" + [B4]::Text((Ctl $page 6497)))

# --- 落到磁盘上的成品 ---
Check (Test-Path $Dst) "输出了文件"
if (Test-Path $Dst) {
  $text = [System.IO.File]::ReadAllText($Dst, [System.Text.Encoding]::UTF8)
  $btns = ([regex]::Matches($text, 'id="loadPresetBtn')).Count
  Check ($text.Contains("const CLASS_PRESETS = [")) "成品里有 CLASS_PRESETS"
  Check ($btns -eq 3) ("成品里预设按钮 3 个（实际 " + $btns + "）")
  Check ($text.Contains("A2班 (预设)")) "班级名保持原文（带「 (预设)」）"
  Check ($text.Contains("acoc.namepicker.save.v1")) "成品仍是同一份本机存档键"
  Check ($text.Contains("fonts.googleapis.com")) "成品已恢复外部字体库"
  Check ($text.Contains("const FEATURES = {")) "成品带着功能开关"
}

# --- 名单回到工作区 ---
ClickBtn (Ctl $main 1002)
Start-Sleep -Milliseconds 500
$page2 = Ctl $main 901
$cls = Ctl $page2 6400
$list = Ctl $page2 6200
$clsCount = [B4]::SendMessage($cls, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
$stuCount = [B4]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
$clsName = [B4]::Text((Ctl $page2 6001))
Note ("工作区|班级={0}|学生={1}|当前班名={2}" -f $clsCount, $stuCount, $clsName)
Check ($clsCount -eq 3) "工作区拿到 3 个班级"
Check ($stuCount -eq 52) "当前班 52 人"
Check ($clsName -eq "A2班 (预设)") "当前班名与原文一致"

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
[System.IO.File]::WriteAllLines($LogPath, [string[]]$script:log,
  (New-Object System.Text.UTF8Encoding($false)))
Write-Host ("DONE bad=" + $script:bad)
if ($script:bad -gt 0) { exit 1 }
