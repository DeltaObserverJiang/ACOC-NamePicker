# 覆盖安装流程：没装过时不该弹框，装过之后要先问一句，
# 选“否”能干净退回，选“是”才继续。
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

kInstallBtn, kCancelBtn = 104, 105
IDYES, IDNO = 6, 7

fails = []


def check(label, ok, extra=""):
    print(("  OK   " if ok else "  FAIL ") + label + ("  " + extra if extra else ""))
    if not ok:
        fails.append(label)


class RECT(ctypes.Structure):
    _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long),
                ("r", ctypes.c_long), ("b", ctypes.c_long)]


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


def dlg_text(dlg):
    """对话框正文：遍历直接子控件里的静态文本"""
    parts = []
    buf = ctypes.create_unicode_buffer(2048)
    u32.GetWindowTextW(dlg, buf, 2048)
    parts.append(buf.value)

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(h, l):
        t = ctypes.create_unicode_buffer(2048)
        u32.GetWindowTextW(h, t, 2048)
        if t.value:
            parts.append(t.value)
        return True

    u32.EnumChildWindows(dlg, cb, 0)
    return "\n".join(parts)


def wait_dialog(pid, timeout=6.0):
    end = time.time() + timeout
    while time.time() < end:
        d = find_window(pid, "#32770")
        if d:
            time.sleep(0.3)  # 等文字画全
            return d
        time.sleep(0.15)
    return None


def answer(dlg, ctrl_id):
    u32.SendMessageW(dlg, 0x0111, ctrl_id, 0)  # WM_COMMAND
    time.sleep(0.6)


def click(main, ctrl_id):
    """必须用 PostMessage：BM_CLICK 是同步的，而按钮处理函数里会弹模态框，
    用 SendMessage 会当场死锁——本线程卡在等待里，没法再去点那个对话框。"""
    c = u32.GetDlgItem(main, ctrl_id)
    u32.PostMessageW(c, 0x00F5, 0, 0)  # BM_CLICK
    time.sleep(2.0)


def launch():
    p = subprocess.Popen([SETUP], cwd=os.path.dirname(SETUP))
    time.sleep(2.5)
    return p


def close_setup(pid):
    for _ in range(4):
        d = find_window(pid, "#32770")
        if d:
            answer(d, IDYES)
        m = find_window(pid, "AcocSetup")
        if not m:
            return
        click(m, kInstallBtn if _ else kCancelBtn)
    time.sleep(0.5)


def stored_version():
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG)
        return winreg.QueryValueEx(k, "DisplayVersion")[0]
    except FileNotFoundError:
        return None


def set_stored_version(v):
    k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG, 0, winreg.KEY_SET_VALUE)
    winreg.SetValueEx(k, "DisplayVersion", 0, winreg.REG_SZ, v)


print("=== 清干净 ===")
subprocess.run(["taskkill", "/F", "/IM", "ACOCConfiguratorSetup.exe"],
               capture_output=True)
if os.path.isdir(INSTALL):
    shutil.rmtree(INSTALL, ignore_errors=True)
try:
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, REG)
except FileNotFoundError:
    pass
time.sleep(0.5)

print("=== 第一次安装：不该弹询问框 ===")
p = launch()
main = find_window(p.pid, "AcocSetup", TITLE)
check("安装窗口已出现", main is not None)
click(main, kInstallBtn)
d = find_window(p.pid, "#32770")
check("首次安装没有多问", d is None, "(出现了一个对话框)" if d else "")
if d:
    answer(d, IDYES)
check("已写入注册表", stored_version() == "0.2.0", str(stored_version()))
close_setup(p.pid)
time.sleep(0.8)

print("=== 第二次安装：应询问是否覆盖 ===")
p = launch()
main = find_window(p.pid, "AcocSetup", TITLE)
click(main, kInstallBtn)
d = wait_dialog(p.pid)
check("弹出了询问框", d is not None)
if d:
    txt = dlg_text(d)
    check("正文说明了已装版本", "v0.2.0" in txt, repr(txt[:160]))
    answer(d, IDNO)
    time.sleep(0.6)
    main2 = find_window(p.pid, "AcocSetup", TITLE)
    check("选“否”后窗口还在，退回选项页", main2 is not None)
    st = ctypes.create_unicode_buffer(200)
    u32.GetWindowTextW(u32.GetDlgItem(main2, 108), st, 200)
    check("状态栏提示已取消", "取消" in st.value, repr(st.value))

    click(main2, kInstallBtn)
    d2 = wait_dialog(p.pid)
    check("再次弹出询问框", d2 is not None)
    if d2:
        answer(d2, IDYES)
check("覆盖后版本仍是 0.2.0", stored_version() == "0.2.0", str(stored_version()))
check("程序文件在位",
      os.path.isfile(os.path.join(INSTALL, "ACOCConfigurator.exe")))
close_setup(p.pid)
time.sleep(0.8)

print("=== 模拟 v0.1 旧版：应显示 0.1 ===")
set_stored_version("1.0.0")
p = launch()
main = find_window(p.id if False else p.pid, "AcocSetup", TITLE)
click(main, kInstallBtn)
d = wait_dialog(p.pid)
check("弹出了询问框", d is not None)
if d:
    txt = dlg_text(d)
    check("旧版的 1.0.0 显示为 v0.1", "v0.1" in txt, repr(txt[:160]))
    answer(d, IDNO)
close_setup(p.pid)
time.sleep(0.5)

print("=== 收尾：卸载 ===")
up = subprocess.Popen([os.path.join(INSTALL, "uninstall.exe")], cwd=INSTALL)
time.sleep(2.0)
main = find_window(up.pid, "AcocSetup")
if main:
    click(main, kInstallBtn)
    d = wait_dialog(up.pid)
    if d:
        answer(d, IDYES)   # “卸载完成”只有一个确定
time.sleep(6)
check("安装目录已清除", not os.path.isdir(INSTALL))
check("注册表项已清除", stored_version() is None)

print()
print("结果：%s" % ("全部通过" if not fails else "失败 %d 项 -> %s" % (len(fails), fails)))
sys.exit(1 if fails else 0)
