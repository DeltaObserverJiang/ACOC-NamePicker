# v0.2.1 的覆盖安装流程：没装过时直接装，装过之后先切到界面内的确认页
# （按钮变成“覆盖安装”），确认后才动手。顺便量一下安装耗时是否够两秒。
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import time
import winreg

u32 = ctypes.windll.user32
SETUP = r"R:\NamePicker\configurator\installer\build\bin\ACOCConfiguratorSetup.exe"
INSTALL = os.path.join(os.environ["LOCALAPPDATA"], r"Programs\ACOCConfigurator")
REG = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\ACOCConfigurator"
TITLE = "安装 A.C.O.C. 点名系统配置器"

kInstallBtn, kCancelBtn, kStatus = 104, 105, 108

class RECT(ctypes.Structure):
    _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long),
                ("r", ctypes.c_long), ("b", ctypes.c_long)]


fails = []


def check(label, ok, extra=""):
    print(("  OK   " if ok else "  FAIL ") + label + ("  " + extra if extra else ""))
    if not ok:
        fails.append(label)


def find_window(pid, cls, title=None):
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(h, l):
        if not u32.IsWindowVisible(h):
            return True
        p = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value != pid:
            return True
        buf = ctypes.create_unicode_buffer(128)
        u32.GetClassNameW(h, buf, 128)
        if buf.value != cls:
            return True
        if title is not None:
            t = ctypes.create_unicode_buffer(256)
            u32.GetWindowTextW(h, t, 256)
            if t.value != title:
                return True
        found.append(h)
        return False

    u32.EnumWindows(cb, 0)
    return found[0] if found else None


def text_of(hwnd, ctrl_id):
    h = u32.GetDlgItem(hwnd, ctrl_id)
    if not h:
        return ""
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 2)
    u32.GetWindowTextW(h, b, n + 2)
    return b.value


def click(main, ctrl_id):
    """必须 PostMessage：BM_CLICK 是同步的，处理函数里若弹窗会当场死锁。"""
    u32.PostMessageW(u32.GetDlgItem(main, ctrl_id), 0x00F5, 0, 0)


def uncheck(main, ctrl_id):
    u32.SendMessageW(u32.GetDlgItem(main, ctrl_id), 0x00F1, 0, 0)  # BM_SETCHECK


def close_finish(main):
    """完成页默认勾着“立即运行”，不取消的话真会把配置器拉起来，
    下一轮覆盖安装就写不进正在运行的 exe。测试里统一取消。"""
    uncheck(main, 106)
    click(main, kInstallBtn)
    time.sleep(1.5)


def kill_app():
    subprocess.run(["taskkill", "/F", "/IM", "ACOCConfigurator.exe"],
                   capture_output=True)
    time.sleep(0.5)


def wait_btn(main, want, timeout=8.0):
    end = time.time() + timeout
    while time.time() < end:
        if text_of(main, kInstallBtn) == want:
            return True
        time.sleep(0.1)
    return False


def launch():
    p = subprocess.Popen([SETUP], cwd=os.path.dirname(SETUP))
    time.sleep(2.5)
    return p


def stored_version():
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG)
        return winreg.QueryValueEx(k, "DisplayVersion")[0]
    except FileNotFoundError:
        return None


def set_stored_version(v):
    k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG, 0, winreg.KEY_SET_VALUE)
    winreg.SetValueEx(k, "DisplayVersion", 0, winreg.REG_SZ, v)


def dismiss_done(pid, main):
    """卸载完会弹一个模态框。合成消息对它无效，只能真点。"""
    end = time.time() + 8
    while time.time() < end:
        d = find_window(pid, "#32770")
        if d:
            btn = u32.GetDlgItem(d, 2) or u32.GetDlgItem(d, 1)
            if btn:
                r = RECT()
                u32.GetWindowRect(btn, ctypes.byref(r))
                u32.SetForegroundWindow(d)
                time.sleep(0.25)
                u32.SetCursorPos((r.l + r.r) // 2, (r.t + r.b) // 2)
                time.sleep(0.2)
                u32.mouse_event(0x0002, 0, 0, 0, 0)
                time.sleep(0.06)
                u32.mouse_event(0x0004, 0, 0, 0, 0)
                time.sleep(0.4)
            return True
        time.sleep(0.15)
    return False


print("=== 清干净 ===")
subprocess.run(["taskkill", "/F", "/IM", "ACOCConfiguratorSetup.exe"],
               capture_output=True)
kill_app()
if os.path.isdir(INSTALL):
    shutil.rmtree(INSTALL, ignore_errors=True)
try:
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, REG)
except FileNotFoundError:
    pass
time.sleep(0.5)

print("=== 首次安装：不该出现确认页，且耗时至少两秒 ===")
p = launch()
main = find_window(p.pid, "AcocSetup", TITLE)
check("安装窗口已出现", main is not None)
t0 = time.time()
click(main, kInstallBtn)
ok = wait_btn(main, "完成", timeout=15)
elapsed = time.time() - t0
check("安装已完成", ok, "%.2f 秒" % elapsed)
check("耗时不少于两秒", elapsed >= 2.0, "实测 %.2f 秒" % elapsed)
check("没有弹出确认页（按钮直接走到完成）", ok)
check("写入注册表 0.2.1", stored_version() == "0.2.1", str(stored_version()))
close_finish(main)
kill_app()

print("=== 再装一次：应切到界面内的确认页 ===")
p = launch()
main = find_window(p.pid, "AcocSetup", TITLE)
click(main, kInstallBtn)
seen = wait_btn(main, "覆盖安装", timeout=6)
check("按钮变成“覆盖安装”（确认页出现）", seen)
check("取消按钮变成“返回”", text_of(main, kCancelBtn) == "返回",
      repr(text_of(main, kCancelBtn)))
check("确认页没有弹窗打断", find_window(p.pid, "#32770") is None)
if seen:
    click(main, kCancelBtn)          # 返回
    back = wait_btn(main, "安装", timeout=4)
    check("点“返回”回到选项页", back)
    check("返回后按钮文字复原为“取消”", text_of(main, kCancelBtn) == "取消")
    click(main, kInstallBtn)         # 再进确认页
    check("再次进入确认页", wait_btn(main, "覆盖安装", timeout=4))
    click(main, kInstallBtn)         # 覆盖安装
    check("覆盖后完成", wait_btn(main, "完成", timeout=15))
check("覆盖后版本仍为 0.2.1", stored_version() == "0.2.1", str(stored_version()))
check("程序文件在位",
      os.path.isfile(os.path.join(INSTALL, "ACOCConfigurator.exe")))
close_finish(main)
kill_app()

print("=== 模拟 v0.1 旧版：确认页仍应出现 ===")
set_stored_version("1.0.0")
p = launch()
main = find_window(p.pid, "AcocSetup", TITLE)
click(main, kInstallBtn)
check("检测到旧版，进入确认页", wait_btn(main, "覆盖安装", timeout=6))
click(main, kCancelBtn)
time.sleep(0.5)
u32.PostMessageW(main, 0x0010, 0, 0)   # WM_CLOSE
time.sleep(1.0)

print("=== 收尾：卸载 ===")
up = subprocess.Popen([os.path.join(INSTALL, "uninstall.exe")], cwd=INSTALL)
time.sleep(2.5)
main = find_window(up.pid, "AcocSetup")
if main:
    click(main, kInstallBtn)
    dismiss_done(up.pid, main)
deadline = time.time() + 30
while time.time() < deadline and os.path.isdir(INSTALL):
    time.sleep(0.5)
check("安装目录已清除", not os.path.isdir(INSTALL))
check("注册表项已清除", stored_version() is None)

print()
print("结果：%s" % ("全部通过" if not fails else "失败 %d 项 -> %s" % (len(fails), fails)))
sys.exit(1 if fails else 0)
